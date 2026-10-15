"""
Quick local validation: computes PSNR / SSIM / Edge-NCC for a set of images.

Two modes:
  1. Bicubic baseline — no model needed, fast sanity check:
       python src/validate.py --mode bicubic \
           --lr_dir  超分竞赛数据集/训练数据集/input_320 \
           --hr_dir  超分竞赛数据集/训练数据集/target_640

  2. Model evaluation — run inference then evaluate:
       python src/validate.py --mode model \
           --lr_dir   超分竞赛数据集/训练数据集/input_320 \
           --hr_dir   超分竞赛数据集/训练数据集/target_640 \
           --weights  checkpoints/best_model.pth \
           --n_val    180
"""

import argparse
import os
import sys
from pathlib import Path

import numpy as np
from PIL import Image
import torch
import torch.nn.functional as F

sys.path.insert(0, str(Path(__file__).resolve().parent))
from metrics import compute_psnr, compute_ssim, compute_edge_ncc


def bicubic_sr(img: Image.Image, scale: int = 2) -> Image.Image:
    w, h = img.size
    return img.resize((w * scale, h * scale), Image.BICUBIC)


def model_sr(model, img: Image.Image, device) -> Image.Image:
    from infer import infer_image
    return infer_image(model, img, device, tta=False)


def evaluate_list(sr_fn, lr_dir, hr_dir, file_list, label):
    psnrs, ssims, edges = [], [], []
    for fname in file_list:
        lr = Image.open(os.path.join(lr_dir, fname)).convert("L")
        hr_img = Image.open(os.path.join(hr_dir, fname)).convert("L")

        sr_img = sr_fn(lr)

        sr_t = torch.from_numpy(np.array(sr_img, dtype=np.float32)).unsqueeze(0) / 255.0
        hr_t = torch.from_numpy(np.array(hr_img, dtype=np.float32)).unsqueeze(0) / 255.0

        psnrs.append(compute_psnr(sr_t, hr_t))
        ssims.append(compute_ssim(sr_t, hr_t))
        edges.append(compute_edge_ncc(sr_t, hr_t))

    print(f"\n[{label}]  n={len(file_list)}")
    print(f"  PSNR:     {np.mean(psnrs):.4f} dB  (std {np.std(psnrs):.4f})")
    print(f"  SSIM:     {np.mean(ssims):.4f}     (std {np.std(ssims):.4f})")
    print(f"  Edge NCC: {np.mean(edges):.4f}     (std {np.std(edges):.4f})")
    return {"PSNR": np.mean(psnrs), "SSIM": np.mean(ssims), "Edge": np.mean(edges)}


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--mode",    choices=["bicubic", "model"], default="bicubic")
    p.add_argument("--lr_dir",  type=str, required=True)
    p.add_argument("--hr_dir",  type=str, required=True)
    p.add_argument("--weights", type=str, default=None)
    p.add_argument("--n_val",   type=int, default=180,
                   help="Number of validation images to use (from the front of sorted list)")
    p.add_argument("--scale",   type=int, default=2)
    args = p.parse_args()

    all_files = sorted(f for f in os.listdir(args.lr_dir) if f.lower().endswith(".png"))
    val_files = all_files[: args.n_val]
    print(f"Validating on {len(val_files)} images ({args.mode})")

    if args.mode == "bicubic":
        evaluate_list(
            lambda img: bicubic_sr(img, args.scale),
            args.lr_dir, args.hr_dir, val_files,
            label="Bicubic baseline",
        )
    else:
        if not args.weights:
            print("--weights is required for --mode model")
            return
        device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
        from infer import load_model, infer_image
        model = load_model(args.weights, device)
        evaluate_list(
            lambda img: infer_image(model, img, device, tta=False),
            args.lr_dir, args.hr_dir, val_files,
            label="RCAN",
        )


if __name__ == "__main__":
    main()
