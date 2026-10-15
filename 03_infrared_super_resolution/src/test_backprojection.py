"""
决定性实测：在 Run 14 留出 val 图(0000-0089)上，比较
  (a) 网络原始输出
  (b) 反投影(数据一致性)后输出
的 PSNR / SSIM / Edge / LPIPS-approx。

反投影：已知降质 D = MATLAB bicubic ×0.5 (本地实测 99.8% 像素逐位相等)。
  迭代： SR <- SR + lambda * U( LR_official - D(SR) )，U = MATLAB bicubic ×2。
这会把 SR 在 D 可见子空间里的误差强行拉向 LR_official(=D(HR真值))，
理论上同时提升 PSNR/SSIM/Edge，且不引入外部数据、不是锐化。
"""
import os, sys, time, glob, argparse
import numpy as np
from PIL import Image
import torch

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE); sys.path.insert(0, ROOT)
os.chdir(ROOT)

from models import build_model
from make_external_data import imresize
import metrics as M

WEIGHT = "best_model_ema Run 14，33.56 dB.pth"
LR_DIR = "超分竞赛数据集/训练数据集/input_320"
HR_DIR = "超分竞赛数据集/训练数据集/target_640"

def load_model():
    ckpt = torch.load(WEIGHT, map_location="cpu")
    cfg = ckpt["config"]; print("config:", {k: cfg[k] for k in cfg if k != 'pretrained'})
    cfg = dict(cfg); cfg["pretrained"] = None  # 推理不需要再加载官方权重
    model = build_model(cfg)
    sd = {k: v.float() for k, v in ckpt["model"].items()}
    model.load_state_dict(sd); model.eval()
    return model

def D(x):   # 640x512 float[0,1] -> 320x256
    return imresize(x.astype(np.float64), 0.5)
def U(x):   # 320x256 -> 640x512
    return imresize(x.astype(np.float64), 2.0)

def backproject(sr01, lr01, iters, lam):
    sr = sr01.astype(np.float64).copy()
    for _ in range(iters):
        sr = sr + lam * U(lr01 - D(sr))
        sr = np.clip(sr, 0.0, 1.0)
    return sr

@torch.no_grad()
def infer(model, lr01, tta=False):
    t = torch.from_numpy(lr01[None, None].astype(np.float32))
    if not tta:
        return model(t)[0, 0].numpy()
    flips = [lambda x: x, lambda x: torch.flip(x, [-1]), lambda x: torch.flip(x, [-2]),
             lambda x: torch.flip(x, [-1, -2])]
    acc = None
    for f in flips:
        o = f(model(f(t)))
        acc = o if acc is None else acc + o
    return (acc / len(flips))[0, 0].numpy()

def metr(sr01, hr01):
    sr = torch.from_numpy(sr01.astype(np.float32))[None]
    hr = torch.from_numpy(hr01.astype(np.float32))[None]
    return (M.compute_psnr(sr, hr), M.compute_ssim(sr, hr),
            M.compute_edge_ncc(sr, hr), M.compute_lpips_approx(sr, hr))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=8)
    ap.add_argument("--tta", action="store_true")
    args = ap.parse_args()

    model = load_model()
    names = [f"{i:04d}.png" for i in range(args.n)]   # val = 0000..0089
    grid = [(0,0.0),(5,1.0),(10,1.0),(20,1.0),(30,0.8),(10,0.5)]
    agg = {g: [[],[],[],[]] for g in grid}

    for nm in names:
        lr = np.array(Image.open(os.path.join(LR_DIR, nm)).convert("L"), np.float32)/255.0
        hr = np.array(Image.open(os.path.join(HR_DIR, nm)).convert("L"), np.float32)/255.0
        t0 = time.time()
        sr_raw = infer(model, lr, tta=args.tta)
        dt = time.time()-t0
        for g in grid:
            it, lam = g
            sr = sr_raw if it == 0 else backproject(sr_raw, lr, it, lam)
            sr_u8 = np.round(np.clip(sr,0,1)*255)/255.0   # 模拟提交的 uint8 量化
            p,s,e,l = metr(sr_u8, hr)
            for j,v in enumerate((p,s,e,l)): agg[g][j].append(v)
        print(f"  {nm}  infer {dt:.1f}s  raw PSNR={np.mean(agg[(0,0.0)][0]):.4f}")

    print(f"\n=== {len(names)} val 图均值  (TTA={args.tta}) ===")
    print(f"{'(iters,lam)':<14}{'PSNR':>9}{'SSIM':>9}{'Edge':>9}{'LPIPS↓':>10}")
    base = None
    for g in grid:
        p,s,e,l = [np.mean(agg[g][j]) for j in range(4)]
        if g==(0,0.0): base=(p,s,e,l)
        dP = p-base[0]
        tag = "  <-原始" if g==(0,0.0) else f"  ΔP={dP:+.4f}"
        print(f"{str(g):<14}{p:>9.4f}{s:>9.4f}{e:>9.4f}{l:>10.4f}{tag}")

if __name__ == "__main__":
    main()
