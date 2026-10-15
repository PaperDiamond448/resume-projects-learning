"""
Ensemble inference: average N models, each with the full 8-way geometric
self-ensemble (TTA).  Predictions per image = 8 * num_models.

Usage:
    python infer_ensemble.py \
        --input_dir  /path/to/input_320 \
        --output_dir /path/to/results \
        --weights    /path/to/hat.pth /path/to/swinir.pth /path/to/rcan.pth
"""

import argparse
import os
import sys
import time
from pathlib import Path

import torch
import torch.nn.functional as F

sys.path.insert(0, str(Path(__file__).resolve().parent))
from models import build_model
from ckpt_utils import load_params
from infer import to_tensor, to_pil, _tta_transforms
from PIL import Image


def load_model(weights_path: str, device: torch.device):
    ckpt = torch.load(weights_path, map_location=device, weights_only=False)
    config = ckpt["config"]
    config = dict(config)
    config["pretrained"] = None   # 推理不再加载官方预训练(权重来自微调 ckpt)
    model = build_model(config)
    load_params(model, ckpt["model"])  # 容忍瘦身权重缺确定性 buffer
    return model.to(device).eval()


@torch.no_grad()
def infer_ensemble(models, img: Image.Image, device: torch.device,
                   out_h: int = 512, out_w: int = 640,
                   model_weights=None) -> Image.Image:
    """各模型 TTA×8 平均后，再按 model_weights 加权融合。

    model_weights=None → 等权(原行为)。不等强模型(如 HAT-L + 较弱 SwinIR)用
    加权可让强者主导、弱者只贡献小幅解相关多样性，规避 Run 7c/8c 等权集成净亏。
    """
    lr = to_tensor(img).to(device)

    if model_weights is None:
        model_weights = [1.0] * len(models)
    wsum = float(sum(model_weights))

    sr = None
    for model, w in zip(models, model_weights):
        preds = [inv(model(fwd(lr))) for fwd, inv in _tta_transforms()]
        m_sr = torch.stack(preds).mean(dim=0)          # 该模型 TTA×8 均值
        sr = m_sr * w if sr is None else sr + m_sr * w
    sr = sr / wsum

    _, _, oh, ow = sr.shape
    if (oh, ow) != (out_h, out_w):
        sr = F.interpolate(sr, size=(out_h, out_w), mode="bilinear", align_corners=False)
    return to_pil(sr)


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("--input_dir",  required=True)
    p.add_argument("--output_dir", required=True)
    p.add_argument("--weights", nargs="+", required=True,
                   help="One or more model weight files (arch auto-detected)")
    p.add_argument("--model_weights", type=float, nargs="+", default=None,
                   help="Per-model fusion weights (same count as --weights). "
                        "Default equal. Use e.g. 0.7 0.3 to let a stronger model dominate.")
    p.add_argument("--out_h", type=int, default=512)
    p.add_argument("--out_w", type=int, default=640)
    return p.parse_args()


def main():
    args = parse_args()
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"Device: {device}")

    models = []
    for w in args.weights:
        m = load_model(w, device)
        print(f"Loaded {w}  ({sum(p.numel() for p in m.parameters())/1e6:.2f}M params)")
        models.append(m)

    mw = args.model_weights
    if mw is not None:
        assert len(mw) == len(models), "--model_weights 数量必须等于 --weights"
        print(f"Fusion weights: {mw}")

    os.makedirs(args.output_dir, exist_ok=True)
    files = sorted(f for f in os.listdir(args.input_dir) if f.lower().endswith(".png"))
    print(f"\n{len(files)} images x {len(models)} models x 8 TTA "
          f"= {len(files) * len(models) * 8} forward passes")

    t0 = time.time()
    for i, fname in enumerate(files):
        img = Image.open(os.path.join(args.input_dir, fname))
        sr = infer_ensemble(models, img, device, args.out_h, args.out_w, model_weights=mw)
        sr.save(os.path.join(args.output_dir, fname))

        if (i + 1) % 20 == 0 or i == 0:
            avg = (time.time() - t0) / (i + 1)
            print(f"  [{i+1:3d}/{len(files)}]  {avg:.2f}s/img  "
                  f"~{avg * (len(files) - i - 1):.0f}s remaining")

    total = time.time() - t0
    print(f"\nDone!  {len(files)} images in {total:.1f}s  ({total/len(files):.2f}s avg)")
    print(f"Results: {args.output_dir}")


if __name__ == "__main__":
    main()
