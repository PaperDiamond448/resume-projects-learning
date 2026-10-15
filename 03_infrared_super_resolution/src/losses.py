"""
Loss functions for infrared super-resolution.

Combined loss directly optimises the three auto-evaluated metrics:
  SSIM  → SSIMLoss  (30 pts in competition)
  PSNR  → L1Loss    (30 pts; L1 ≈ maximises PSNR empirically)
  Edge  → EdgeLoss  (20 pts; Sobel edge magnitude L1)

LPIPS (20 pts) is a perceptual metric — we approximate it by adding a
frequency-domain loss (high-freq component L1) which mimics the effect of
perceptual loss without requiring a pre-trained VGG network.
"""

import warnings

import torch
import torch.nn as nn
import torch.nn.functional as F


# ---------------------------------------------------------------------------
# SSIM Loss
# ---------------------------------------------------------------------------

class SSIMLoss(nn.Module):
    """
    Differentiable SSIM loss.  Returns (1 - SSIM) so minimising it
    maximises structural similarity.
    """

    def __init__(self, window_size: int = 11, sigma: float = 1.5, data_range: float = 1.0):
        super().__init__()
        self.window_size = window_size
        self.data_range = data_range
        self.C1 = (0.01 * data_range) ** 2
        self.C2 = (0.03 * data_range) ** 2

        kernel = self._gauss_kernel(window_size, sigma)
        # shape: [1, 1, W, W] — will be expanded to [C, 1, W, W] in forward
        self.register_buffer("kernel", kernel.unsqueeze(0).unsqueeze(0))

    @staticmethod
    def _gauss_kernel(size: int, sigma: float) -> torch.Tensor:
        coords = torch.arange(size, dtype=torch.float32) - size // 2
        g = torch.exp(-(coords ** 2) / (2 * sigma ** 2))
        g /= g.sum()
        return g.outer(g)

    def forward(self, img1: torch.Tensor, img2: torch.Tensor) -> torch.Tensor:
        img1, img2 = img1.float(), img2.float()
        B, C, H, W = img1.shape
        kernel = self.kernel.expand(C, 1, self.window_size, self.window_size)
        pad = self.window_size // 2

        mu1 = F.conv2d(img1, kernel, padding=pad, groups=C)
        mu2 = F.conv2d(img2, kernel, padding=pad, groups=C)
        mu1_sq, mu2_sq, mu12 = mu1 ** 2, mu2 ** 2, mu1 * mu2

        sig1_sq = F.conv2d(img1 * img1, kernel, padding=pad, groups=C) - mu1_sq
        sig2_sq = F.conv2d(img2 * img2, kernel, padding=pad, groups=C) - mu2_sq
        sig12   = F.conv2d(img1 * img2, kernel, padding=pad, groups=C) - mu12

        num = (2 * mu12 + self.C1) * (2 * sig12 + self.C2)
        den = (mu1_sq + mu2_sq + self.C1) * (sig1_sq + sig2_sq + self.C2)
        ssim_map = num / (den + 1e-8)
        return 1.0 - ssim_map.mean()


# ---------------------------------------------------------------------------
# Edge Loss  (Sobel + L1)
# ---------------------------------------------------------------------------

class EdgeLoss(nn.Module):
    """
    Enforces edge consistency between SR and HR using Sobel edge maps.
    Directly aligned with the competition's Edge Retention metric.
    """

    def __init__(self):
        super().__init__()
        sx = torch.tensor([[-1, 0, 1], [-2, 0, 2], [-1, 0, 1]], dtype=torch.float32)
        sy = torch.tensor([[-1, -2, -1], [0, 0, 0], [1, 2, 1]], dtype=torch.float32)
        self.register_buffer("sx", sx.view(1, 1, 3, 3))
        self.register_buffer("sy", sy.view(1, 1, 3, 3))

    def _edge_map(self, x: torch.Tensor) -> torch.Tensor:
        B, C, H, W = x.shape
        xf = x.view(B * C, 1, H, W)
        ex = F.conv2d(xf, self.sx, padding=1)
        ey = F.conv2d(xf, self.sy, padding=1)
        mag = torch.sqrt(ex ** 2 + ey ** 2 + 1e-8)
        return mag.view(B, C, H, W)

    def forward(self, sr: torch.Tensor, hr: torch.Tensor) -> torch.Tensor:
        return F.l1_loss(self._edge_map(sr), self._edge_map(hr))


# ---------------------------------------------------------------------------
# Frequency Loss  (high-frequency component)
# ---------------------------------------------------------------------------

class FrequencyLoss(nn.Module):
    """
    Penalises differences in the high-frequency components of SR and HR.
    Acts as a lightweight proxy for perceptual loss, helping LPIPS.
    Uses a Laplacian filter to extract high-freq content.
    """

    def __init__(self):
        super().__init__()
        lap = torch.tensor(
            [[0, -1, 0], [-1, 4, -1], [0, -1, 0]], dtype=torch.float32
        )
        self.register_buffer("lap", lap.view(1, 1, 3, 3))

    def _high_freq(self, x: torch.Tensor) -> torch.Tensor:
        B, C, H, W = x.shape
        xf = x.float().view(B * C, 1, H, W)
        hf = F.conv2d(xf, self.lap, padding=1)
        return hf.view(B, C, H, W)

    def forward(self, sr: torch.Tensor, hr: torch.Tensor) -> torch.Tensor:
        return F.l1_loss(self._high_freq(sr), self._high_freq(hr))


# ---------------------------------------------------------------------------
# Combined Loss
# ---------------------------------------------------------------------------

class CombinedLoss(nn.Module):
    """
    Weighted combination of L1 + SSIM + Edge + Frequency losses.

    Default weights are tuned for the competition scoring formula:
        30 pts PSNR  →  primary L1 loss
        30 pts SSIM  →  SSIM loss
        20 pts Edge  →  Edge loss
        20 pts LPIPS →  FrequencyLoss (approximation)
    """

    def __init__(
        self,
        l1_weight: float = 1.0,
        ssim_weight: float = 0.15,
        edge_weight: float = 0.1,
        freq_weight: float = 0.05,
    ):
        super().__init__()
        self.l1_w = l1_weight
        self.ssim_w = ssim_weight
        self.edge_w = edge_weight
        self.freq_w = freq_weight

        self.ssim_loss = SSIMLoss()
        self.edge_loss = EdgeLoss()
        self.freq_loss = FrequencyLoss()

    def forward(
        self, sr: torch.Tensor, hr: torch.Tensor
    ) -> tuple[torch.Tensor, dict]:
        # Only compute terms with a non-zero weight — for pure-L1 training this
        # skips three expensive convolution passes per step.
        l1 = F.l1_loss(sr, hr)
        total = self.l1_w * l1
        breakdown = {"l1": l1.item(), "ssim": 0.0, "edge": 0.0, "freq": 0.0}

        if self.ssim_w != 0:
            ssim = self.ssim_loss(sr, hr)
            total = total + self.ssim_w * ssim
            breakdown["ssim"] = ssim.item()
        if self.edge_w != 0:
            edge = self.edge_loss(sr, hr)
            total = total + self.edge_w * edge
            breakdown["edge"] = edge.item()
        if self.freq_w != 0:
            freq = self.freq_loss(sr, hr)
            total = total + self.freq_w * freq
            breakdown["freq"] = freq.item()

        return total, breakdown


# ---------------------------------------------------------------------------
# Perceptual (LPIPS) Loss — the real thing, for the final finetune stage
# ---------------------------------------------------------------------------

class PerceptualLoss(nn.Module):
    """
    Real LPIPS perceptual distance (the metric family the competition actually
    scores on the 20-point LPIPS item), used as a training objective.

    Grayscale [0,1] tensors are replicated to 3 channels and rescaled to
    [-1, 1] as LPIPS expects.  Falls back to a VGG16 feature-L1 perceptual loss
    if the `lpips` package or its weights are unavailable (e.g. offline).

    NOTE: this network is frozen (eval, requires_grad=False) — gradients flow
    only into the SR model, not into the perceptual net.
    """

    def __init__(self, net: str = "alex"):
        super().__init__()
        self.backend = None
        try:
            import lpips
            self.lpips = lpips.LPIPS(net=net, verbose=False)
            for p in self.lpips.parameters():
                p.requires_grad_(False)
            self.lpips.eval()
            self.backend = "lpips"
        except Exception as e:  # offline / weights missing → VGG fallback
            warnings.warn(f"lpips unavailable ({e}); using VGG16 perceptual fallback")
            from torchvision import models
            vgg = models.vgg16(weights=models.VGG16_Weights.DEFAULT).features[:16]
            for p in vgg.parameters():
                p.requires_grad_(False)
            self.vgg = vgg.eval()
            self.register_buffer("vgg_mean", torch.tensor([0.485, 0.456, 0.406]).view(1, 3, 1, 1))
            self.register_buffer("vgg_std", torch.tensor([0.229, 0.224, 0.225]).view(1, 3, 1, 1))
            self.backend = "vgg"

    def forward(self, sr: torch.Tensor, hr: torch.Tensor) -> torch.Tensor:
        sr3 = sr.repeat(1, 3, 1, 1) if sr.shape[1] == 1 else sr
        hr3 = hr.repeat(1, 3, 1, 1) if hr.shape[1] == 1 else hr

        if self.backend == "lpips":
            # lpips expects inputs in [-1, 1]
            return self.lpips(sr3 * 2 - 1, hr3 * 2 - 1).mean()

        # VGG fallback: feature-space L1 on ImageNet-normalised inputs
        sr_n = (sr3.clamp(0, 1) - self.vgg_mean) / self.vgg_std
        hr_n = (hr3.clamp(0, 1) - self.vgg_mean) / self.vgg_std
        return F.l1_loss(self.vgg(sr_n), self.vgg(hr_n))


class FinetuneLoss(nn.Module):
    """
    Loss for the perceptual finetune stage applied to an already-converged,
    L1-trained model.

    L1 stays dominant (weight 1.0) to anchor PSNR/SSIM near their optimum, while
    a small perceptual term nudges the output toward better LPIPS — the 20 points
    most teams leave on the table.  This is deliberately different from the past
    failed experiments, which used a fake LPIPS proxy (Laplacian high-freq) and
    SSIM/Edge terms that fight L1's gradient.

    Recommended: l1_w=1.0, perceptual_w in [0.01, 0.05], LR <= 1e-5, ~20-40 ep.
    """

    def __init__(self, l1_weight: float = 1.0, perceptual_weight: float = 0.02,
                 net: str = "alex"):
        super().__init__()
        self.l1_w = l1_weight
        self.perc_w = perceptual_weight
        self.perceptual = PerceptualLoss(net=net)

    def forward(self, sr: torch.Tensor, hr: torch.Tensor):
        l1 = F.l1_loss(sr, hr)
        # perceptual net is happiest on valid-range images
        perc = self.perceptual(sr.clamp(0, 1), hr)
        total = self.l1_w * l1 + self.perc_w * perc
        breakdown = {"l1": l1.item(), "perceptual": perc.item(),
                     "ssim": 0.0, "edge": 0.0, "freq": 0.0}
        return total, breakdown
