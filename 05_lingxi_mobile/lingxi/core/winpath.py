"""Windows 中文路径兼容工具。

本项目路径含中文（d:\\华为比赛），而多个 C++ 底层库（MediaPipe、sherpa-onnx）
无法打开非 ANSI 路径。MediaPipe 走字节流加载绕过；sherpa-onnx 只认文件路径，
用本工具把模型文件镜像到 ASCII 安全目录（%LOCALAPPDATA%\\lingxi\\models）。
按文件大小判断是否需要重新复制，二次启动零开销。
"""
from __future__ import annotations

import os
import shutil
from pathlib import Path

from .log import get_logger

logger = get_logger("core.winpath")


def _is_ascii(p: Path) -> bool:
    try:
        str(p).encode("ascii")
        return True
    except UnicodeEncodeError:
        return False


def ensure_ascii_dir(src_dir: Path, mirror_name: str | None = None) -> Path:
    """目录路径含非 ASCII 时，镜像到安全位置并返回新路径；否则原样返回。"""
    src_dir = Path(src_dir)
    if _is_ascii(src_dir):
        return src_dir
    base = Path(os.environ.get("LOCALAPPDATA", Path.home())) / "lingxi" / "models"
    dst = base / (mirror_name or src_dir.name)
    dst.mkdir(parents=True, exist_ok=True)
    for f in src_dir.rglob("*"):  # 递归：部分模型带子目录（如 melo TTS 的 dict/ 分词词典）
        if not f.is_file():
            continue
        target = dst / f.relative_to(src_dir)
        if not target.exists() or target.stat().st_size != f.stat().st_size:
            target.parent.mkdir(parents=True, exist_ok=True)
            logger.info("镜像模型文件到 ASCII 路径: %s → %s", f.relative_to(src_dir), dst)
            shutil.copy2(f, target)
    return dst
