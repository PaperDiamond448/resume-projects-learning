"""
亲自验证：官方 LR(input_320) 到底用什么算法从 HR(target_640) 降出来的。
不信任何现成结论，直接在本地训练对上逐像素实测。
对每种候选下采样算子 D，计算 D(HR) 与官方 LR 的 8bit PSNR。
谁最高(接近量化上限) + 像素相等率高 = 真实降质算子。这是 back-projection 的地基。
"""
import os, sys, glob
import numpy as np
from PIL import Image
import torch
import torch.nn.functional as F
import cv2

sys.path.insert(0, os.path.dirname(__file__))
from make_external_data import imresize as matlab_imresize

HR_DIR = "超分竞赛数据集/训练数据集/target_640"
LR_DIR = "超分竞赛数据集/训练数据集/input_320"

def psnr_u8(a, b):
    a = a.astype(np.float64); b = b.astype(np.float64)
    mse = np.mean((a - b) ** 2)
    return float("inf") if mse == 0 else 10 * np.log10(255.0 ** 2 / mse)

def d_matlab(hr):
    return np.clip(np.round(matlab_imresize(hr.astype(np.float64), 0.5)), 0, 255).astype(np.uint8)
def d_pil(hr, r):
    return np.array(Image.fromarray(hr).resize((hr.shape[1]//2, hr.shape[0]//2), r), dtype=np.uint8)
def d_cv2(hr, i):
    return cv2.resize(hr, (hr.shape[1]//2, hr.shape[0]//2), interpolation=i).astype(np.uint8)
def d_torch(hr, mode, aa):
    t = torch.from_numpy(hr.astype(np.float32))[None, None]
    out = F.interpolate(t, scale_factor=0.5, mode=mode, antialias=aa, align_corners=False)
    return np.clip(np.round(out[0,0].numpy()), 0, 255).astype(np.uint8)

CANDS = {
    "MATLAB-bicubic-AA(记录声称)": d_matlab,
    "PIL-BICUBIC":   lambda h: d_pil(h, Image.BICUBIC),
    "PIL-BILINEAR":  lambda h: d_pil(h, Image.BILINEAR),
    "PIL-LANCZOS":   lambda h: d_pil(h, Image.LANCZOS),
    "PIL-BOX(area)": lambda h: d_pil(h, Image.BOX),
    "PIL-HAMMING":   lambda h: d_pil(h, Image.HAMMING),
    "cv2-INTER_AREA":  lambda h: d_cv2(h, cv2.INTER_AREA),
    "cv2-INTER_CUBIC": lambda h: d_cv2(h, cv2.INTER_CUBIC),
    "cv2-INTER_LINEAR":lambda h: d_cv2(h, cv2.INTER_LINEAR),
    "torch-bicubic-AA":   lambda h: d_torch(h, 'bicubic', True),
    "torch-bicubic-noAA": lambda h: d_torch(h, 'bicubic', False),
}

def main():
    hr_files = sorted(glob.glob(os.path.join(HR_DIR, "*.png")))[:30]
    print(f"在 {len(hr_files)} 对训练样本上验证降质算子...\n")
    acc = {k: [] for k in CANDS}; eqr = {k: [] for k in CANDS}
    for hp in hr_files:
        name = os.path.basename(hp); lp = os.path.join(LR_DIR, name)
        if not os.path.exists(lp): continue
        hr = np.array(Image.open(hp).convert("L"), dtype=np.uint8)
        lr = np.array(Image.open(lp).convert("L"), dtype=np.uint8)
        for k, fn in CANDS.items():
            try:
                d = fn(hr)
                if d.shape != lr.shape: continue
                acc[k].append(psnr_u8(d, lr))
                eqr[k].append(np.mean(d == lr))
            except Exception:
                pass
    print(f"{'算子':<30}{'平均PSNR(dB)':>12}{'像素相等%':>12}")
    print("-" * 56)
    rows = [(np.mean(acc[k]) if acc[k] else -1, k, 100*np.mean(eqr[k]) if eqr[k] else 0) for k in CANDS]
    for mp, k, e in sorted(rows, reverse=True):
        print(f"{k:<30}{mp:>12.2f}{e:>11.1f}%")
    print("\n>45dB 即可认定为真实降质算子；据此选 back-projection 的 D。")

if __name__ == "__main__":
    main()
