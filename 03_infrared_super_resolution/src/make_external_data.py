"""
外部公开数据 → 与官方逐像素一致的训练对合成器。

本地实测已确认:官方 LR = MATLAB imresize bicubic(抗锯齿) + 8bit 取整,
对 target_640 复现 input_320 可达 ~70 dB(残差 98.9% 在 ±0.5,纯量化)。
本脚本用同一套 imresize,把任意公开 HR 图(可见光/红外,RGB/灰度)合成出
与官方降质口径完全一致的 LR,从而无降质域差地扩充训练数据(规则明确允许)。

用法示例(在 GPU 实例上,对下载好的 DF2K HR 目录):
    python src/make_external_data.py \
        --hr_dir external_raw/DF2K_HR \
        --out_root data_ext/df2k \
        --scales 2 4 --to_gray --crop_size 512 --crop_stride 480 \
        --min_size 256

产出:
    out_root/HR/*.png        (灰度 HR,可能是从大图切出的子图)
    out_root/LR_x2/*.png      (×2 LR,与 HR 同名)
    out_root/LR_x4/*.png      (×4 LR,与 HR 同名,若 --scales 含 4)

之后训练时把 hr_dir=out_root/HR、lr_dir=out_root/LR_x2 当成普通配对目录即可。
"""

import argparse
import os
from concurrent.futures import ProcessPoolExecutor, as_completed

import numpy as np
from PIL import Image


# ----------------------------------------------------------------------
# MATLAB imresize bicubic(抗锯齿)—— 与官方降质口径一致(本地实测 ~70dB)
# ----------------------------------------------------------------------
def _cubic(x):
    ax = np.abs(x)
    ax2 = ax * ax
    ax3 = ax2 * ax
    return (
        (1.5 * ax3 - 2.5 * ax2 + 1) * (ax <= 1)
        + (-0.5 * ax3 + 2.5 * ax2 - 4 * ax + 2) * ((ax > 1) & (ax <= 2))
    )


def _contributions(in_len, out_len, scale):
    kernel_width = 4.0 / scale if scale < 1 else 4.0
    x = np.arange(1, out_len + 1, dtype=np.float64)
    u = x / scale + 0.5 * (1 - 1.0 / scale)
    left = np.floor(u - kernel_width / 2)
    P = int(np.ceil(kernel_width)) + 2
    ind = left[:, None] + np.arange(P)[None, :]
    if scale < 1:
        weights = scale * _cubic(scale * (u[:, None] - ind))
    else:
        weights = _cubic(u[:, None] - ind)
    weights = weights / np.sum(weights, axis=1, keepdims=True)
    ind = np.minimum(np.maximum(ind - 1, 0), in_len - 1).astype(np.int64)
    return weights, ind


def imresize(img, scale):
    """img: float64 HxW (单通道)。返回 float64 缩放结果(未取整)。"""
    img = img.astype(np.float64)
    H, W = img.shape
    oh = int(round(H * scale))
    ow = int(round(W * scale))
    # 先竖直方向
    wr, ir = _contributions(H, oh, scale)
    tmp = np.empty((oh, W), dtype=np.float64)
    for i in range(oh):
        tmp[i] = np.sum(img[ir[i], :] * wr[i][:, None], axis=0)
    # 再水平方向
    wc, ic = _contributions(W, ow, scale)
    out = np.empty((oh, ow), dtype=np.float64)
    for j in range(ow):
        out[:, j] = np.sum(tmp[:, ic[j]] * wc[j][None, :], axis=1)
    return out


def downsample_uint8(hr_u8, factor):
    """hr_u8: uint8 HxW;factor=2 或 4。返回 uint8 LR(匹配官方口径)。"""
    lr = imresize(hr_u8.astype(np.float64), 1.0 / factor)
    return np.clip(np.round(lr), 0, 255).astype(np.uint8)


# ----------------------------------------------------------------------
# 子图切分(把 2K 大图切成小块,加速训练 IO;无重叠或带步长)
# ----------------------------------------------------------------------
def crop_subimages(arr, size, stride):
    H, W = arr.shape
    crops = []
    if size <= 0 or (H <= size and W <= size):
        return [arr]
    ys = list(range(0, max(1, H - size + 1), stride))
    xs = list(range(0, max(1, W - size + 1), stride))
    if ys[-1] != H - size:
        ys.append(H - size)
    if xs[-1] != W - size:
        xs.append(W - size)
    for y in ys:
        for x in xs:
            crops.append(arr[y : y + size, x : x + size])
    return crops


def process_one(args_tuple):
    path, out_root, scales, to_gray, crop_size, crop_stride, min_size, factor_align = args_tuple
    try:
        img = Image.open(path)
        if to_gray:
            img = img.convert("L")
        else:
            img = img.convert("L")  # 任务是灰度,统一转 L
        arr = np.array(img, dtype=np.uint8)
    except Exception as e:
        return (path, 0, f"open_fail:{e}")

    if arr.ndim != 2:
        return (path, 0, "not_2d")

    base = os.path.splitext(os.path.basename(path))[0]
    subs = crop_subimages(arr, crop_size, crop_stride)
    written = 0
    for k, hr in enumerate(subs):
        H, W = hr.shape
        if H < min_size or W < min_size:
            continue
        # 让 HR 尺寸能被最大 scale 整除,保证 LR×scale 与 HR 完全对齐
        H2 = H - (H % factor_align)
        W2 = W - (W % factor_align)
        if H2 < min_size or W2 < min_size:
            continue
        hr = hr[:H2, :W2]
        name = f"{base}_{k:03d}.png" if len(subs) > 1 else f"{base}.png"
        Image.fromarray(hr).save(os.path.join(out_root, "HR", name))
        for s in scales:
            lr = downsample_uint8(hr, s)
            Image.fromarray(lr).save(os.path.join(out_root, f"LR_x{s}", name))
        written += 1
    return (path, written, "ok")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--hr_dir", required=True, help="外部 HR 原图目录")
    ap.add_argument("--out_root", required=True)
    ap.add_argument("--scales", type=int, nargs="+", default=[2])
    ap.add_argument("--to_gray", action="store_true")
    ap.add_argument("--crop_size", type=int, default=0, help="0=不切;否则切成该尺寸子图")
    ap.add_argument("--crop_stride", type=int, default=0, help="子图步长,默认=crop_size(无重叠)")
    ap.add_argument("--min_size", type=int, default=256, help="子图最小边,过滤太小的块")
    ap.add_argument("--workers", type=int, default=max(1, (os.cpu_count() or 2) - 1))
    ap.add_argument("--exts", nargs="+", default=[".png", ".jpg", ".jpeg", ".bmp", ".tif", ".tiff"])
    args = ap.parse_args()

    stride = args.crop_stride if args.crop_stride > 0 else (args.crop_size or 1)
    factor_align = max(args.scales)

    os.makedirs(os.path.join(args.out_root, "HR"), exist_ok=True)
    for s in args.scales:
        os.makedirs(os.path.join(args.out_root, f"LR_x{s}"), exist_ok=True)

    files = []
    for root, _, fs in os.walk(args.hr_dir):
        for f in fs:
            if os.path.splitext(f)[1].lower() in args.exts:
                files.append(os.path.join(root, f))
    files.sort()
    print(f"找到 {len(files)} 张外部 HR 原图,scales={args.scales},crop={args.crop_size}/{stride},workers={args.workers}")

    tasks = [
        (p, args.out_root, args.scales, args.to_gray, args.crop_size, stride, args.min_size, factor_align)
        for p in files
    ]
    total_sub = 0
    done = 0
    with ProcessPoolExecutor(max_workers=args.workers) as ex:
        futs = [ex.submit(process_one, t) for t in tasks]
        for fut in as_completed(futs):
            _, w, status = fut.result()
            total_sub += w
            done += 1
            if done % 200 == 0:
                print(f"  进度 {done}/{len(files)},累计子图 {total_sub}")
    print(f"完成:{done} 张原图 → {total_sub} 对训练样本,输出在 {args.out_root}")


if __name__ == "__main__":
    main()
