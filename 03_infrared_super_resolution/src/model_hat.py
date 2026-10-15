"""
HAT: Hybrid Attention Transformer for image super-resolution.
Reference: Chen et al., "Activating More Pixels in Image Super-Resolution
Transformer", CVPR 2023.

Adapted for single-channel (grayscale) infrared 2x super-resolution.

Self-contained (no timm dependency).  Key adaptations vs. the official code:
  * in_chans = 1
  * per-image mean subtraction (matches inference where batch=1)
  * shifted-window attention masks are recomputed at runtime for the actual
    input resolution, so train (patch) and inference (full image) agree
  * optional gradient checkpointing to fit patch=128 on a 24 GB GPU
"""

import math
import torch
import torch.nn as nn
import torch.nn.functional as F
import torch.utils.checkpoint as cp


# ---------------------------------------------------------------------------
# Utilities
# ---------------------------------------------------------------------------

def drop_path_f(x, drop_prob: float = 0., training: bool = False):
    if drop_prob == 0. or not training:
        return x
    keep_prob = 1 - drop_prob
    shape = (x.shape[0],) + (1,) * (x.ndim - 1)
    random_tensor = keep_prob + torch.rand(shape, dtype=x.dtype, device=x.device)
    random_tensor.floor_()
    return x.div(keep_prob) * random_tensor


class DropPath(nn.Module):
    def __init__(self, drop_prob=0.):
        super().__init__()
        self.drop_prob = drop_prob

    def forward(self, x):
        return drop_path_f(x, self.drop_prob, self.training)


def window_partition(x, window_size):
    """(B, H, W, C) -> (num_windows*B, window_size, window_size, C)"""
    b, h, w, c = x.shape
    x = x.view(b, h // window_size, window_size, w // window_size, window_size, c)
    return x.permute(0, 1, 3, 2, 4, 5).contiguous().view(-1, window_size, window_size, c)


def window_reverse(windows, window_size, h, w):
    """(num_windows*B, window_size, window_size, C) -> (B, H, W, C)"""
    b = int(windows.shape[0] / (h * w / window_size / window_size))
    x = windows.view(b, h // window_size, w // window_size, window_size, window_size, -1)
    return x.permute(0, 1, 3, 2, 4, 5).contiguous().view(b, h, w, -1)


# ---------------------------------------------------------------------------
# Relative-position indices (resolution independent)
# ---------------------------------------------------------------------------

def calculate_rpi_sa(window_size):
    coords_h = torch.arange(window_size)
    coords_w = torch.arange(window_size)
    coords = torch.stack(torch.meshgrid([coords_h, coords_w], indexing="ij"))
    coords_flatten = torch.flatten(coords, 1)
    rel = coords_flatten[:, :, None] - coords_flatten[:, None, :]
    rel = rel.permute(1, 2, 0).contiguous()
    rel[:, :, 0] += window_size - 1
    rel[:, :, 1] += window_size - 1
    rel[:, :, 0] *= 2 * window_size - 1
    return rel.sum(-1)  # (ws*ws, ws*ws)


def calculate_rpi_oca(window_size, overlap_ratio):
    ws_ori = window_size
    ws_ext = window_size + int(overlap_ratio * window_size)

    coords_h = torch.arange(ws_ori)
    coords_w = torch.arange(ws_ori)
    coords_ori = torch.stack(torch.meshgrid([coords_h, coords_w], indexing="ij"))
    coords_ori_flat = torch.flatten(coords_ori, 1)  # 2, ws_ori^2

    coords_h = torch.arange(ws_ext)
    coords_w = torch.arange(ws_ext)
    coords_ext = torch.stack(torch.meshgrid([coords_h, coords_w], indexing="ij"))
    coords_ext_flat = torch.flatten(coords_ext, 1)  # 2, ws_ext^2

    rel = coords_ext_flat[:, None, :] - coords_ori_flat[:, :, None]  # 2, ws_ori^2, ws_ext^2
    rel = rel.permute(1, 2, 0).contiguous()
    rel[:, :, 0] += ws_ori - ws_ext + 1
    rel[:, :, 1] += ws_ori - ws_ext + 1
    rel[:, :, 0] *= ws_ori + ws_ext - 1
    return rel.sum(-1)


def calculate_mask(x_size, window_size, shift_size):
    h, w = x_size
    img_mask = torch.zeros((1, h, w, 1))
    h_slices = (slice(0, -window_size), slice(-window_size, -shift_size), slice(-shift_size, None))
    w_slices = (slice(0, -window_size), slice(-window_size, -shift_size), slice(-shift_size, None))
    cnt = 0
    for hs in h_slices:
        for ws in w_slices:
            img_mask[:, hs, ws, :] = cnt
            cnt += 1
    mask_windows = window_partition(img_mask, window_size).view(-1, window_size * window_size)
    attn_mask = mask_windows.unsqueeze(1) - mask_windows.unsqueeze(2)
    return attn_mask.masked_fill(attn_mask != 0, -100.0).masked_fill(attn_mask == 0, 0.0)


# ---------------------------------------------------------------------------
# Building blocks
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


class ChannelAttention(nn.Module):
    def __init__(self, num_feat, squeeze_factor=16):
        super().__init__()
        self.attention = nn.Sequential(
            nn.AdaptiveAvgPool2d(1),
            nn.Conv2d(num_feat, num_feat // squeeze_factor, 1, padding=0),
            nn.ReLU(inplace=True),
            nn.Conv2d(num_feat // squeeze_factor, num_feat, 1, padding=0),
            nn.Sigmoid(),
        )

    def forward(self, x):
        return x * self.attention(x)


class CAB(nn.Module):
    """Channel-Attention Block — the convolutional branch of HAB."""
    def __init__(self, num_feat, compress_ratio=3, squeeze_factor=30):
        super().__init__()
        self.cab = nn.Sequential(
            nn.Conv2d(num_feat, num_feat // compress_ratio, 3, 1, 1),
            nn.GELU(),
            nn.Conv2d(num_feat // compress_ratio, num_feat, 3, 1, 1),
            ChannelAttention(num_feat, squeeze_factor),
        )

    def forward(self, x):
        return self.cab(x)


class WindowAttention(nn.Module):
    def __init__(self, dim, window_size, num_heads, qkv_bias=True):
        super().__init__()
        self.dim = dim
        self.window_size = window_size  # (Wh, Ww)
        self.num_heads = num_heads
        self.scale = (dim // num_heads) ** -0.5

        self.relative_position_bias_table = nn.Parameter(
            torch.zeros((2 * window_size[0] - 1) * (2 * window_size[1] - 1), num_heads)
        )
        nn.init.trunc_normal_(self.relative_position_bias_table, std=0.02)

        self.qkv = nn.Linear(dim, dim * 3, bias=qkv_bias)
        self.proj = nn.Linear(dim, dim)
        self.softmax = nn.Softmax(dim=-1)

    def forward(self, x, rpi, mask=None):
        b_, n, c = x.shape
        qkv = self.qkv(x).reshape(b_, n, 3, self.num_heads, c // self.num_heads).permute(2, 0, 3, 1, 4)
        q, k, v = qkv[0], qkv[1], qkv[2]

        attn = (q * self.scale) @ k.transpose(-2, -1)

        rpb = self.relative_position_bias_table[rpi.view(-1)].view(
            self.window_size[0] * self.window_size[1],
            self.window_size[0] * self.window_size[1], -1)
        attn = attn + rpb.permute(2, 0, 1).contiguous().unsqueeze(0)

        if mask is not None:
            nw = mask.shape[0]
            attn = attn.view(b_ // nw, nw, self.num_heads, n, n) + mask.unsqueeze(1).unsqueeze(0)
            attn = attn.view(-1, self.num_heads, n, n)

        attn = self.softmax(attn)
        x = (attn @ v).transpose(1, 2).reshape(b_, n, c)
        return self.proj(x)


class HAB(nn.Module):
    """Hybrid Attention Block: (S)W-MSA in parallel with a CAB conv branch."""
    def __init__(self, dim, num_heads, window_size=16, shift_size=0,
                 compress_ratio=3, squeeze_factor=30, conv_scale=0.01,
                 mlp_ratio=2., qkv_bias=True, drop_path=0.):
        super().__init__()
        self.window_size = window_size
        self.shift_size = shift_size
        self.conv_scale = conv_scale

        self.norm1 = nn.LayerNorm(dim)
        self.attn = WindowAttention(dim, (window_size, window_size), num_heads, qkv_bias)
        self.conv_block = CAB(dim, compress_ratio, squeeze_factor)
        self.drop_path = DropPath(drop_path) if drop_path > 0 else nn.Identity()
        self.norm2 = nn.LayerNorm(dim)
        self.mlp = Mlp(dim, int(dim * mlp_ratio))

    def forward(self, x, x_size, rpi_sa, attn_mask):
        h, w = x_size
        b, _, c = x.shape
        shortcut = x
        x = self.norm1(x).view(b, h, w, c)

        # Convolutional (channel-attention) branch
        conv_x = self.conv_block(x.permute(0, 3, 1, 2))
        conv_x = conv_x.permute(0, 2, 3, 1).contiguous().view(b, h * w, c)

        # (Shifted) window self-attention branch
        if self.shift_size > 0:
            shifted = torch.roll(x, shifts=(-self.shift_size, -self.shift_size), dims=(1, 2))
            mask = attn_mask
        else:
            shifted = x
            mask = None

        x_windows = window_partition(shifted, self.window_size).view(-1, self.window_size ** 2, c)
        attn_windows = self.attn(x_windows, rpi=rpi_sa, mask=mask)
        attn_windows = attn_windows.view(-1, self.window_size, self.window_size, c)
        shifted = window_reverse(attn_windows, self.window_size, h, w)

        if self.shift_size > 0:
            attn_x = torch.roll(shifted, shifts=(self.shift_size, self.shift_size), dims=(1, 2))
        else:
            attn_x = shifted
        attn_x = attn_x.view(b, h * w, c)

        x = shortcut + self.drop_path(attn_x) + conv_x * self.conv_scale
        x = x + self.drop_path(self.mlp(self.norm2(x)))
        return x


class OCAB(nn.Module):
    """Overlapping Cross-Attention Block."""
    def __init__(self, dim, window_size, overlap_ratio, num_heads,
                 qkv_bias=True, mlp_ratio=2.):
        super().__init__()
        self.dim = dim
        self.window_size = window_size
        self.num_heads = num_heads
        self.scale = (dim // num_heads) ** -0.5
        self.overlap_win_size = int(window_size * overlap_ratio) + window_size

        self.norm1 = nn.LayerNorm(dim)
        self.qkv = nn.Linear(dim, dim * 3, bias=qkv_bias)
        self.unfold = nn.Unfold(
            kernel_size=(self.overlap_win_size, self.overlap_win_size),
            stride=window_size,
            padding=(self.overlap_win_size - window_size) // 2,
        )
        self.relative_position_bias_table = nn.Parameter(
            torch.zeros((window_size + self.overlap_win_size - 1) *
                        (window_size + self.overlap_win_size - 1), num_heads)
        )
        nn.init.trunc_normal_(self.relative_position_bias_table, std=0.02)

        self.proj = nn.Linear(dim, dim)
        self.norm2 = nn.LayerNorm(dim)
        self.mlp = Mlp(dim, int(dim * mlp_ratio))
        self.softmax = nn.Softmax(dim=-1)

    def forward(self, x, x_size, rpi):
        h, w = x_size
        b, _, c = x.shape
        ows = self.overlap_win_size
        ws = self.window_size

        shortcut = x
        x = self.norm1(x).view(b, h, w, c)

        qkv = self.qkv(x).reshape(b, h, w, 3, c).permute(3, 0, 4, 1, 2)  # 3, b, c, h, w
        q = qkv[0].permute(0, 2, 3, 1)              # b, h, w, c
        kv = torch.cat((qkv[1], qkv[2]), dim=1)     # b, 2c, h, w

        # non-overlapping query windows
        q_windows = window_partition(q, ws).view(-1, ws * ws, c)  # (b*nw, ws^2, c)

        # overlapping key/value windows via unfold
        kv_windows = self.unfold(kv)                              # b, 2c*ows*ows, nw
        nw = kv_windows.shape[-1]
        kv_windows = kv_windows.view(b, 2 * c, ows * ows, nw)
        kv_windows = kv_windows.permute(0, 3, 2, 1).contiguous()  # b, nw, ows^2, 2c
        kv_windows = kv_windows.view(b * nw, ows * ows, 2, c)
        k_windows = kv_windows[:, :, 0, :]                        # (b*nw, ows^2, c)
        v_windows = kv_windows[:, :, 1, :]

        b_, nq, _ = q_windows.shape
        n = k_windows.shape[1]
        d = c // self.num_heads
        q = q_windows.reshape(b_, nq, self.num_heads, d).permute(0, 2, 1, 3)
        k = k_windows.reshape(b_, n, self.num_heads, d).permute(0, 2, 1, 3)
        v = v_windows.reshape(b_, n, self.num_heads, d).permute(0, 2, 1, 3)

        attn = (q * self.scale) @ k.transpose(-2, -1)
        rpb = self.relative_position_bias_table[rpi.view(-1)].view(ws * ws, ows * ows, -1)
        attn = attn + rpb.permute(2, 0, 1).contiguous().unsqueeze(0)
        attn = self.softmax(attn)

        attn_windows = (attn @ v).transpose(1, 2).reshape(b_, nq, c)
        attn_windows = attn_windows.view(-1, ws, ws, c)
        x = window_reverse(attn_windows, ws, h, w).view(b, h * w, c)

        x = self.proj(x) + shortcut
        x = x + self.mlp(self.norm2(x))
        return x


class AttenBlocks(nn.Module):
    """A sequence of HABs followed by one OCAB."""
    def __init__(self, dim, depth, num_heads, window_size, compress_ratio,
                 squeeze_factor, conv_scale, overlap_ratio, mlp_ratio,
                 qkv_bias=True, drop_path=0.):
        super().__init__()
        self.blocks = nn.ModuleList([
            HAB(
                dim, num_heads, window_size,
                shift_size=0 if (i % 2 == 0) else window_size // 2,
                compress_ratio=compress_ratio, squeeze_factor=squeeze_factor,
                conv_scale=conv_scale, mlp_ratio=mlp_ratio, qkv_bias=qkv_bias,
                drop_path=drop_path[i] if isinstance(drop_path, list) else drop_path,
            )
            for i in range(depth)
        ])
        self.overlap_attn = OCAB(dim, window_size, overlap_ratio, num_heads,
                                 qkv_bias=qkv_bias, mlp_ratio=mlp_ratio)

    def forward(self, x, x_size, params):
        for blk in self.blocks:
            x = blk(x, x_size, params["rpi_sa"], params["attn_mask"])
        x = self.overlap_attn(x, x_size, params["rpi_oca"])
        return x


class RHAG(nn.Module):
    """Residual Hybrid Attention Group."""
    def __init__(self, dim, depth, num_heads, window_size, compress_ratio,
                 squeeze_factor, conv_scale, overlap_ratio, mlp_ratio,
                 qkv_bias=True, drop_path=0., use_checkpoint=False):
        super().__init__()
        self.use_checkpoint = use_checkpoint
        self.residual_group = AttenBlocks(
            dim, depth, num_heads, window_size, compress_ratio, squeeze_factor,
            conv_scale, overlap_ratio, mlp_ratio, qkv_bias, drop_path,
        )
        self.conv = nn.Conv2d(dim, dim, 3, 1, 1)

    def forward(self, x, x_size, params):
        b, _, c = x.shape
        h, w = x_size
        if self.use_checkpoint and self.training:
            res = cp.checkpoint(self.residual_group, x, x_size, params, use_reentrant=False)
        else:
            res = self.residual_group(x, x_size, params)
        feat = res.transpose(1, 2).view(b, c, h, w)
        feat = self.conv(feat).flatten(2).transpose(1, 2)
        return feat + x


class Upsample(nn.Sequential):
    def __init__(self, scale, num_feat):
        m = []
        for _ in range(int(math.log2(scale))):
            m += [nn.Conv2d(num_feat, 4 * num_feat, 3, 1, 1), nn.PixelShuffle(2)]
        super().__init__(*m)


# ---------------------------------------------------------------------------
# HAT
# ---------------------------------------------------------------------------

class HAT(nn.Module):
    def __init__(self, in_chans=1, embed_dim=144,
                 depths=(6, 6, 6, 6, 6, 6), num_heads=(6, 6, 6, 6, 6, 6),
                 window_size=16, compress_ratio=3, squeeze_factor=30,
                 conv_scale=0.01, overlap_ratio=0.5, mlp_ratio=2.,
                 qkv_bias=True, drop_path_rate=0.1, upscale=2,
                 use_checkpoint=False, **kwargs):
        super().__init__()
        self.window_size = window_size
        self.shift_size = window_size // 2
        self.upscale = upscale

        # resolution-independent relative-position indices
        self.register_buffer("rpi_sa", calculate_rpi_sa(window_size))
        self.register_buffer("rpi_oca", calculate_rpi_oca(window_size, overlap_ratio))

        self.conv_first = nn.Conv2d(in_chans, embed_dim, 3, 1, 1)

        dpr = [x.item() for x in torch.linspace(0, drop_path_rate, sum(depths))]
        self.layers = nn.ModuleList([
            RHAG(
                dim=embed_dim, depth=depths[i], num_heads=num_heads[i],
                window_size=window_size, compress_ratio=compress_ratio,
                squeeze_factor=squeeze_factor, conv_scale=conv_scale,
                overlap_ratio=overlap_ratio, mlp_ratio=mlp_ratio, qkv_bias=qkv_bias,
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
            nn.init.constant_(m.weight, 1.0)

    def _pad(self, x):
        _, _, h, w = x.shape
        ph = (self.window_size - h % self.window_size) % self.window_size
        pw = (self.window_size - w % self.window_size) % self.window_size
        return F.pad(x, (0, pw, 0, ph), "reflect"), h, w

    def forward(self, x):
        x, h0, w0 = self._pad(x)
        x_size = (x.shape[2], x.shape[3])

        attn_mask = calculate_mask(x_size, self.window_size, self.shift_size).to(x.device)
        params = {"rpi_sa": self.rpi_sa, "rpi_oca": self.rpi_oca, "attn_mask": attn_mask}

        mean = x.mean(dim=(2, 3), keepdim=True)
        x = x - mean

        x = self.conv_first(x)
        res = x
        b, c, hf, wf = x.shape
        seq = x.flatten(2).transpose(1, 2)
        for layer in self.layers:
            seq = layer(seq, x_size, params)
        seq = self.norm(seq)
        body = seq.transpose(1, 2).view(b, c, hf, wf)

        x = self.conv_after_body(body) + res
        x = self.conv_before_upsample(x)
        x = self.upsample(x)
        x = self.conv_last(x)

        x = x + mean
        return x[:, :, :h0 * self.upscale, :w0 * self.upscale]

    def param_count_m(self):
        return sum(p.numel() for p in self.parameters()) / 1e6

    def model_size_mb_fp16(self):
        return sum(p.numel() for p in self.parameters()) * 2 / 1024 ** 2


def build_model(config: dict) -> HAT:
    return HAT(
        in_chans=config.get("n_colors", 1),
        embed_dim=config.get("embed_dim", 144),
        depths=tuple(config.get("depths", (6, 6, 6, 6, 6, 6))),
        num_heads=tuple(config.get("num_heads", (6, 6, 6, 6, 6, 6))),
        window_size=config.get("window_size", 16),
        compress_ratio=config.get("compress_ratio", 3),
        squeeze_factor=config.get("squeeze_factor", 30),
        conv_scale=config.get("conv_scale", 0.01),
        overlap_ratio=config.get("overlap_ratio", 0.5),
        mlp_ratio=config.get("mlp_ratio", 2.0),
        upscale=config.get("scale", 2),
        use_checkpoint=config.get("use_checkpoint", False),
    )


if __name__ == "__main__":
    # CPU smoke test: forward + backward, shape & gradient sanity
    torch.manual_seed(0)
    cfg = dict(embed_dim=60, depths=(2, 2), num_heads=(4, 4), window_size=8,
               scale=2, n_colors=1)
    net = build_model(cfg)
    print(f"params: {net.param_count_m():.3f}M  fp16: {net.model_size_mb_fp16():.1f}MB")
    inp = torch.rand(2, 1, 32, 40, requires_grad=True)
    out = net(inp)
    print("in:", tuple(inp.shape), "-> out:", tuple(out.shape))
    assert out.shape == (2, 1, 64, 80), "scale/shape mismatch"
    loss = out.mean()
    loss.backward()
    g = inp.grad
    assert torch.isfinite(out).all() and torch.isfinite(g).all(), "non-finite values"
    print("backward OK, grads finite. HAT smoke test passed.")
