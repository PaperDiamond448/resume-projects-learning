"""个性化层：用户画像 Schema、本地存储、交互增量更新（隐私全端侧）。"""

from .profile import UserProfile, apply_preference
from .store import ProfileStore
from .updater import ProfileUpdater

__all__ = ["UserProfile", "ProfileStore", "ProfileUpdater", "apply_preference"]
