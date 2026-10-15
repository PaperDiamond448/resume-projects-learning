"""
SwinIR for 2x infrared super-resolution.
Reference: Liang et al., SwinIR, ICCVW 2021.

Adaptations for this competition:
  * in_chans = 1 (grayscale)
  * per-image mean subtraction (matches inference where batch=1; the old
    global x.mean() coupled samples within a batch and created a
    train/inference normalisation mismatch)
  * optional gradient checkpointing (use_checkpoint) to fit patch=128 on a
    24 GB GPU
"""

import math
import torch
import torch.nn as nn
import torch.nn.functional as F
import torch.utils.checkpoint as cp


# ---------------------------------------------------------------------------
# Utilities
# ---------------------------------------------------------------------------

def drop_path_f(x, drop_prob=0., training=False):
    if drop_prob == 0. or not training:
        return x
    keep_prob = 1 - drop_prob
    shape = (x.shape[0],) + (1,) * (x.ndim - 1)
    random_tensor = torch.rand(shape, dtype=x.dtype, device=x.device)
    random_tensor = torch.floor(random_tensor + keep_prob)
    return x / keep_prob * random_tensor


class DropPath(nn.Module):
    def __init__(self, drop_prob=0.):
        super().__init__()
        self.drop_prob = drop_prob

    def forward(self, x):
        return drop_path_f(x, self.drop_prob, self.training)


def window_partition(x, window_size):
    """(B, H, W, C) -> (num_windows*B, Wh, Ww, C)"""
    B, H, W, C = x.shape
    x = x.view(B, H // window_size, window_size, W // window_size, window_size, C)
    return x.permute(0, 1, 3, 2, 4, 5).contiguous().view(-1, window_size, window_size, C)


def window_reverse(windows, window_size, H, W):
    """(num_windows*B, Wh, Ww, C) -> (B, H, W, C)"""
    B = int(windows.shape[0] / (H * W / window_size / window_size))
    x = windows.view(B, H // window_size, W // window_size, window_size, window_size, -1)
    return x.permute(0, 1, 3, 2, 4, 5).contiguous().view(B, H, W, -1)


# ---------------------------------------------------------------------------
# Attention & Transformer blocks
# ---------------------------------------------------------------------------

class Mlp(nn.Module):
    def __init__(self, in_features, hidden_features=None, drop=0.):
        super().__init__()
        hidden_features = hidden_features or in_features
        self.fc1 = nn.Linear(in_features, hidden_features)
        self.act = nn.GELU()
        self.fc2 = nn.Linear(hidden_features, in_features)
        self.drop = nn.Dropout(drop)

    def forward(self, x):
        return self.drop(self.fc2(self.drop(self.act(self.fc1(x)))))


class WindowAttention(nn.Module):
    def __init__(self, dim, window_size, num_heads, qkv_bias=True, attn_drop=0., proj_drop=0.):
        super().__init__()
        self.num_heads = num_heads
        self.scale = (dim // num_heads) ** -0.5
        self.window_size = window_size  # (Wh, Ww)

        self.relative_position_bias_table = nn.Parameter(
            torch.zeros((2 * window_size[0] - 1) * (2 * window_size[1] - 1), num_heads)
        )
        nn.init.trunc_normal_(self.relative_position_bias_table, std=0.02)

        coords = torch.stack(torch.meshgrid(
            torch.arange(window_size[0]), torch.arange(window_size[1]), indexing='ij'
        ))
        coords_flat = torch.flatten(coords, 1)
        rel = coords_flat[:, :, None] - coords_flat[:, None, :]
        rel = rel.permute(1, 2, 0).contiguous()
        rel[:, :, 0] += window_size[0] - 1
        rel[:, :, 1] += window_size[1] - 1
        rel[:, :, 0] *= 2 * window_size[1] - 1
        self.register_buffer("relative_position_index", rel.sum(-1))

        self.qkv = nn.Linear(dim, dim * 3, bias=qkv_bias)
        self.attn_drop = nn.Dropout(attn_drop)
        self.proj = nn.Linear(dim, dim)
        self.proj_drop = nn.Dropout(proj_drop)

    def forward(self, x, mask=None):
        B_, N, C = x.shape
        qkv = self.qkv(x).reshape(B_, N, 3, self.num_heads, C // self.num_heads).permute(2, 0, 3, 1, 4)
        q, k, v = qkv.unbind(0)
        attn = (q * self.scale) @ k.transpose(-2, -1)

        rpb = self.relative_position_bias_table[self.relative_position_index.view(-1)]
        rpb = rpb.view(self.window_size[0] * self.window_size[1],
                       self.window_size[0] * self.window_size[1], -1).permute(2, 0, 1).contiguous()
        attn = attn + rpb.unsqueeze(0)

        if mask is not None:
            nW = mask.shape[0]
            attn = attn.view(B_ // nW, nW, self.num_heads, N, N) + mask.unsqueeze(1).unsqueeze(0)
            attn = attn.view(-1, self.num_heads, N, N)

        attn = self.attn_drop(attn.softmax(dim=-1))
        x = (attn @ v).transpose(1, 2).reshape(B_, N, C)
        return self.proj_drop(self.proj(x))


class SwinTransformerBlock(nn.Module):
    def __init__(self, dim, input_resolution, num_heads, window_size=8, shift_size=0,
                 mlp_ratio=4., qkv_bias=True, drop=0., attn_drop=0., drop_path=0.):
        super().__init__()
        self.input_resolution = input_resolution
        ws = min(window_size, min(input_resolution))
        self.window_size = ws
        self.shift_size = shift_size if min(input_resolution) > ws else 0

        self.norm1 = nn.LayerNorm(dim)
        self.attn = WindowAttention(dim, (ws, ws), num_heads, qkv_bias, attn_drop, drop)
        self.drop_path = DropPath(drop_path) if drop_path > 0 else nn.Identity()
        self.norm2 = nn.LayerNorm(dim)
        self.mlp = Mlp(dim, int(dim * mlp_ratio), drop=drop)

        mask = self._calc_mask(input_resolution) if self.shift_size > 0 else None
        self.register_buffer("attn_mask", mask)

    def _calc_mask(self, x_size):
        H, W = x_size
        img_mask = torch.zeros(1, H, W, 1)
        cnt = 0
        for hs in (slice(0, -self.window_size), slice(-self.window_size, -self.shift_size), slice(-self.shift_size, None)):
            for ws in (slice(0, -self.window_size), slice(-self.window_size, -self.shift_size), slice(-self.shift_size, None)):
                img_mask[:, hs, ws, :] = cnt
                cnt += 1
        mw = window_partition(img_mask, self.window_size).view(-1, self.window_size * self.window_size)
        mask = mw.unsqueeze(1) - mw.unsqueeze(2)
        return mask.masked_fill(mask != 0, -100.).masked_fill(mask == 0, 0.)

    def forward(self, x, x_size):
        H, W = x_size
        B, _, C = x.shape
        shortcut = x
        x = self.norm1(x).view(B, H, W, C)

        if self.shift_size > 0:
            x = torch.roll(x, (-self.shift_size, -self.shift_size), (1, 2))

        x_w = window_partition(x, self.window_size).view(-1, self.window_size * self.window_size, C)
        mask = self.attn_mask if x_size == self.input_resolution else self._calc_mask(x_size).to(x.device)
        x_w = self.attn(x_w, mask=mask)

        x = window_reverse(x_w.view(-1, self.window_size, self.window_size, C), self.window_size, H, W)
        if self.shift_size > 0:
            x = torch.roll(x, (self.shift_size, self.shift_size), (1, 2))

        x = shortcut + self.drop_path(x.view(B, H * W, C))
        return x + self.drop_path(self.mlp(self.norm2(x)))


class BasicLayer(nn.Module):
    def __init__(self, dim, input_resolution, depth, num_heads, window_size,
                 mlp_ratio=4., qkv_bias=True, drop=0., attn_drop=0., drop_path=0.):
        super().__init__()
        self.blocks = nn.ModuleList([
            SwinTransformerBlock(
                dim, input_resolution, num_heads, window_size,
                shift_size=0 if i % 2 == 0 else window_size // 2,
                mlp_ratio=mlp_ratio, qkv_bias=qkv_bias,
                drop=drop, attn_drop=attn_drop,
                drop_path=drop_path[i] if isinstance(drop_path, list) else drop_path,
            )
            for i in range(depth)
        ])

    def forward(self, x, x_size):
        for blk in self.blocks:
            x = blk(x, x_size)
        return x


class RSTB(nn.Module):
    """Residual Swin Transformer Block"""
    def __init__(self, dim, input_resolution, depth, num_heads, window_size,
                 mlp_ratio=4., qkv_bias=True, drop=0., attn_drop=0., drop_path=0.,
                 use_checkpoint=False):
        super().__init__()
        self.use_checkpoint = use_checkpoint
        self.residual_group = BasicLayer(
            dim, input_resolution, depth, num_heads, window_size,
            mlp_ratio, qkv_bias, drop, attn_drop, drop_path,
        )
        self.conv = nn.Conv2d(dim, dim, 3, 1, 1)

    def forward(self, x, x_size):
        B, C, H, W = x.shape
        x_seq = x.flatten(2).transpose(1, 2)
        if self.use_checkpoint and self.training:
            x_seq = cp.checkpoint(self.residual_group, x_seq, x_size, use_reentrant=False)
        else:
            x_seq = self.residual_group(x_seq, x_size)
        feat = x_seq.transpose(1, 2).view(B, C, H, W)
        return self.conv(feat) + x


# ---------------------------------------------------------------------------
# Upsample
# ---------------------------------------------------------------------------

class Upsample(nn.Sequential):
    def __init__(self, scale, num_feat):
        m = []
        for _ in range(int(math.log2(scale))):
            m += [nn.Conv2d(num_feat, 4 * num_feat, 3, 1, 1), nn.PixelShuffle(2)]
        super().__init__(*m)


# ---------------------------------------------------------------------------
# SwinIR
# ---------------------------------------------------------------------------

class SwinIR(nn.Module):
    def __init__(self, img_size=128, in_chans=1, embed_dim=96,
                 depths=(6, 6, 6, 6, 6, 6), num_heads=(6, 6, 6, 6, 6, 6),
                 window_size=8, mlp_ratio=4., qkv_bias=True,
                 drop_rate=0., attn_drop_rate=0., drop_path_rate=0.1,
                 upscale=2, use_checkpoint=False, **kwargs):
        super().__init__()
        self.upscale = upscale
        self.window_size = window_size

        self.conv_first = nn.Conv2d(in_chans, embed_dim, 3, 1, 1)

        dpr = [x.item() for x in torch.linspace(0, drop_path_rate, sum(depths))]
        self.layers = nn.ModuleList([
            RSTB(
                dim=embed_dim,
                input_resolution=(img_size, img_size),
                depth=depths[i],
                num_heads=num_heads[i],
                window_size=window_size,
                mlp_ratio=mlp_ratio,
                qkv_bias=qkv_bias,
                drop=drop_rate,
                attn_drop=attn_drop_rate,
                drop_path=dpr[sum(depths[:i]):sum(depths[:i + 1])],
                use_checkpoint=use_checkpoint,
            )
            for i in range(len(depths))
        ])

        self.norm = nn.LayerNorm(embed_dim)
        self.conv_after_body = nn.Conv2d(embed_dim, embed_dim, 3, 1, 1)
        self.conv_before_upsample = nn.Sequential(
            nn.Conv2d(embed_dim, 64, 3, 1, 1), nn.LeakyReLU(inplace=True)
        )
        self.upsample = Upsample(upscale, 64)
        self.conv_last = nn.Conv2d(64, in_chans, 3, 1, 1)

        self.apply(self._init_weights)

    def _init_weights(self, m):
        if isinstance(m, nn.Linear):
            nn.init.trunc_normal_(m.weight, std=0.02)
            if m.bias is not None:
                nn.init.constant_(m.bias, 0)
        elif isinstance(m, nn.LayerNorm):
            nn.init.constant_(m.bias, 0)
            nn.init.constant_(m.weight, 1.)

    def _pad(self, x):
        _, _, h, w = x.shape
        ph = (self.window_size - h % self.window_size) % self.window_size
        pw = (self.window_size - w % self.window_size) % self.window_size
        return F.pad(x, (0, pw, 0, ph), 'reflect'), h, w

    def forward(self, x):
        x, H, W = self._pad(x)
        x_size = (x.shape[2], x.shape[3])

        # per-image mean (consistent between training batches and batch=1 inference)
        mean = x.mean(dim=(2, 3), keepdim=True)
        x = x - mean

        x = self.conv_first(x)
        res = x
        for layer in self.layers:
            x = layer(x, x_size)

        B, C, Hf, Wf = x.shape
        x_seq = self.norm(x.flatten(2).transpose(1, 2))
        x = x_seq.transpose(1, 2).view(B, C, Hf, Wf)

        x = self.conv_after_body(x) + res
        x = self.conv_before_upsample(x)
        x = self.upsample(x)
        x = self.conv_last(x)

        x = x + mean
        return x[:, :, :H * self.upscale, :W * self.upscale]

    def param_count_m(self):
        return sum(p.numel() for p in self.parameters()) / 1e6

    def model_size_mb_fp16(self):
        return sum(p.numel() for p in self.parameters()) * 2 / 1024 ** 2


def build_model(config: dict) -> SwinIR:
    return SwinIR(
        img_size=config.get('img_size', 128),
        in_chans=config.get('n_colors', 1),
        embed_dim=config.get('embed_dim', 96),
        depths=tuple(config.get('depths', (6, 6, 6, 6, 6, 6))),
        num_heads=tuple(config.get('num_heads', (6, 6, 6, 6, 6, 6))),
        window_size=config.get('window_size', 8),
        mlp_ratio=config.get('mlp_ratio', 4.),
        upscale=config.get('scale', 2),
        use_checkpoint=config.get('use_checkpoint', False),
    )


if __name__ == "__main__":
    torch.manual_seed(0)
    net = build_model(dict(embed_dim=60, depths=(2, 2), num_heads=(6, 6),
                           window_size=8, scale=2, n_colors=1))
    print(f"params: {net.param_count_m():.3f}M  fp16: {net.model_size_mb_fp16():.1f}MB")
    inp = torch.rand(2, 1, 32, 40, requires_grad=True)
    out = net(inp)
    print("in:", tuple(inp.shape), "-> out:", tuple(out.shape))
    assert out.shape == (2, 1, 64, 80)
    out.mean().backward()
    assert torch.isfinite(out).all() and torch.isfinite(inp.grad).all()
    print("SwinIR smoke test passed.")
