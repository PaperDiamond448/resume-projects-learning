"""
Evaluation metrics matching the competition's auto-scoring system.

Competition scoring (Stage 1):
    Score = 30·s_PSNR + 30·s_SSIM + 20·s_LPIPS + 20·s_Edge

This module computes the raw metric values; normalisation to s_* scores
is done by the official platform.  We expose the raw values plus a
convenience function to evaluate a whole results directory.
"""

import os
from typing import Dict, Optional

import numpy as np
from PIL import Image
import torch
import torch.nn.functional as F


# ---------------------------------------------------------------------------
# Core metrics
# ---------------------------------------------------------------------------

def compute_psnr(
    img1: torch.Tensor,
    img2: torch.Tensor,
    max_val: float = 1.0,
) -> float:
    """
    PSNR in dB between two image tensors [..., H, W] in [0, 1].
    Higher is better.
    """
    mse = ((img1.float() - img2.float()) ** 2).mean()
    if mse == 0:
        return float("inf")
    return (10.0 * torch.log10(torch.tensor(max_val ** 2) / mse)).item()


def _gauss_kernel(size: int, sigma: float, device) -> torch.Tensor:
    coords = torch.arange(size, dtype=torch.float32, device=device) - size // 2
    g = torch.exp(-(coords ** 2) / (2 * sigma ** 2))
    g /= g.sum()
    return g.outer(g)


def compute_ssim(
    img1: torch.Tensor,
    img2: torch.Tensor,
    window_size: int = 11,
    sigma: float = 1.5,
    data_range: float = 1.0,
) -> float:
    """
    SSIM between two tensors.  Accepts [H, W], [C, H, W], or [B, C, H, W].
    Higher is better, max = 1.
    """
    img1, img2 = img1.float(), img2.float()
    if img1.dim() == 2:
        img1 = img1.unsqueeze(0).unsqueeze(0)
        img2 = img2.unsqueeze(0).unsqueeze(0)
    elif img1.dim() == 3:
        img1 = img1.unsqueeze(0)
        img2 = img2.unsqueeze(0)

    B, C, H, W = img1.shape
    C1 = (0.01 * data_range) ** 2
    C2 = (0.03 * data_range) ** 2

    kernel = _gauss_kernel(window_size, sigma, img1.device)
    kernel = kernel.unsqueeze(0).unsqueeze(0).expand(C, 1, window_size, window_size)
    pad = window_size // 2

    mu1 = F.conv2d(img1, kernel, padding=pad, groups=C)
    mu2 = F.conv2d(img2, kernel, padding=pad, groups=C)
    mu1_sq, mu2_sq, mu12 = mu1 ** 2, mu2 ** 2, mu1 * mu2

    sig1_sq = F.conv2d(img1 * img1, kernel, padding=pad, groups=C) - mu1_sq
    sig2_sq = F.conv2d(img2 * img2, kernel, padding=pad, groups=C) - mu2_sq
    sig12   = F.conv2d(img1 * img2, kernel, padding=pad, groups=C) - mu12

    num = (2 * mu12 + C1) * (2 * sig12 + C2)
    den = (mu1_sq + mu2_sq + C1) * (sig1_sq + sig2_sq + C2)
    return (num / (den + 1e-8)).mean().item()


def compute_edge_ncc(
    img1: torch.Tensor,
    img2: torch.Tensor,
) -> float:
    """
    Edge Retention: Sobel edge extraction followed by Normalised
    Cross-Correlation (NCC).  Matches the competition's definition.
    Higher is better, max ≈ 1.
    """
    img1, img2 = img1.float(), img2.float()
    if img1.dim() < 4:
        img1 = img1.view(1, 1, *img1.shape[-2:])
        img2 = img2.view(1, 1, *img2.shape[-2:])

    def _sobel(x: torch.Tensor) -> torch.Tensor:
        B, C, H, W = x.shape
        sx = torch.tensor([[-1, 0, 1], [-2, 0, 2], [-1, 0, 1]],
                          dtype=torch.float32, device=x.device).view(1, 1, 3, 3)
        sy = torch.tensor([[-1, -2, -1], [0, 0, 0], [1, 2, 1]],
                          dtype=torch.float32, device=x.device).view(1, 1, 3, 3)
        xf = x.view(B * C, 1, H, W)
        mag = torch.sqrt(
            F.conv2d(xf, sx, padding=1) ** 2
            + F.conv2d(xf, sy, padding=1) ** 2
            + 1e-8
        )
        return mag.view(B, C, H, W)

    e1 = _sobel(img1).flatten()
    e2 = _sobel(img2).flatten()

    e1 = e1 - e1.mean()
    e2 = e2 - e2.mean()

    ncc = (e1 * e2).sum() / (
        torch.sqrt((e1 ** 2).sum() * (e2 ** 2).sum()) + 1e-8
    )
    return ncc.item()


def compute_lpips_approx(
    img1: torch.Tensor,
    img2: torch.Tensor,
) -> float:
    """
    Lightweight LPIPS approximation using multi-scale gradient differences.
    Used for local monitoring only (official LPIPS uses VGG features).
    Lower is better.
    """
    img1, img2 = img1.float(), img2.float()
    if img1.dim() == 2:
        img1 = img1.view(1, 1, *img1.shape)
        img2 = img2.view(1, 1, *img2.shape)
    elif img1.dim() == 3:
        img1 = img1.unsqueeze(0)
        img2 = img2.unsqueeze(0)

    sx = torch.tensor([[-1, 0, 1], [-2, 0, 2], [-1, 0, 1]],
                      dtype=torch.float32, device=img1.device).view(1, 1, 3, 3)
    sy = torch.tensor([[-1, -2, -1], [0, 0, 0], [1, 2, 1]],
                      dtype=torch.float32, device=img1.device).view(1, 1, 3, 3)

    loss = 0.0
    for scale in [1, 2, 4]:
        if scale > 1:
            i1 = F.avg_pool2d(img1, scale)
            i2 = F.avg_pool2d(img2, scale)
        else:
            i1, i2 = img1, img2

        B, C, H, W = i1.shape
        xf1 = i1.view(B * C, 1, H, W)
        xf2 = i2.view(B * C, 1, H, W)

        g1x = F.conv2d(xf1, sx, padding=1)
        g1y = F.conv2d(xf1, sy, padding=1)
        g2x = F.conv2d(xf2, sx, padding=1)
        g2y = F.conv2d(xf2, sy, padding=1)

        loss += ((g1x - g2x).abs() + (g1y - g2y).abs()).mean().item()

    return loss / 3.0


# ---------------------------------------------------------------------------
# Directory-level evaluation
# ---------------------------------------------------------------------------

def evaluate_directory(
    sr_dir: str,
    hr_dir: str,
    device: str = "cpu",
    verbose: bool = True,
) -> Dict[str, float]:
    """
    Compute average PSNR / SSIM / Edge-NCC / LPIPS-approx for all images
    in sr_dir against corresponding images in hr_dir.

    Returns a dict with keys: PSNR, SSIM, Edge_NCC, LPIPS_approx, n_images.
    """
    sr_files = sorted(f for f in os.listdir(sr_dir) if f.lower().endswith(".png"))

    psnrs, ssims, edges, lpips_vals = [], [], [], []

    for fname in sr_files:
        sr_path = os.path.join(sr_dir, fname)
        hr_path = os.path.join(hr_dir, fname)
        if not os.path.exists(hr_path):
            if verbose:
                print(f"  [skip] HR not found: {hr_path}")
            continue

        sr = torch.from_numpy(np.array(Image.open(sr_path).convert("L"),
                                        dtype=np.float32)).unsqueeze(0) / 255.0
        hr = torch.from_numpy(np.array(Image.open(hr_path).convert("L"),
                                        dtype=np.float32)).unsqueeze(0) / 255.0

        psnrs.append(compute_psnr(sr, hr))
        ssims.append(compute_ssim(sr, hr))
        edges.append(compute_edge_ncc(sr, hr))
        lpips_vals.append(compute_lpips_approx(sr, hr))

    results = {
        "PSNR": float(np.mean(psnrs)),
        "SSIM": float(np.mean(ssims)),
        "Edge_NCC": float(np.mean(edges)),
        "LPIPS_approx": float(np.mean(lpips_vals)),
        "n_images": len(psnrs),
    }

    if verbose:
        print(f"  Evaluated {results['n_images']} images")
        print(f"  PSNR:         {results['PSNR']:.4f} dB")
        print(f"  SSIM:         {results['SSIM']:.4f}")
        print(f"  Edge NCC:     {results['Edge_NCC']:.4f}")
        print(f"  LPIPS approx: {results['LPIPS_approx']:.4f} (lower is better)")

    return results


if __name__ == "__main__":
    # Quick sanity check: perfect SR → PSNR=inf, SSIM=1, Edge≈1
    x = torch.rand(1, 512, 640)
    print("PSNR (same):", compute_psnr(x, x))
    print("SSIM (same):", compute_ssim(x, x))
    print("Edge (same):", compute_edge_ncc(x, x))
