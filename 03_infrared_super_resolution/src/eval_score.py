"""
Local competition-style scorer with REAL LPIPS.

The platform normalises each metric across all submissions, then applies the
stage's image-quality formula:
    Stage 1 (×2):  Score = 30*s_PSNR + 30*s_SSIM + 20*s_LPIPS + 20*s_Edge
    Stage 2 (×4):  quality part = 28*s_PSNR + 22*s_SSIM + 10*s_LPIPS + 10*s_Edge
                   (+ 15*s_Params + 15*s_Size — NOT image-based, see note below)
We cannot see other teams, but we CAN rank our own candidate submissions the
same way: min-max normalise each metric across the candidates (plus a bicubic
anchor for a meaningful floor) and apply the official weights.  This tells you
which of your candidate result folders to actually submit.

⚠️ 第二阶段的「参数量 15 + 体积 15」是模型级常量,不随单张图片变化:
   - 比较**同一模型**的不同候选(EMA vs raw / 不同 epoch / TTA 配置)时,params/size
     相同,不影响排序 → 直接用 --stage 2 的质量权重选版即可。
   - 比较**不同大小模型**(light vs classical 找帕累托点)时,需另把 15·s_Params +
     15·s_Size 手动叠加(params/size 是已知常量,见作战规划速查表)。

Usage(第二阶段 ×4):
    python src/eval_score.py --stage 2 --scale 4 \
        --hr_dir  超分竞赛数据集/训练数据集/target_640 \
        --lr_dir  超分竞赛数据集/训练数据集/input_160 \
        --sr_dirs results/light_ema results/light_raw \
        --n_val 90

Each --sr_dirs entry is a folder of 640x512 PNGs named like the HR files.
"""

import argparse
import os
import sys
from pathlib import Path

import numpy as np
from PIL import Image
import torch

sys.path.insert(0, str(Path(__file__).resolve().parent))
from metrics import compute_psnr, compute_ssim, compute_edge_ncc


def _load(path):
    return torch.from_numpy(
        np.array(Image.open(path).convert("L"), dtype=np.float32)
    ).unsqueeze(0) / 255.0


def _bicubic(lr_path, scale=4):
    img = Image.open(lr_path).convert("L")
    w, h = img.size
    big = img.resize((w * scale, h * scale), Image.BICUBIC)
    return torch.from_numpy(np.array(big, dtype=np.float32)).unsqueeze(0) / 255.0


def _make_lpips(device):
    try:
        import lpips
        net = lpips.LPIPS(net="alex", verbose=False).to(device).eval()
        return net
    except Exception as e:
        print(f"[warn] real LPIPS unavailable ({e}); LPIPS column will be NaN")
        return None


@torch.no_grad()
def _lpips_val(net, sr, hr, device):
    if net is None:
        return float("nan")
    s = sr.unsqueeze(0).repeat(1, 3, 1, 1).to(device) * 2 - 1
    h = hr.unsqueeze(0).repeat(1, 3, 1, 1).to(device) * 2 - 1
    return net(s, h).item()


def evaluate(get_sr, hr_dir, files, lpips_net, device, label):
    P, S, E, L = [], [], [], []
    for f in files:
        hr = _load(os.path.join(hr_dir, f))
        sr = get_sr(f)
        if sr.shape != hr.shape:
            sr = torch.nn.functional.interpolate(
                sr.unsqueeze(0), size=hr.shape[-2:], mode="bilinear", align_corners=False
            ).squeeze(0)
        P.append(compute_psnr(sr, hr))
        S.append(compute_ssim(sr, hr))
        E.append(compute_edge_ncc(sr, hr))
        L.append(_lpips_val(lpips_net, sr, hr, device))
    r = {"PSNR": float(np.mean(P)), "SSIM": float(np.mean(S)),
         "Edge": float(np.mean(E)), "LPIPS": float(np.nanmean(L))}
    print(f"  [{label:24s}] PSNR={r['PSNR']:.4f}  SSIM={r['SSIM']:.4f}  "
          f"Edge={r['Edge']:.4f}  LPIPS={r['LPIPS']:.4f}")
    return r


# 各阶段图像质量项权重(参数量/体积是模型级常量,不在逐图打分里)
_QUALITY_WEIGHTS = {
    1: dict(PSNR=30, SSIM=30, LPIPS=20, Edge=20),   # 一阶段 ×2
    2: dict(PSNR=28, SSIM=22, LPIPS=10, Edge=10),   # 二阶段 ×4 质量部分
}


def normalized_scores(results: dict, stage: int = 2):
    """results: {label: {PSNR,SSIM,Edge,LPIPS}} -> {label: weighted_score}."""
    w = _QUALITY_WEIGHTS[stage]
    labels = list(results)
    def col(metric, lower_better=False):
        vals = np.array([results[l][metric] for l in labels], dtype=np.float64)
        if lower_better:
            vals = -vals
        lo, hi = np.nanmin(vals), np.nanmax(vals)
        if hi - lo < 1e-12:
            return {l: 1.0 for l in labels}
        return {l: float((vals[i] - lo) / (hi - lo)) for i, l in enumerate(labels)}

    sP = col("PSNR")
    sS = col("SSIM")
    sE = col("Edge")
    sL = col("LPIPS", lower_better=True)
    return {l: w["PSNR"] * sP[l] + w["SSIM"] * sS[l]
               + w["LPIPS"] * sL[l] + w["Edge"] * sE[l] for l in labels}


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--hr_dir", required=True)
    p.add_argument("--sr_dirs", nargs="+", required=True,
                   help="One or more folders of result PNGs to compare")
    p.add_argument("--lr_dir", default=None,
                   help="If given, adds a bicubic anchor for normalisation floor")
    p.add_argument("--n_val", type=int, default=180,
                   help="Use the first N sorted HR files (match your training val split)")
    p.add_argument("--stage", type=int, default=2, choices=[1, 2],
                   help="Quality weights: 1=30/30/20/20 (×2), 2=28/22/10/10 (×4, default)")
    p.add_argument("--scale", type=int, default=4,
                   help="SR scale for the bicubic anchor (2 for stage 1, 4 for stage 2)")
    args = p.parse_args()

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    lpips_net = _make_lpips(device)

    files = sorted(f for f in os.listdir(args.hr_dir) if f.lower().endswith(".png"))[: args.n_val]
    print(f"Scoring {len(files)} images (real LPIPS: {lpips_net is not None})\n")

    results = {}
    if args.lr_dir:
        results["bicubic(anchor)"] = evaluate(
            lambda f: _bicubic(os.path.join(args.lr_dir, f), scale=args.scale),
            args.hr_dir, files, lpips_net, device, "bicubic(anchor)")

    for d in args.sr_dirs:
        label = os.path.basename(os.path.normpath(d))
        results[label] = evaluate(
            lambda f, d=d: _load(os.path.join(d, f)),
            args.hr_dir, files, lpips_net, device, label)

    w = _QUALITY_WEIGHTS[args.stage]
    print(f"\nNormalised competition-style score (stage {args.stage} quality weights "
          f"{w['PSNR']}/{w['SSIM']}/{w['LPIPS']}/{w['Edge']}, across these candidates):")
    if args.stage == 2:
        print("  注:未含「参数量15+体积15」——同模型选版无影响;跨模型比大小需另叠加。")
    scores = normalized_scores(results, stage=args.stage)
    for l in sorted(scores, key=scores.get, reverse=True):
        tag = "  <-- best" if l == max(scores, key=scores.get) else ""
        print(f"  {l:24s} {scores[l]:6.2f}{tag}")
    print("\nNote: absolute numbers are only meaningful relative to each other "
          "(the real field is unknown). Submit the candidate that ranks highest here.")


if __name__ == "__main__":
    main()
