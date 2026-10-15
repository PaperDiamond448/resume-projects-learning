"""
RCAN: Residual Channel Attention Network
Adapted for grayscale infrared 2x super-resolution.
"""

import math
import torch
import torch.nn as nn


def default_conv(in_ch, out_ch, kernel_size, bias=True):
    return nn.Conv2d(in_ch, out_ch, kernel_size,
                     padding=kernel_size // 2, bias=bias)


class ChannelAttention(nn.Module):
    def __init__(self, n_feats, reduction=16):
        super().__init__()
        self.body = nn.Sequential(
            nn.AdaptiveAvgPool2d(1),
            nn.Conv2d(n_feats, n_feats // reduction, 1, bias=True),
            nn.ReLU(inplace=True),
            nn.Conv2d(n_feats // reduction, n_feats, 1, bias=True),
            nn.Sigmoid(),
        )

    def forward(self, x):
        return x * self.body(x)


class RCAB(nn.Module):
    def __init__(self, n_feats, kernel_size=3, reduction=16, res_scale=1.0):
        super().__init__()
        self.res_scale = res_scale
        self.body = nn.Sequential(
            default_conv(n_feats, n_feats, kernel_size),
            nn.ReLU(inplace=True),
            default_conv(n_feats, n_feats, kernel_size),
            ChannelAttention(n_feats, reduction),
        )

    def forward(self, x):
        return x + self.body(x) * self.res_scale


class ResidualGroup(nn.Module):
    def __init__(self, n_feats, n_resblocks, kernel_size=3, reduction=16):
        super().__init__()
        blocks = [RCAB(n_feats, kernel_size, reduction) for _ in range(n_resblocks)]
        blocks.append(default_conv(n_feats, n_feats, kernel_size))
        self.body = nn.Sequential(*blocks)

    def forward(self, x):
        return x + self.body(x)


class Upsampler(nn.Sequential):
    def __init__(self, scale, n_feats, bias=True):
        m = []
        if (scale & (scale - 1)) == 0:
            for _ in range(int(math.log2(scale))):
                m += [default_conv(n_feats, 4 * n_feats, 3, bias=bias), nn.PixelShuffle(2)]
        else:
            m += [default_conv(n_feats, scale * scale * n_feats, 3, bias=bias), nn.PixelShuffle(scale)]
        super().__init__(*m)


class RCAN(nn.Module):
    def __init__(self, n_resgroups=10, n_resblocks=20, n_feats=64,
                 scale=2, n_colors=1, reduction=16, res_scale=1.0):
        super().__init__()
        self.scale = scale
        kernel_size = 3
        self.head = default_conv(n_colors, n_feats, kernel_size)
        rg_list = [ResidualGroup(n_feats, n_resblocks, kernel_size, reduction)
                   for _ in range(n_resgroups)]
        rg_list.append(default_conv(n_feats, n_feats, kernel_size))
        self.body = nn.Sequential(*rg_list)
        self.tail = nn.Sequential(
            Upsampler(scale, n_feats),
            default_conv(n_feats, n_colors, kernel_size),
        )

    def forward(self, x):
        mean = x.mean(dim=(-2, -1), keepdim=True)
        x_nm = x - mean
        shallow = self.head(x_nm)
        deep = self.body(shallow)
        out = self.tail(deep + shallow)
        return out + mean

    def param_count_m(self):
        return sum(p.numel() for p in self.parameters()) / 1e6

    def model_size_mb_fp16(self):
        return sum(p.numel() for p in self.parameters()) * 2 / 1024 ** 2


def build_model(config: dict) -> RCAN:
    return RCAN(
        n_resgroups=config.get("n_resgroups", 10),
        n_resblocks=config.get("n_resblocks", 20),
        n_feats=config.get("n_feats", 64),
        scale=config.get("scale", 2),
        n_colors=config.get("n_colors", 1),
        reduction=config.get("reduction", 16),
    )
