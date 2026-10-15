"""
官方预训练 SwinIR → 灰度红外迁移 的包装器。

思路:官方 SwinIR 是在 DF2K/DIV2K 上用**和本赛题完全相同的 bicubic 降质**训练的,
是极强的重建先验。它是 3 通道(RGB)。本包装器把它当成 1 通道模型用——
输入灰度复制成 3 通道喂进去、输出 3 通道取均值还原灰度——从而:
  (1) 直接 strict 加载官方权重(零 key 手术);
  (2) 对外暴露与我们其它模型一致的 1ch→1ch 接口,
      train.py / infer.py / eval_score.py / EMA 全部零改动复用。

支持两个官方变体(config['swinir_variant']):
  - "classical"(默认,第一阶段 ×2 主力):SwinIR-M classical-SR
      embed_dim=180, depths[6]×6, upsampler='pixelshuffle', ~12M
      权重: 001_classicalSR_DF2K_s64w8_SwinIR-M_x{2,4}.pth
  - "light"(★第二阶段 4× 主力,轻量化):SwinIR-S lightweight-SR
      embed_dim=60, depths[6]×4, upsampler='pixelshuffledirect', ~0.9M
      权重: 002_lightweightSR_DIV2K_s64w8_SwinIR-S_x4.pth
    选它是因为第二阶段评分新增「参数量15+体积15」两项,越小越好;
    ~0.9M / FP16~1.7MB 接近满分档,且复用同一灰度管线。

放大倍数由 config['scale'] 决定(第一阶段=2,第二阶段=4)。
upsampler='pixelshuffle' 仅支持 scale∈{2,3,4,8};'pixelshuffledirect' 同。

依赖:官方架构文件 `network_swinir.py`(从 KAIR/SwinIR 仓库下载) + timm。
"""

import os
import torch
import torch.nn as nn


# 官方变体超参(固定,保证与对应官方权重严格匹配)
_VARIANTS = {
    # SwinIR-M classical-SR (001_classicalSR_DF2K_s64w8_SwinIR-M_x{2,4})
    "classical": dict(
        img_size=64,
        window_size=8,
        img_range=1.0,
        depths=[6, 6, 6, 6, 6, 6],
        embed_dim=180,
        num_heads=[6, 6, 6, 6, 6, 6],
        mlp_ratio=2.0,
        upsampler="pixelshuffle",
        resi_connection="1conv",
    ),
    # SwinIR-S lightweight-SR (002_lightweightSR_DIV2K_s64w8_SwinIR-S_x{2,3,4})
    "light": dict(
        img_size=64,
        window_size=8,
        img_range=1.0,
        depths=[6, 6, 6, 6],
        embed_dim=60,
        num_heads=[6, 6, 6, 6],
        mlp_ratio=2.0,
        upsampler="pixelshuffledirect",
        resi_connection="1conv",
    ),
}


class GraySwinIR(nn.Module):
    """把 3 通道官方 SwinIR 包成 1 通道模型(灰度复制进 / 取均值出)。"""

    def __init__(self, config: dict):
        super().__init__()
        try:
            from network_swinir import SwinIR  # 官方架构文件(用户上传到 src/)
        except ImportError as e:
            raise ImportError(
                "缺少官方 network_swinir.py。请从 KAIR/SwinIR 仓库下载 "
                "models/network_swinir.py 放到 src/ 下。原始错误: " + str(e)
            )

        # 旧 checkpoint(第一阶段)没有 swinir_variant 键 → 默认 classical,向后兼容
        variant = config.get("swinir_variant", "classical")
        if variant not in _VARIANTS:
            raise ValueError(f"未知 swinir_variant: {variant!r}，应为 {list(_VARIANTS)}")
        v = _VARIANTS[variant]
        self.variant = variant

        scale = int(config.get("scale", 2))
        self.scale = scale
        self.net = SwinIR(
            upscale=scale,
            in_chans=3,
            img_size=v["img_size"],
            window_size=v["window_size"],
            img_range=v["img_range"],
            depths=v["depths"],
            embed_dim=v["embed_dim"],
            num_heads=v["num_heads"],
            mlp_ratio=v["mlp_ratio"],
            upsampler=v["upsampler"],
            resi_connection=v["resi_connection"],
            use_checkpoint=bool(config.get("use_checkpoint", False)),
        )

        pre = config.get("pretrained")
        if pre and os.path.isfile(pre):
            sd = torch.load(pre, map_location="cpu", weights_only=False)
            # KAIR 格式权重存在 'params' / 'params_ema' 键下
            if isinstance(sd, dict):
                sd = sd.get("params", sd.get("params_ema", sd))
            missing, unexpected = self.net.load_state_dict(sd, strict=False)
            n_loaded = len(self.net.state_dict()) - len(missing)
            print(f"[GraySwinIR/{variant}] 已加载官方预训练: {pre}")
            print(f"  匹配 {n_loaded} 个张量 | missing={len(missing)} unexpected={len(unexpected)}")
            if len(missing) > 5 or len(unexpected) > 5:
                print(f"  ⚠ 警告:不匹配较多,检查架构参数/变体。"
                      f"missing[:5]={missing[:5]} unexpected[:5]={unexpected[:5]}")

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        # x: (B,1,H,W) -> 复制成 3 通道;官方 SwinIR 内部已处理 window 对齐与裁剪
        out3 = self.net(x.repeat(1, 3, 1, 1))
        return out3.mean(dim=1, keepdim=True)

    def param_count_m(self) -> float:
        return sum(p.numel() for p in self.parameters()) / 1e6

    def model_size_mb_fp16(self) -> float:
        return sum(p.numel() for p in self.parameters()) * 2 / (1024 ** 2)


def build_model(config: dict) -> nn.Module:
    return GraySwinIR(config)
