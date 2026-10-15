"""
Backward-compatibility shim.

The SwinIR implementation moved to `model_swinir.py` (cleaner name + per-image
mean fix + gradient checkpointing).  This module re-exports everything so older
notebooks / scripts that `import model_SwinlR` keep working.
"""

from model_swinir import *          # noqa: F401,F403
from model_swinir import build_model, SwinIR  # noqa: F401
