"""
Unified model factory.

`build_model(config)` dispatches to the right architecture based on
config["arch"] ("rcan" | "swinir" | "hat").  For older checkpoints saved
without an "arch" key, the architecture is inferred from the config keys so
existing best_model.pth files keep loading.
"""


def detect_arch(config: dict) -> str:
    arch = config.get("arch")
    if arch:
        return arch.lower()
    # Backward-compatible inference for checkpoints saved before "arch" existed
    if "n_resgroups" in config:
        return "rcan"
    if "compress_ratio" in config or "overlap_ratio" in config:
        return "hat"
    return "swinir"


def build_model(config: dict):
    arch = detect_arch(config)
    if arch == "rcan":
        from model_rcan import build_model as _b
    elif arch == "hat":
        from model_hat import build_model as _b
    elif arch == "swinir":
        from model_swinir import build_model as _b
    elif arch in ("swinir_pt", "swinir_pretrained"):
        from model_swinir_pretrained import build_model as _b
    elif arch in ("hat_pt", "hat_pretrained"):
        from model_hat_pretrained import build_model as _b
    elif arch in ("hit_sir", "hit_sir_pt", "hit_srf"):
        from model_hit_sir_pretrained import build_model as _b
    elif arch in ("atd", "atd_light"):
        from model_atd_pretrained import build_model as _b
    else:
        raise ValueError(f"Unknown arch: {arch!r}")
    return _b(config)
