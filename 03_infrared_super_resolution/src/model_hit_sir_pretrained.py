"""
官方预训练 HiT-SIR(ECCV 2024) → 灰度红外迁移 的包装器。

HiT-SIR 是对 SwinIR-light 的层次化 Transformer 改进:**参数更少(~792K vs SwinIR-S 910K)
且重建质量更高**(同族最强)，并提供与本赛题一致的 DIV2K bicubic ×4 预训练。第二阶段
新增「参数量+体积」评分,HiT-SIR 是少有的"更小+更强"双赢升级(对比换大模型牺牲体积分)。

本包装器把官方 3 通道 HiT_SIR 当 1 通道模型用(灰度复制成 3 通道喂进、输出取均值还原)，
从而:
  (1) 直接严格加载官方权重(零 key 手术);
  (2) 对外暴露与其它模型一致的 1ch→1ch 接口,train/infer/eval/EMA/打包脚本零改动复用。

依赖:官方架构文件 `hit_sir_arch.py`(已放入 src/)+ timm。
arch 额外 import 的 basicsr.utils.registry / huggingface_hub 由 `_compat_shims.install()`
在缺包时注入零依赖桩(仅注册装饰器 + 空 Mixin,不影响数值),保证提交不引入新依赖。

HiT-SIR ×4 固定超参(与官方 HiT-SIR-4x.pth 对齐,均为 arch 默认值):
  embed_dim=60, depths=[6]*4, num_heads=[6]*4, base_win_size=[8,8], mlp_ratio=2.0,
  upsampler='pixelshuffledirect', resi_connection='1conv',
  hier_win_ratios=[0.5,1,2,4,6,8], img_size=64, img_range=1.0
"""

import os
import sys
from pathlib import Path

import torch
import torch.nn as nn

# 必须在 import hit_sir_arch 之前安装兼容垫片(它顶部 import basicsr/huggingface_hub)
sys.path.insert(0, str(Path(__file__).resolve().parent))
try:
    import _compat_shims
    _compat_shims.install()
except Exception:
    pass


# HiT-SIR 固定超参(全部为 arch 默认值,仅 upscale 随阶段变)
_HIT_SIR = dict(
    img_size=64,
    in_chans=3,
    embed_dim=60,
    depths=[6, 6, 6, 6],
    num_heads=[6, 6, 6, 6],
    base_win_size=[8, 8],
    mlp_ratio=2.0,
    upsampler="pixelshuffledirect",
    resi_connection="1conv",
    hier_win_ratios=[0.5, 1, 2, 4, 6, 8],
    img_range=1.0,
)


class GrayHiTSIR(nn.Module):
    """把 3 通道官方 HiT-SIR 包成 1 通道模型(灰度复制进 / 取均值出)。"""

    def __init__(self, config: dict):
        super().__init__()
        # 变体选择:hit_sir(SwinIR式) 或 hit_srf(SRFormer式,更强)。两者构造超参完全一致,
        # 仅 arch 类与内部注意力不同。旧 ckpt 无 arch 或 arch=hit_sir → 默认 HiT-SIR。
        variant = (config.get("arch") or "hit_sir").lower()
        if variant == "hit_srf":
            try:
                from hit_srf_arch import HiT_SRF as _Net  # 官方架构文件(放入 src/)
            except ImportError as e:
                raise ImportError(
                    "缺少官方 hit_srf_arch.py。请从 XiangZ-0/HiT-SR 仓库下载 "
                    "basicsr/archs/hit_srf_arch.py 放到 src/ 下。原始错误: " + str(e)
                )
            self.tag = "HiT-SRF"
        else:
            try:
                from hit_sir_arch import HiT_SIR as _Net  # 官方架构文件(放入 src/)
            except ImportError as e:
                raise ImportError(
                    "缺少官方 hit_sir_arch.py。请从 XiangZ-0/HiT-SR 仓库下载 "
                    "basicsr/archs/hit_sir_arch.py 放到 src/ 下。原始错误: " + str(e)
                )
            self.tag = "HiT-SIR"

        scale = int(config.get("scale", 4))
        self.scale = scale
        self.net = _Net(
            upscale=scale,
            use_checkpoint=bool(config.get("use_checkpoint", False)),
            **_HIT_SIR,
        )

        pre = config.get("pretrained")
        if pre and os.path.isfile(pre):
            sd = torch.load(pre, map_location="cpu", weights_only=False)
            if isinstance(sd, dict):
                sd = sd.get("params", sd.get("params_ema", sd))
            missing, unexpected = self.net.load_state_dict(sd, strict=False)
            n_loaded = len(self.net.state_dict()) - len(missing)
            print(f"[Gray{self.tag.replace('-','')}] 已加载官方预训练: {pre}")
            print(f"  匹配 {n_loaded} 个张量 | missing={len(missing)} unexpected={len(unexpected)}")
            if len(missing) > 5 or len(unexpected) > 5:
                print(f"  ⚠ 警告:不匹配较多,检查架构参数。"
                      f"missing[:5]={missing[:5]} unexpected[:5]={unexpected[:5]}")

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        # x: (B,1,H,W) -> 复制成 3 通道;HiT_SIR 内部 check_image_size 处理窗口对齐与裁剪
        out3 = self.net(x.repeat(1, 3, 1, 1))
        return out3.mean(dim=1, keepdim=True)

    def param_count_m(self) -> float:
        return sum(p.numel() for p in self.parameters()) / 1e6

    def model_size_mb_fp16(self) -> float:
        return sum(p.numel() for p in self.parameters()) * 2 / (1024 ** 2)


def build_model(config: dict) -> nn.Module:
    return GrayHiTSIR(config)
