"""
Training script for RCAN infrared super-resolution.

Usage (on a GPU machine / Colab):
    python src/train.py \
        --data_dir /path/to/超分竞赛数据集/训练数据集 \
        --save_dir ./checkpoints \
        --epochs 300 \
        --batch_size 16

Resume from checkpoint:
    python src/train.py ... --resume checkpoints/latest.pth
"""

import argparse
import copy
import os
import random
import sys
import time
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
import torch.optim as optim
from torch.utils.data import DataLoader

# Allow running from either src/ or the project root
sys.path.insert(0, str(Path(__file__).resolve().parent))
from models import build_model
from dataset import split_dataset
from losses import CombinedLoss, FinetuneLoss
from metrics import compute_psnr, compute_ssim
from ckpt_utils import strip_deterministic_buffers, load_params


# ---------------------------------------------------------------------------
# Reproducibility
# ---------------------------------------------------------------------------

def set_seed(seed: int = 42):
    random.seed(seed)
    np.random.seed(seed)
    torch.manual_seed(seed)
    if torch.cuda.is_available():
        torch.cuda.manual_seed_all(seed)
    torch.backends.cudnn.benchmark = True


# ---------------------------------------------------------------------------
# Training helpers
# ---------------------------------------------------------------------------

def train_one_epoch(
    model: nn.Module,
    loader: DataLoader,
    optimizer: optim.Optimizer,
    criterion: CombinedLoss,
    scaler,          # torch.cuda.amp.GradScaler
    device: torch.device,
    epoch: int,
    log_interval: int = 100,
    ema: "EMA | None" = None,
    raw_model: nn.Module = None,
    amp_dtype: "torch.dtype" = torch.float16,
    use_amp: bool = True,
) -> float:
    model.train()
    total_loss = 0.0
    n = len(loader)

    for i, (lr, hr) in enumerate(loader):
        lr = lr.to(device, non_blocking=True)
        hr = hr.to(device, non_blocking=True)

        optimizer.zero_grad()

        # Mixed-precision forward.  Do NOT clamp before the loss: clamping
        # zeros the gradient at saturated pixels (0/1) and slows learning.
        with torch.amp.autocast(device_type=device.type, dtype=amp_dtype, enabled=use_amp):
            sr = model(lr)
            loss, breakdown = criterion(sr, hr)

        # Scaled backward
        scaler.scale(loss).backward()
        scaler.unscale_(optimizer)
        torch.nn.utils.clip_grad_norm_(model.parameters(), max_norm=1.0)
        scaler.step(optimizer)
        scaler.update()

        if ema is not None:
            ema.update(raw_model if raw_model is not None else model)

        total_loss += loss.item()

        if (i + 1) % log_interval == 0 or i == 0 or i == n - 1:
            extra = "  ".join(f"{k}={v:.5f}" for k, v in breakdown.items()
                              if k != "l1" and v != 0.0)
            print(
                f"  [E{epoch+1:03d} {i+1:4d}/{n}] "
                f"loss={loss.item():.5f}  l1={breakdown['l1']:.5f}  {extra}"
            )

    return total_loss / n


@torch.no_grad()
def validate(
    model: nn.Module,
    loader: DataLoader,
    device: torch.device,
) -> tuple[float, float]:
    model.eval()
    psnr_sum = 0.0
    ssim_sum = 0.0
    count = 0

    for lr, hr in loader:
        lr = lr.to(device, non_blocking=True)
        hr = hr.to(device, non_blocking=True)

        sr = model(lr).clamp(0.0, 1.0)

        for b in range(sr.shape[0]):
            psnr_sum += compute_psnr(sr[b], hr[b])
            ssim_sum += compute_ssim(sr[b], hr[b])
            count += 1

    return psnr_sum / count, ssim_sum / count


# ---------------------------------------------------------------------------
# EMA (online weight averaging — cheap +0.05~0.15 dB at zero extra training)
# ---------------------------------------------------------------------------

class EMA:
    """
    Exponential moving average of model weights.

    Updated every optimizer step with decay warmup so the shadow model
    does not lag behind during the first few hundred steps.  Matches by
    state_dict key (robust to DataParallel / gradient-checkpoint wrappers).
    Integer buffers (e.g. num_batches_tracked) are copied directly.
    """

    def __init__(self, model: nn.Module, decay: float = 0.999):
        self.decay = decay
        self.steps = 0
        self.shadow = copy.deepcopy(model)
        self.shadow.eval()
        for p in self.shadow.parameters():
            p.requires_grad_(False)

    @torch.no_grad()
    def update(self, model: nn.Module) -> None:
        self.steps += 1
        d = min(self.decay, (1.0 + self.steps) / (10.0 + self.steps))
        msd = model.state_dict()
        for k, v in self.shadow.state_dict().items():
            src = msd[k].detach()
            if v.dtype.is_floating_point:
                v.mul_(d).add_(src.to(v.dtype), alpha=1.0 - d)
            else:
                v.copy_(src)

    def state_dict(self) -> dict:
        return {
            "decay": self.decay,
            "steps": self.steps,
            "shadow": self.shadow.state_dict(),
        }

    def load_state_dict(self, sd: dict) -> None:
        self.decay = sd["decay"]
        self.steps = sd["steps"]
        self.shadow.load_state_dict(sd["shadow"])


# ---------------------------------------------------------------------------
# Checkpoint helpers
# ---------------------------------------------------------------------------

def save_checkpoint(
    path: str,
    epoch: int,
    model: nn.Module,
    optimizer: optim.Optimizer,
    scheduler,
    best_psnr: float,
    config: dict,
):
    torch.save(
        {
            "epoch": epoch,
            "model": model.state_dict(),
            "optimizer": optimizer.state_dict(),
            "scheduler": scheduler.state_dict(),
            "best_psnr": best_psnr,
            "config": config,
        },
        path,
    )


def save_best_model(
    path: str,
    model: nn.Module,
    config: dict,
):
    """Save FP16 model for submission (required by competition rules).

    确定性 buffer(attn_mask / relative_position_index)在加载时由 __init__ 重建,
    存盘时剥掉以压缩模型体积(第二阶段评分项)。
    """
    slim = strip_deterministic_buffers(model.state_dict())
    state_fp16 = {k: v.half() for k, v in slim.items()}
    torch.save({"model": state_fp16, "config": config}, path)
    size_mb = os.path.getsize(path) / (1024 ** 2)
    print(f"  Saved best model → {path}  ({size_mb:.2f} MB)")


def save_best_checkpoint(
    path: str,
    epoch: int,
    model: nn.Module,
    optimizer: optim.Optimizer,
    scheduler,
    best_psnr: float,
    config: dict,
):
    """Full checkpoint at best PSNR epoch — allows resuming from best point."""
    torch.save(
        {
            "epoch": epoch,
            "model": model.state_dict(),
            "optimizer": optimizer.state_dict(),
            "scheduler": scheduler.state_dict(),
            "best_psnr": best_psnr,
            "config": config,
        },
        path,
    )


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Train RCAN for infrared 2x SR")

    # Data
    p.add_argument("--data_dir", type=str, required=True,
                   help="Root of 训练数据集 (contains input_320/ and target_640/)")
    p.add_argument("--lr_folder", type=str, default="input_320")
    p.add_argument("--hr_folder", type=str, default="target_640")
    p.add_argument("--val_ratio", type=float, default=0.1,
                   help="Fraction of data for validation")
    p.add_argument("--extra_data", action="append", nargs=3,
                   metavar=("LR_DIR", "HR_DIR", "REPEAT"), default=None,
                   help="Extra paired training source: LR_DIR HR_DIR REPEAT "
                        "(repeatable). Use for DF2K/external synthesized pairs.")
    p.add_argument("--infrared_repeat", type=int, default=1,
                   help="Oversample factor for the in-domain infrared train set "
                        "so a large external pool does not drown it out.")

    # Model
    p.add_argument("--arch", type=str, default="hat",
                   choices=["hat", "swinir", "rcan", "swinir_pt", "hat_pt", "hit_sir", "hit_srf", "atd"],
                   help="Architecture to train ('swinir_pt'/'hat_pt'/'hit_sir'/'hit_srf'/'atd' = official pretrained finetune)")
    p.add_argument("--pretrained", type=str, default=None,
                   help="Path to official pretrained .pth (for --arch swinir_pt / hat_pt)")
    p.add_argument("--hat_variant", type=str, default="hat_l", choices=["hat", "hat_l"],
                   help="HAT size for --arch hat_pt (hat_l = strongest, ~40M)")
    p.add_argument("--swinir_variant", type=str, default="classical",
                   choices=["classical", "light"],
                   help="SwinIR size for --arch swinir_pt: "
                        "'classical'=SwinIR-M ~12M (stage-1 ×2 main); "
                        "'light'=SwinIR-S ~0.9M/FP16~1.7MB (★stage-2 ×4 lightweight main)")
    p.add_argument("--scale",       type=int,   default=2)
    p.add_argument("--embed_dim",   type=int,   default=144)
    p.add_argument("--depths",      type=int,   nargs="+", default=[6, 6, 6, 6, 6, 6])
    p.add_argument("--num_heads",   type=int,   nargs="+", default=[6, 6, 6, 6, 6, 6])
    p.add_argument("--window_size", type=int,   default=16)
    p.add_argument("--mlp_ratio",   type=float, default=2.0)
    p.add_argument("--use_checkpoint", action="store_true",
                   help="Gradient checkpointing — fits patch=128 on a 24GB GPU")
    # HAT-specific
    p.add_argument("--compress_ratio", type=int,   default=3)
    p.add_argument("--squeeze_factor", type=int,   default=30)
    p.add_argument("--conv_scale",     type=float, default=0.01)
    p.add_argument("--overlap_ratio",  type=float, default=0.5)
    # RCAN-specific
    p.add_argument("--n_resgroups", type=int, default=10)
    p.add_argument("--n_resblocks", type=int, default=20)
    p.add_argument("--n_feats",     type=int, default=64)

    # Training
    p.add_argument("--patch_size", type=int, default=128,
                   help="LR patch size for training crops (window attention needs >=128)")
    p.add_argument("--batch_size", type=int, default=4)
    p.add_argument("--epochs", type=int, default=300)
    p.add_argument("--lr", type=float, default=2e-4)
    p.add_argument("--min_lr", type=float, default=1e-6)
    p.add_argument("--num_workers", type=int, default=4)
    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--amp", type=str, default="fp16", choices=["fp16", "bf16", "none"],
                   help="Mixed precision: fp16 (default), bf16 (stable for transformers "
                        "like SwinIR/HAT — avoids fp16 attention overflow), none (fp32)")

    # Loss weights
    p.add_argument("--l1_w", type=float, default=1.0)
    p.add_argument("--ssim_w", type=float, default=0.0)
    p.add_argument("--edge_w", type=float, default=0.0)
    p.add_argument("--freq_w", type=float, default=0.0)

    # Perceptual finetune stage (real LPIPS) — only for finishing a converged model
    p.add_argument("--perceptual", action="store_true",
                   help="Use L1 + real LPIPS finetune loss (run on a converged model)")
    p.add_argument("--perc_w",   type=float, default=0.02,
                   help="Perceptual (LPIPS) weight; keep small so PSNR holds")
    p.add_argument("--perc_net", type=str, default="alex", choices=["alex", "vgg"],
                   help="LPIPS backbone")

    # EMA (online weight averaging)
    p.add_argument("--ema", action="store_true",
                   help="Maintain an EMA shadow of model weights; saved as best_model_ema.pth")
    p.add_argument("--ema_decay", type=float, default=0.999,
                   help="EMA decay (with warmup); higher = slower mix, longer memory")

    # I/O
    p.add_argument("--save_dir", type=str, default="checkpoints")
    p.add_argument("--resume", type=str, default=None,
                   help="Path to a checkpoint to resume from")
    p.add_argument("--finetune", type=str, default=None,
                   help="Path to best_model.pth to fine-tune from (model weights only, optimizer reset)")
    p.add_argument("--val_every", type=int, default=5,
                   help="Run validation every N epochs (also the save cadence when --val_ratio 0)")

    return p.parse_args()


def main():
    args = parse_args()
    set_seed(args.seed)

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"Device: {device}")
    if device.type == "cuda":
        print(f"GPU:    {torch.cuda.get_device_name(0)}")
        print(f"VRAM:   {torch.cuda.get_device_properties(0).total_memory / 1024**3:.1f} GB")

    # ------------------------------------------------------------------
    # Data
    # ------------------------------------------------------------------
    lr_dir = os.path.join(args.data_dir, args.lr_folder)
    hr_dir = os.path.join(args.data_dir, args.hr_folder)

    if not args.extra_data and args.infrared_repeat == 1:
        # Single-source path (unchanged behaviour)
        train_ds, val_ds = split_dataset(
            lr_dir, hr_dir,
            val_ratio=args.val_ratio,
            patch_size=args.patch_size,
            scale=args.scale,
        )
    else:
        # Multi-source path: held-out infrared val + (infrared + external) train pool
        from dataset import InfraredSRDataset, build_combined_train
        all_files = sorted(f for f in os.listdir(lr_dir) if f.lower().endswith(".png"))
        if args.val_ratio > 0:
            n_val = max(1, int(len(all_files) * args.val_ratio))
            val_files, train_files = all_files[:n_val], all_files[n_val:]
        else:
            val_files, train_files = [], all_files

        sources = [{"lr_dir": lr_dir, "hr_dir": hr_dir,
                    "file_list": train_files, "repeat": args.infrared_repeat}]
        for lr_d, hr_d, rep in args.extra_data or []:
            sources.append({"lr_dir": lr_d, "hr_dir": hr_d, "repeat": int(rep)})
        train_ds = build_combined_train(sources, patch_size=args.patch_size, scale=args.scale)

        val_ds = None
        if val_files:
            val_ds = InfraredSRDataset(
                lr_dir, hr_dir, patch_size=None, augment=False,
                scale=args.scale, file_list=val_files,
            )
        print("Data sources:")
        print(f"  infrared train: {len(train_files)} × {args.infrared_repeat}")
        for lr_d, hr_d, rep in args.extra_data or []:
            n = len([f for f in os.listdir(lr_d) if f.lower().endswith('.png')])
            print(f"  external: {lr_d}  ({n} × {rep})")

    val_count = len(val_ds) if val_ds is not None else 0
    print(f"Train: {len(train_ds)} samples  |  Val: {val_count} images"
          + ("  (full-data mode — platform is the val signal)" if val_count == 0 else ""))

    train_loader = DataLoader(
        train_ds,
        batch_size=args.batch_size,
        shuffle=True,
        num_workers=args.num_workers,
        pin_memory=(device.type == "cuda"),
        drop_last=True,
    )
    val_loader = None
    if val_ds is not None:
        val_loader = DataLoader(
            val_ds,
            batch_size=1,
            shuffle=False,
            num_workers=2,
            pin_memory=(device.type == "cuda"),
        )

    # ------------------------------------------------------------------
    # Model
    # ------------------------------------------------------------------
    if args.arch == "rcan":
        config = {
            "arch":        "rcan",
            "n_resgroups": args.n_resgroups,
            "n_resblocks": args.n_resblocks,
            "n_feats":     args.n_feats,
            "scale":       args.scale,
            "n_colors":    1,
        }
    elif args.arch == "swinir_pt":
        # Variant-driven official SwinIR finetune. Hyperparams are fixed inside
        # model_swinir_pretrained.py per variant so they match the official weights;
        # here we only record the variant + scale so infer.py rebuilds correctly.
        config = {
            "arch":           "swinir_pt",
            "swinir_variant": args.swinir_variant,
            "scale":          args.scale,
            "n_colors":       1,
            "pretrained":     args.pretrained,
            "use_checkpoint": args.use_checkpoint,
        }
    elif args.arch == "hat_pt":
        # Official pretrained HAT / HAT-L finetune (params fixed to match weights)
        config = {
            "arch":        "hat_pt",
            "scale":       args.scale,
            "hat_variant": args.hat_variant,
            "n_colors":    1,
            "pretrained":  args.pretrained,
            "use_checkpoint": args.use_checkpoint,
        }
    elif args.arch in ("hit_sir", "hit_srf"):
        # Official pretrained HiT-SIR / HiT-SRF finetune (hyperparams fixed in wrapper;
        # HiT-SRF is the SRFormer-based, stronger variant — same config, different arch class)
        config = {
            "arch":           args.arch,
            "scale":          args.scale,
            "n_colors":       1,
            "pretrained":     args.pretrained,
            "use_checkpoint": args.use_checkpoint,
        }
    elif args.arch == "atd":
        # Official pretrained ATD-light finetune (hyperparams fixed in wrapper to match v2 weight)
        config = {
            "arch":           "atd",
            "scale":          args.scale,
            "n_colors":       1,
            "pretrained":     args.pretrained,
            "use_checkpoint": args.use_checkpoint,
        }
    else:
        config = {
            "arch":        args.arch,
            "img_size":    args.patch_size,
            "embed_dim":   args.embed_dim,
            "depths":      tuple(args.depths),
            "num_heads":   tuple(args.num_heads),
            "window_size": args.window_size,
            "mlp_ratio":   args.mlp_ratio,
            "scale":       args.scale,
            "n_colors":    1,
            "use_checkpoint": args.use_checkpoint,
        }
        if args.arch == "hat":
            config.update({
                "compress_ratio": args.compress_ratio,
                "squeeze_factor": args.squeeze_factor,
                "conv_scale":     args.conv_scale,
                "overlap_ratio":  args.overlap_ratio,
            })
    model = build_model(config).to(device)
    print(f"Parameters: {model.param_count_m():.2f}M  |  "
          f"FP16 size: {model.model_size_mb_fp16():.1f} MB")

    n_gpu = torch.cuda.device_count()
    if n_gpu > 1:
        print(f"DataParallel: {n_gpu} GPUs — each handles {args.batch_size // n_gpu} samples/batch")
        model = nn.DataParallel(model)
    raw_model = model.module if isinstance(model, nn.DataParallel) else model

    # ------------------------------------------------------------------
    # Optimiser & Scheduler
    # ------------------------------------------------------------------
    optimizer = optim.Adam(model.parameters(), lr=args.lr, betas=(0.9, 0.999))
    scheduler = optim.lr_scheduler.CosineAnnealingLR(
        optimizer, T_max=args.epochs, eta_min=args.min_lr
    )
    if args.perceptual:
        criterion = FinetuneLoss(
            l1_weight=args.l1_w,
            perceptual_weight=args.perc_w,
            net=args.perc_net,
        ).to(device)
        print(f"Loss: L1 + {args.perc_w}·LPIPS({args.perc_net}) "
              f"[{criterion.perceptual.backend}]  (perceptual finetune stage)")
    else:
        criterion = CombinedLoss(
            l1_weight=args.l1_w,
            ssim_weight=args.ssim_w,
            edge_weight=args.edge_w,
            freq_weight=args.freq_w,
        ).to(device)
    amp_map = {"fp16": torch.float16, "bf16": torch.bfloat16, "none": torch.float32}
    amp_dtype = amp_map[args.amp]
    use_amp = (device.type == "cuda" and args.amp != "none")
    # GradScaler only needed for fp16 (bf16 has fp32 exponent range; fp32 needs nothing)
    scaler = torch.amp.GradScaler(device.type, enabled=(use_amp and args.amp == "fp16"))
    print(f"AMP: {args.amp}" + ("" if use_amp else " (full fp32)"))

    # ------------------------------------------------------------------
    # Resume
    # ------------------------------------------------------------------
    start_epoch = 0
    best_psnr = 0.0
    best_psnr_ema = 0.0
    resume_ckpt = None

    if args.finetune and os.path.isfile(args.finetune):
        ckpt = torch.load(args.finetune, map_location=device, weights_only=False)
        load_params(raw_model, ckpt["model"])  # 容忍瘦身权重缺确定性 buffer
        print(f"Fine-tuning from {args.finetune} (optimizer/scheduler reset)")
    elif args.resume and os.path.isfile(args.resume):
        resume_ckpt = torch.load(args.resume, map_location=device, weights_only=False)
        raw_model.load_state_dict(resume_ckpt["model"])
        optimizer.load_state_dict(resume_ckpt["optimizer"])
        scheduler.load_state_dict(resume_ckpt["scheduler"])
        start_epoch = resume_ckpt["epoch"] + 1
        best_psnr = resume_ckpt.get("best_psnr", 0.0)
        best_psnr_ema = resume_ckpt.get("best_psnr_ema", 0.0)
        print(f"Resumed from epoch {start_epoch}  |  best PSNR so far: {best_psnr:.4f}")

    # EMA shadow — initialised AFTER finetune/resume so it tracks the
    # converged starting weights instead of random init.
    ema = EMA(raw_model, decay=args.ema_decay) if args.ema else None
    if ema is not None and resume_ckpt is not None and "ema" in resume_ckpt:
        ema.load_state_dict(resume_ckpt["ema"])
        print(f"  Restored EMA shadow ({ema.steps} steps, decay={ema.decay})")
    if ema is not None:
        print(f"EMA enabled (decay={args.ema_decay})")
    del resume_ckpt  # release the big tensors

    os.makedirs(args.save_dir, exist_ok=True)

    # ------------------------------------------------------------------
    # Training loop
    # ------------------------------------------------------------------
    has_val = val_loader is not None

    for epoch in range(start_epoch, args.epochs):
        t0 = time.time()
        train_loss = train_one_epoch(
            model, train_loader, optimizer, criterion, scaler, device, epoch,
            ema=ema, raw_model=raw_model, amp_dtype=amp_dtype, use_amp=use_amp,
        )

        do_save = (epoch + 1) % args.val_every == 0 or epoch == args.epochs - 1
        if do_save:
            elapsed = time.time() - t0
            if has_val:
                psnr_val, ssim_val = validate(model, val_loader, device)
                print(
                    f"[Epoch {epoch+1:3d}/{args.epochs}]  "
                    f"train_loss={train_loss:.5f}  "
                    f"PSNR={psnr_val:.4f}  "
                    f"SSIM={ssim_val:.4f}  "
                    f"lr={scheduler.get_last_lr()[0]:.2e}  "
                    f"({elapsed:.1f}s)"
                )

                if psnr_val > best_psnr:
                    best_psnr = psnr_val
                    save_best_model(
                        os.path.join(args.save_dir, "best_model.pth"), raw_model, config
                    )
                    save_best_checkpoint(
                        os.path.join(args.save_dir, "best_checkpoint.pth"),
                        epoch, raw_model, optimizer, scheduler, best_psnr, config,
                    )
                    print(f"  ↑ New best PSNR: {best_psnr:.4f} dB")

                # EMA validation — only save EMA file if it beats prior EMA best
                if ema is not None:
                    psnr_e, ssim_e = validate(ema.shadow, val_loader, device)
                    print(f"  EMA: PSNR={psnr_e:.4f}  SSIM={ssim_e:.4f}")
                    if psnr_e > best_psnr_ema:
                        best_psnr_ema = psnr_e
                        save_best_model(
                            os.path.join(args.save_dir, "best_model_ema.pth"),
                            ema.shadow, config,
                        )
                        print(f"  ↑ New best EMA PSNR: {best_psnr_ema:.4f} dB")
            else:
                # Full-data mode: no val signal. Save latest FP16 + EMA each cadence.
                print(
                    f"[Epoch {epoch+1:3d}/{args.epochs}]  "
                    f"train_loss={train_loss:.5f}  "
                    f"lr={scheduler.get_last_lr()[0]:.2e}  "
                    f"({elapsed:.1f}s)  [full-data: no val]"
                )
                save_best_model(
                    os.path.join(args.save_dir, "best_model.pth"), raw_model, config
                )
                if ema is not None:
                    save_best_model(
                        os.path.join(args.save_dir, "best_model_ema.pth"),
                        ema.shadow, config,
                    )

        # Always save latest checkpoint for resuming (includes EMA state)
        ckpt_dict = {
            "epoch": epoch,
            "model": raw_model.state_dict(),
            "optimizer": optimizer.state_dict(),
            "scheduler": scheduler.state_dict(),
            "best_psnr": best_psnr,
            "best_psnr_ema": best_psnr_ema,
            "config": config,
        }
        if ema is not None:
            ckpt_dict["ema"] = ema.state_dict()
        torch.save(ckpt_dict, os.path.join(args.save_dir, "latest.pth"))

        scheduler.step()

    if has_val:
        print(f"\nTraining complete.  Best Val PSNR: {best_psnr:.4f} dB"
              + (f"  |  Best EMA PSNR: {best_psnr_ema:.4f} dB" if ema is not None else ""))
    else:
        print(f"\nTraining complete (full-data mode). "
              f"Compare best_model.pth vs best_model_ema.pth on the platform"
              if ema is not None else
              f"\nTraining complete (full-data mode).")


if __name__ == "__main__":
    main()
