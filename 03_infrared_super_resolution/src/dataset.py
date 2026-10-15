"""
Dataset and data augmentation for infrared SR training.

Handles:
- 8-bit grayscale PNG images
- Random patch cropping for training efficiency
- Augmentation: flips + 90-degree rotations (geometry-preserving for infrared)
- Deterministic full-image evaluation mode
"""

import os
import random
from typing import List, Optional, Tuple

import numpy as np
from PIL import Image
import torch
from torch.utils.data import Dataset


class InfraredSRDataset(Dataset):
    """
    Paired LR-HR dataset for infrared super-resolution.

    Args:
        lr_dir:     Directory containing low-resolution input images.
        hr_dir:     Directory containing high-resolution target images.
        patch_size: LR patch size for training crops (None = full image).
        augment:    Whether to apply random augmentation.
        scale:      SR scale factor (default 2 for stage 1).
        file_list:  Explicit list of filenames; if None, scans lr_dir.
    """

    def __init__(
        self,
        lr_dir: str,
        hr_dir: str,
        patch_size: Optional[int] = 128,
        augment: bool = True,
        scale: int = 2,
        file_list: Optional[List[str]] = None,
    ):
        self.lr_dir = lr_dir
        self.hr_dir = hr_dir
        self.patch_size = patch_size
        self.augment = augment
        self.scale = scale

        if file_list is not None:
            self.filenames = file_list
        else:
            self.filenames = sorted(
                f for f in os.listdir(lr_dir) if f.lower().endswith(".png")
            )

    def __len__(self) -> int:
        return len(self.filenames)

    def __getitem__(self, idx: int) -> Tuple[torch.Tensor, torch.Tensor]:
        fname = self.filenames[idx]
        lr_img = Image.open(os.path.join(self.lr_dir, fname)).convert("L")
        hr_img = Image.open(os.path.join(self.hr_dir, fname)).convert("L")

        # numpy → float32 tensor in [0, 1]
        lr = torch.from_numpy(np.array(lr_img, dtype=np.float32)).unsqueeze(0) / 255.0
        hr = torch.from_numpy(np.array(hr_img, dtype=np.float32)).unsqueeze(0) / 255.0

        if self.patch_size is not None:
            lr, hr = self._random_crop(lr, hr)

        if self.augment:
            lr, hr = self._augment(lr, hr)

        return lr, hr

    # ------------------------------------------------------------------
    # Private helpers
    # ------------------------------------------------------------------

    def _random_crop(
        self, lr: torch.Tensor, hr: torch.Tensor
    ) -> Tuple[torch.Tensor, torch.Tensor]:
        _, h, w = lr.shape
        ps = self.patch_size

        if h <= ps or w <= ps:
            # Image too small — just return as-is (shouldn't happen with 320×256 input)
            return lr, hr

        top = random.randint(0, h - ps)
        left = random.randint(0, w - ps)
        s = self.scale

        lr_crop = lr[:, top : top + ps, left : left + ps]
        hr_crop = hr[
            :,
            top * s : (top + ps) * s,
            left * s : (left + ps) * s,
        ]
        return lr_crop, hr_crop

    def _augment(
        self, lr: torch.Tensor, hr: torch.Tensor
    ) -> Tuple[torch.Tensor, torch.Tensor]:
        # Random horizontal flip
        if random.random() > 0.5:
            lr = torch.flip(lr, [-1])
            hr = torch.flip(hr, [-1])

        # Random vertical flip
        if random.random() > 0.5:
            lr = torch.flip(lr, [-2])
            hr = torch.flip(hr, [-2])

        # Random 90° rotation (0 / 90 / 180 / 270)
        k = random.randint(0, 3)
        if k > 0:
            lr = torch.rot90(lr, k, dims=[-2, -1])
            hr = torch.rot90(hr, k, dims=[-2, -1])

        return lr, hr


class RepeatDataset(Dataset):
    """Wrap a dataset so it appears `times` longer (oversampling without copying)."""

    def __init__(self, base: Dataset, times: int = 1):
        self.base = base
        self.times = max(1, int(times))

    def __len__(self) -> int:
        return len(self.base) * self.times

    def __getitem__(self, idx: int):
        return self.base[idx % len(self.base)]


def make_paired_dataset(
    lr_dir: str,
    hr_dir: str,
    patch_size: int = 128,
    scale: int = 2,
    augment: bool = True,
    file_list: Optional[List[str]] = None,
    repeat: int = 1,
) -> Dataset:
    ds = InfraredSRDataset(
        lr_dir, hr_dir,
        patch_size=patch_size,
        augment=augment,
        scale=scale,
        file_list=file_list,
    )
    return RepeatDataset(ds, repeat) if repeat > 1 else ds


def build_combined_train(
    sources: List[dict],
    patch_size: int = 128,
    scale: int = 2,
) -> Dataset:
    """
    Combine several paired (lr_dir, hr_dir) sources into one training set.

    `sources` is a list of dicts, each:
        {"lr_dir": ..., "hr_dir": ..., "repeat": int, "file_list": optional}

    `repeat` oversamples a source — used to keep the in-domain infrared set
    from being drowned out by a much larger external (DF2K) pool.
    Returns a torch ConcatDataset.
    """
    from torch.utils.data import ConcatDataset

    parts = []
    for s in sources:
        parts.append(
            make_paired_dataset(
                s["lr_dir"], s["hr_dir"],
                patch_size=patch_size,
                scale=scale,
                augment=True,
                file_list=s.get("file_list"),
                repeat=int(s.get("repeat", 1)),
            )
        )
    return ConcatDataset(parts)


class InfraredTestDataset(Dataset):
    """
    Single-directory dataset for inference (no HR available).

    Returns (tensor, filename) pairs so the filename can be used
    when saving results.
    """

    def __init__(self, lr_dir: str):
        self.lr_dir = lr_dir
        self.filenames = sorted(
            f for f in os.listdir(lr_dir) if f.lower().endswith(".png")
        )

    def __len__(self) -> int:
        return len(self.filenames)

    def __getitem__(self, idx: int) -> Tuple[torch.Tensor, str]:
        fname = self.filenames[idx]
        img = Image.open(os.path.join(self.lr_dir, fname)).convert("L")
        tensor = torch.from_numpy(np.array(img, dtype=np.float32)).unsqueeze(0) / 255.0
        return tensor, fname


def split_dataset(
    lr_dir: str,
    hr_dir: str,
    val_ratio: float = 0.1,
    seed: int = 42,
    patch_size: int = 128,
    scale: int = 2,
) -> Tuple[InfraredSRDataset, Optional[InfraredSRDataset]]:
    """
    Split all files into train / val datasets deterministically.

    The first `val_ratio` fraction of sorted filenames becomes validation set.
    Set val_ratio=0 to use all data for training (val_ds returned as None).
    This is intended for finishing finetune passes on a converged model where
    the platform leaderboard becomes the validation signal.
    """
    all_files = sorted(
        f for f in os.listdir(lr_dir) if f.lower().endswith(".png")
    )
    if val_ratio <= 0:
        train_files = all_files
        val_files = []
    else:
        n_val = max(1, int(len(all_files) * val_ratio))
        val_files = all_files[:n_val]
        train_files = all_files[n_val:]

    train_ds = InfraredSRDataset(
        lr_dir, hr_dir,
        patch_size=patch_size,
        augment=True,
        scale=scale,
        file_list=train_files,
    )
    if not val_files:
        return train_ds, None

    val_ds = InfraredSRDataset(
        lr_dir, hr_dir,
        patch_size=None,   # full image for validation
        augment=False,
        scale=scale,
        file_list=val_files,
    )
    return train_ds, val_ds
