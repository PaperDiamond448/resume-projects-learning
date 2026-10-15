"""
Inference script — generates 640×512 super-resolved images from 320×256 inputs.

Stage 1 usage:
    python src/infer.py \
        --input_dir  /path/to/test_input_320 \
        --output_dir ./results/preliminary \
        --weights    ./checkpoints/best_model.pth

The script supports:
  • Test-time augmentation (TTA): averages 4 flipped predictions (+~0.1 dB)
  • Automatic FP16 ↔ FP32 handling
  • CPU or CUDA inference
"""

import argparse
import os
import sys
import time
from pathlib import Path

import numpy as np
from PIL import Image
import torch
import torch.nn.functional as F

sys.path.insert(0, str(Path(__file__).resolve().parent))
from models import build_model
from ckpt_utils import load_params, robust_load


# ---------------------------------------------------------------------------
# Model loading
# ---------------------------------------------------------------------------

def load_model(weights_path: str, device: torch.device):
    """
    Load a competition-format checkpoint (FP16 weights) and return a
    FP32 model ready for inference.
    """
    ckpt = robust_load(weights_path, map_location=device)
    config = dict(ckpt["config"])
    config["pretrained"] = None   # 推理权重来自微调 ckpt，不必再载官方预训练
    model = build_model(config)

    # Checkpoint stores FP16 params; load_params casts to FP32, tolerates
    # stripped deterministic buffers (regenerated in __init__), and asserts
    # every learnable parameter is present.
    load_params(model, ckpt["model"])
    model = model.to(device).eval()
    return model


# ---------------------------------------------------------------------------
# Single-image inference
# ---------------------------------------------------------------------------

def to_tensor(img: Image.Image) -> torch.Tensor:
    """PIL grayscale image → [1, 1, H, W] float32 tensor in [0, 1]."""
    arr = np.array(img.convert("L"), dtype=np.float32) / 255.0
    return torch.from_numpy(arr).unsqueeze(0).unsqueeze(0)


def to_pil(tensor: torch.Tensor) -> Image.Image:
    """[1, 1, H, W] or [1, H, W] tensor → uint8 PIL grayscale image."""
    arr = tensor.squeeze().clamp(0.0, 1.0).cpu().float().numpy()
    arr = (arr * 255.0).round().astype(np.uint8)
    return Image.fromarray(arr, mode="L")


def _tta_transforms():
    """The 8 geometric self-ensemble ops (D4 group) as (forward, inverse) pairs."""
    def ident(x): return x
    def hflip(x): return torch.flip(x, [-1])
    def vflip(x): return torch.flip(x, [-2])
    def hvflip(x): return torch.flip(x, [-1, -2])
    def transpose(x): return x.transpose(-1, -2)
    def t_h(x): return torch.flip(x.transpose(-1, -2), [-1])
    def t_v(x): return torch.flip(x.transpose(-1, -2), [-2])
    def t_hv(x): return torch.flip(x.transpose(-1, -2), [-1, -2])
    # each op is its own inverse except the transpose-then-flip ones,
    # so we pair forward with the exact undo
    return [
        (ident, ident),
        (hflip, hflip),
        (vflip, vflip),
        (hvflip, hvflip),
        (transpose, transpose),
        (t_h, lambda x: torch.flip(x, [-1]).transpose(-1, -2)),
        (t_v, lambda x: torch.flip(x, [-2]).transpose(-1, -2)),
        (t_hv, lambda x: torch.flip(x, [-1, -2]).transpose(-1, -2)),
    ]


@torch.no_grad()
def infer_image(
    model,
    img: Image.Image,
    device: torch.device,
    tta: bool = True,
    expected_out_h: int = 512,
    expected_out_w: int = 640,
) -> Image.Image:
    """
    Run super-resolution on a single PIL image.

    With tta=True, applies the full 8-way geometric self-ensemble (D4 group:
    4 flips x {identity, transpose}) and averages the de-augmented predictions.
    """
    lr = to_tensor(img).to(device)

    if tta:
        preds = []
        for fwd, inv in _tta_transforms():
            preds.append(inv(model(fwd(lr))))
        sr = torch.stack(preds).mean(dim=0)
    else:
        sr = model(lr)

    # Enforce exact output resolution (safeguard against rounding)
    _, _, oh, ow = sr.shape
    if (oh, ow) != (expected_out_h, expected_out_w):
        sr = F.interpolate(
            sr, size=(expected_out_h, expected_out_w),
            mode="bilinear", align_corners=False,
        )

    return to_pil(sr)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="RCAN inference for infrared SR")
    p.add_argument("--input_dir",  type=str, required=True,
                   help="Directory containing input LR PNG images")
    p.add_argument("--output_dir", type=str, required=True,
                   help="Directory to write super-resolved PNG images")
    p.add_argument("--weights",    type=str, required=True,
                   help="Path to best_model.pth")
    p.add_argument("--tta",        action="store_true", default=True,
                   help="Enable test-time augmentation (default: on)")
    p.add_argument("--no_tta",     dest="tta", action="store_false",
                   help="Disable TTA")
    p.add_argument("--out_h", type=int, default=512,
                   help="Expected output height (default 512)")
    p.add_argument("--out_w", type=int, default=640,
                   help="Expected output width (default 640)")
    return p.parse_args()


def main():
    args = parse_args()

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"Device:  {device}")
    print(f"TTA:     {args.tta}")
    print(f"Weights: {args.weights}")

    model = load_model(args.weights, device)
    print(f"Model loaded  ({sum(p.numel() for p in model.parameters())/1e6:.2f}M params)")

    os.makedirs(args.output_dir, exist_ok=True)

    input_files = sorted(
        f for f in os.listdir(args.input_dir) if f.lower().endswith(".png")
    )
    if not input_files:
        print(f"No PNG files found in {args.input_dir}")
        return

    print(f"\nProcessing {len(input_files)} images → {args.output_dir}")
    t_start = time.time()

    for idx, fname in enumerate(input_files):
        in_path = os.path.join(args.input_dir, fname)
        out_path = os.path.join(args.output_dir, fname)

        img = Image.open(in_path)
        sr = infer_image(
            model, img, device,
            tta=args.tta,
            expected_out_h=args.out_h,
            expected_out_w=args.out_w,
        )
        sr.save(out_path)

        if (idx + 1) % 50 == 0 or idx == 0:
            elapsed = time.time() - t_start
            avg = elapsed / (idx + 1)
            remaining = avg * (len(input_files) - idx - 1)
            print(f"  [{idx+1:4d}/{len(input_files)}]  "
                  f"{avg:.2f}s/img  ~{remaining:.0f}s remaining")

    total = time.time() - t_start
    print(f"\nDone!  {len(input_files)} images in {total:.1f}s  "
          f"({total/len(input_files):.2f}s avg)")
    print(f"Results: {args.output_dir}")


if __name__ == "__main__":
    main()
