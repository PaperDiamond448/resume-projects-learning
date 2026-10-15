"""画像增量更新 —— 每轮交互后从 IntentResult 中提取可学习信号。

只统计"用户真实执行/确认"的交互：need_clarification 的轮次不计入
（用户意图未达成，统计会引入噪声）。confirm 计到待确认动作的类型上。
"""
from __future__ import annotations

from datetime import datetime

from ..core.log import get_logger
from ..intent.schema import IntentResult
from .profile import UserProfile

logger = get_logger("persona.updater")

_BUCKETS = [(5, 11, "早晨"), (11, 14, "中午"), (14, 18, "下午"), (18, 23, "晚间"), (23, 29, "深夜")]


def time_bucket(dt: datetime) -> str:
    h = dt.hour if dt.hour >= 5 else dt.hour + 24  # 0~4点归入深夜段
    for lo, hi, name in _BUCKETS:
        if lo <= h < hi:
            return name
    return "深夜"


def _bump(counter: dict, key: str, n: int = 1) -> None:
    if key:
        counter[key] = counter.get(key, 0) + n


class ProfileUpdater:
    def update(
        self,
        profile: UserProfile,
        result: IntentResult,
        pending_action: dict | None = None,
        now: datetime | None = None,
    ) -> UserProfile:
        if result.need_clarification or result.intent in ("unclear", "chitchat", "cancel"):
            return profile  # 未达成/无信息量的轮次不学习

        intent = result.intent
        slots = result.slots
        if intent == "confirm" and pending_action:
            intent = pending_action.get("type", intent)   # 确认的是什么就学什么
            slots = pending_action.get("slots", slots)    # 槽位也取自被确认的动作
        _bump(profile.intent_stats, intent)
        contact = slots.get("recipient") or slots.get("contact")
        if contact:
            info = profile.frequent_contacts.setdefault(
                str(contact), {"count": 0, "aliases": [], "preferred_app": ""}
            )
            info["count"] += 1
            if slots.get("app"):
                info["preferred_app"] = str(slots["app"])
        for app_key in ("app", "app_name"):
            if slots.get(app_key):
                _bump(profile.favorite_apps, str(slots[app_key]))
        if intent == "play_music" and slots.get("query"):
            _bump(profile.music_pref, str(slots["query"]))

        bucket = time_bucket(now or datetime.now())
        profile.habits.setdefault(bucket, {})
        _bump(profile.habits[bucket], intent)

        logger.debug("画像更新: %s += %s", profile.user_id, intent)
        return profile
