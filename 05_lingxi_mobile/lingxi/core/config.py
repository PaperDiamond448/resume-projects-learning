"""配置加载 —— configs/config.yaml + 环境变量插值。

字符串值中的 ${VAR} 会替换为对应环境变量；未设置的环境变量替换为空串，
由使用方（如 LLM 客户端）在真正需要时给出明确报错，而不是在加载期失败。
"""
from __future__ import annotations

import os
import re
from pathlib import Path
from typing import Any

import yaml

_ENV_PATTERN = re.compile(r"\$\{(\w+)\}")
PROJECT_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_CONFIG_PATH = PROJECT_ROOT / "configs" / "config.yaml"


def _interpolate(value: Any) -> Any:
    if isinstance(value, str):
        return _ENV_PATTERN.sub(lambda m: os.environ.get(m.group(1), ""), value)
    if isinstance(value, dict):
        return {k: _interpolate(v) for k, v in value.items()}
    if isinstance(value, list):
        return [_interpolate(v) for v in value]
    return value


class Config:
    """嵌套 dict 的轻量包装，支持 cfg.get("llm.provider") 点路径访问。"""

    def __init__(self, data: dict) -> None:
        self._data = data

    def get(self, path: str, default: Any = None) -> Any:
        node: Any = self._data
        for key in path.split("."):
            if not isinstance(node, dict) or key not in node:
                return default
            node = node[key]
        return node

    def require(self, path: str) -> Any:
        value = self.get(path)
        if value in (None, ""):
            raise KeyError(f"配置项缺失或为空: {path}")
        return value

    def raw(self) -> dict:
        return self._data


def load_config(path: str | Path | None = None) -> Config:
    cfg_path = Path(path) if path else DEFAULT_CONFIG_PATH
    with open(cfg_path, "r", encoding="utf-8") as f:
        data = yaml.safe_load(f) or {}
    return Config(_interpolate(data))
