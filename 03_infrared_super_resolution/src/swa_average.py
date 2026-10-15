"""
Offline weight averaging — average a list of FP16 submission checkpoints
into a single FP16 file.

Use case: belt-and-braces backup to in-training EMA. If you trained without
--ema, or want to combine snapshots from multiple short runs, dump them all
and average here.

Constraints:
  - All inputs must share the same architecture / shape (asserted).
  - The output preserves the config of the first input (so infer.py picks
    up the right arch automatically).

Usage:
    python src/swa_average.py \
        --inputs checkpoints/best_e160.pth checkpoints/best_e180.pth checkpoints/best_e200.pth \
        --output checkpoints/best_swa.pth
"""

import argparse
import os

import torch


def main():
    p = argparse.ArgumentParser(description="Average FP16 checkpoints (SWA-style).")
    p.add_argument("--inputs", nargs="+", required=True,
                   help="List of best_model_*.pth files to average")
    p.add_argument("--output", required=True,
                   help="Output FP16 file (same format as best_model.pth)")
    p.add_argument("--weights", nargs="+", type=float, default=None,
                   help="Optional per-input weights (default: uniform)")
    args = p.parse_args()

    if args.weights is not None and len(args.weights) != len(args.inputs):
        raise ValueError(f"--weights ({len(args.weights)}) must match --inputs ({len(args.inputs)})")
    weights = args.weights or [1.0] * len(args.inputs)
    total = sum(weights)
    weights = [w / total for w in weights]

    print(f"Averaging {len(args.inputs)} checkpoints:")
    for f, w in zip(args.inputs, weights):
        print(f"  {w:.4f} × {f}")

    accum: dict[str, torch.Tensor] = {}
    config = None
    keys_ref = None

    for path, w in zip(args.inputs, weights):
        ckpt = torch.load(path, map_location="cpu")
        if "model" not in ckpt:
            raise ValueError(f"{path} is not a submission ckpt (no 'model' key)")
        sd = ckpt["model"]
        if keys_ref is None:
            keys_ref = set(sd.keys())
            config = ckpt.get("config", None)
        else:
            if set(sd.keys()) != keys_ref:
                missing = keys_ref - set(sd.keys())
                extra = set(sd.keys()) - keys_ref
                raise ValueError(
                    f"key mismatch in {path}: missing={list(missing)[:3]} extra={list(extra)[:3]}"
                )

        for k, v in sd.items():
            v32 = v.float()
            if k not in accum:
                accum[k] = torch.zeros_like(v32)
            if v32.dtype.is_floating_point:
                accum[k].add_(v32, alpha=w)
            else:
                # Integer buffers: just take the last one (they should be identical anyway)
                accum[k] = v32

    out_state = {k: v.half() if v.dtype.is_floating_point else v
                 for k, v in accum.items()}
    payload = {"model": out_state}
    if config is not None:
        payload["config"] = config

    os.makedirs(os.path.dirname(args.output) or ".", exist_ok=True)
    torch.save(payload, args.output)
    size_mb = os.path.getsize(args.output) / (1024 ** 2)
    print(f"Wrote {args.output}  ({size_mb:.1f} MB)")


if __name__ == "__main__":
    main()
