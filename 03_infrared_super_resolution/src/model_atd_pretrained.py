"""
官方预训练 ATD-light(CVPR 2024, Adaptive Token Dictionary) → 灰度红外迁移 的包装器。

ATD-light 是当前轻量组 SOTA(打败 SwinIR-light / SRFormer-light / OmniSR),
参数约 0.78M(比 HiT-SRF 0.87M 更小),Urban100 ×4 约 26.97dB(比 HiT-SRF ~26.85 更高),
并提供与本赛题一致的 DIV2K 标准 bicubic ×4 预训练。第二阶段「参数量+体积」评分下,
ATD-light 是又一"更小+更强"双赢升级。

本包装器把官方 3 通道 ATD 当 1 通道模型用(灰度复制成 3 通道喂进、输出取均值还原),
从而直接加载官方权重、并复用 train/infer/eval/EMA/打包脚本全套(零改动)。

依赖:官方架构文件 `atd_arch.py`(已放入 src/) + timm。
arch 顶部 `from timm.layers import ...` 与 `basicsr.utils.registry` 由
`_compat_shims.install()` 兜底(缺 timm.layers 时别名 timm.models.layers;
basicsr 注册器注入零依赖桩),保证提交不引入新依赖。

ATD-light ×4 固定超参(与 103_ATD_light_SRx4_finetune(v2).pth 严格对齐,strict 加载 missing=0):
  embed_dim=48, depths=[6]*4, num_heads=[3]*4, window_size=16, dim_ffn_td=4,
  category_size=128, num_tokens=128, reducted_dim=12, convffn_kernel_size=7,
  mlp_ratio=1.0, upsampler='pixelshuffledirect', resi_connection='1conv',
  img_size=64, img_range=1.0。权重存于 'params_ema' 键。
"""

import os
import sys
from pathlib import Path

import torch
import torch.nn as nn

# 必须在 import atd_arch 之前安装兼容垫片(它顶部 import timm.layers / basicsr.utils.registry)
sys.path.insert(0, str(Path(__file__).resolve().parent))
try:
    import _compat_shims
    _compat_shims.install()
except Exception:
    pass


# ATD-light 固定超参(仅 upscale 随阶段变)
_ATD_LIGHT = dict(
    img_size=64,
    in_chans=3,
    embed_dim=48,
    depths=[6, 6, 6, 6],
    num_heads=[3, 3, 3, 3],
    window_size=16,
    dim_ffn_td=4,
    category_size=128,
    num_tokens=128,
    reducted_dim=12,
    convffn_kernel_size=7,
    mlp_ratio=1.0,
    upsampler="pixelshuffledirect",
    resi_connection="1conv",
    img_range=1.0,
)


class GrayATD(nn.Module):
    """把 3 通道官方 ATD 包成 1 通道模型(灰度复制进 / 取均值出)。"""

    def __init__(self, config: dict):
        super().__init__()
        try:
            from atd_arch import ATD  # 官方架构文件(放入 src/)
        except ImportError as e:
            raise ImportError(
                "缺少官方 atd_arch.py。请从 LabShuHangGU/Adaptive-Token-Dictionary 仓库下载 "
                "basicsr/archs/atd_arch.py 放到 src/ 下。原始错误: " + str(e)
            )

        scale = int(config.get("scale", 4))
        self.scale = scale
        self.net = ATD(
            upscale=scale,
            use_checkpoint=bool(config.get("use_checkpoint", False)),
            **_ATD_LIGHT,
        )

        pre = config.get("pretrained")
        if pre and os.path.isfile(pre):
            sd = torch.load(pre, map_location="cpu", weights_only=False)
            # ATD 官方权重存于 'params_ema'(优先) / 'params'
            if isinstance(sd, dict):
                sd = sd.get("params_ema", sd.get("params", sd))
            missing, unexpected = self.net.load_state_dict(sd, strict=False)
            n_loaded = len(self.net.state_dict()) - len(missing)
            print(f"[GrayATD] 已加载官方预训练: {pre}")
            print(f"  匹配 {n_loaded} 个张量 | missing={len(missing)} unexpected={len(unexpected)}")
            if len(missing) > 5 or len(unexpected) > 5:
                print(f"  ⚠ 警告:不匹配较多,检查架构参数/版本(须用 v2)。"
                      f"missing[:5]={missing[:5]} unexpected[:5]={unexpected[:5]}")

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        # x: (B,1,H,W) -> 复制成 3 通道;ATD 窗口=16,输入 H/W 需被 16 整除
        # (本赛题 160×128 及训练 patch 96 均满足)。
        out3 = self.net(x.repeat(1, 3, 1, 1))
        return out3.mean(dim=1, keepdim=True)

    def param_count_m(self) -> float:
        return sum(p.numel() for p in self.parameters()) / 1e6

    def model_size_mb_fp16(self) -> float:
        return sum(p.numel() for p in self.parameters()) * 2 / (1024 ** 2)


def build_model(config: dict) -> nn.Module:
    return GrayATD(config)
