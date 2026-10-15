"""画像本地存储 —— data/profiles/{user_id}.json，原子写入。"""
from __future__ import annotations

import json
import os
from pathlib import Path

from ..core.config import PROJECT_ROOT, Config
from ..core.log import get_logger
from .profile import UserProfile

logger = get_logger("persona.store")


class ProfileStore:
    def __init__(self, cfg: Config) -> None:
        self.dir = PROJECT_ROOT / cfg.get("persona.profile_dir", "data/profiles")
        self.dir.mkdir(parents=True, exist_ok=True)

    def _path(self, user_id: str) -> Path:
        return self.dir / f"{user_id}.json"

    def load(self, user_id: str = "default") -> UserProfile:
        """冷启动返回空白默认画像（风格中性，随交互逐步演化）。"""
        p = self._path(user_id)
        if p.exists():
            try:
                return UserProfile.from_dict(json.loads(p.read_text(encoding="utf-8")))
            except (json.JSONDecodeError, TypeError) as e:
                logger.warning("画像文件损坏(%s)，备份后重建: %s", user_id, e)
                p.rename(p.with_suffix(".corrupt.json"))
        return UserProfile(user_id=user_id)

    def save(self, profile: UserProfile) -> None:
        profile.touch()
        p = self._path(profile.user_id)
        tmp = p.with_suffix(".tmp")
        tmp.write_text(
            json.dumps(profile.to_dict(), ensure_ascii=False, indent=1), encoding="utf-8"
        )
        os.replace(tmp, p)  # 原子替换，避免写一半损坏
