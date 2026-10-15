"""
Checkpoint 存/取 共享工具(train / infer / infer_ensemble 复用)。

第二阶段评分新增「模型体积」15 分(按权重文件实际字节算)。SwinIR/HAT 的
`attn_mask`、`relative_position_index` 是**确定性 buffer**——它们在模型 __init__
里由 window_size / 分辨率确定性地算出,且真实推理分辨率(160×128 ≠ 训练 img_size 64)
下 SwinIR 会动态重算 mask,存盘的那份根本用不到。把它们从提交权重里剥掉:
  SwinIR-S light ×4 的 model.pth: ~8.0MB → ~1.8MB(白捡体积分,零数值影响)。

加载时用 strict=False 容许这些 buffer 缺失(模型 __init__ 已正确生成),
但**断言所有可学习参数都在**,保留对"真权重缺失/架构不匹配"的检错能力。
对仍带完整 buffer 的旧 checkpoint(第一阶段) 同样能加载(多余 buffer 直接匹配)。
"""
import torch

# 确定性、可由 __init__ 完整重建的 buffer 名(按 key 末段匹配)。剥掉不影响数值。
DETERMINISTIC_BUFFER_SUFFIXES = ("attn_mask", "relative_position_index")


def robust_load(path, map_location="cpu"):
    """
    版本兼容的 torch.load。

    `weights_only` 参数 PyTorch<1.13 不存在；≥2.6 默认翻成 True(会拒绝加载含
    config dict 的 ckpt)。评测环境写的是「PyTorch 1.9+」可能是任意版本,故:
      - 新版(支持该参数)显式传 weights_only=False 以加载我们的完整 ckpt;
      - 老版(不认该参数)回退到不带该参数的调用。
    """
    try:
        return torch.load(path, map_location=map_location, weights_only=False)
    except TypeError:
        return torch.load(path, map_location=map_location)


def strip_deterministic_buffers(state_dict: dict) -> dict:
    """返回剥掉确定性 buffer 后的 state_dict(仅用于提交存盘瘦身)。"""
    return {
        k: v for k, v in state_dict.items()
        if k.split(".")[-1] not in DETERMINISTIC_BUFFER_SUFFIXES
    }


def load_params(model, state_dict: dict, *, float_cast: bool = True):
    """
    容错加载:允许确定性 buffer 缺失(瘦身后的提交权重),但所有可学习参数必须齐全。

    - strict=False → 瘦身文件(无 attn_mask/relative_position_index)可加载,
      这些 buffer 由 model.__init__ 重新生成,数值一致。
    - 断言无缺失的**可学习参数** → 真权重缺失/架构不匹配仍会报错(不丢 strict 的安全性)。
    """
    if float_cast:
        state_dict = {k: v.float() for k, v in state_dict.items()}
    missing, unexpected = model.load_state_dict(state_dict, strict=False)
    param_names = {n for n, _ in model.named_parameters()}
    missing_params = [k for k in missing if k in param_names]
    if missing_params:
        raise RuntimeError(
            f"权重缺少 {len(missing_params)} 个可学习参数(非确定性 buffer): "
            f"{missing_params[:5]}{' ...' if len(missing_params) > 5 else ''}"
        )
    return missing, unexpected
