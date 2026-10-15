"""用户画像 Schema 与 Prompt 摘要生成。

隐私红线：画像全部存于本地 data/profiles/*.json，永不上云；
注入 LLM Prompt 的只有 to_prompt_text() 生成的精简文本摘要。

画像维度（对应赛题"常用应用、使用习惯、偏好设置"）：
  称呼/风格偏好 → 应答语气与长短
  常用联系人(含别名/常用应用) → 收件人消歧（"给妈发" → 妈妈/微信）
  常用应用、音乐偏好、时段习惯 → 槽位补全与主动建议
"""
from __future__ import annotations

from dataclasses import asdict, dataclass, field
from datetime import datetime


def _default_style() -> dict:
    return {"reply_length": "适中", "tone": "自然", "use_emoji": False}


@dataclass
class UserProfile:
    user_id: str = "default"
    nickname: str = ""                                   # 怎么称呼用户，空=不称呼
    style: dict = field(default_factory=_default_style)  # reply_length: 简短/适中/详细; tone: 活泼/自然/正式
    decision_style: str = "默认"                         # 谨慎/爽快/默认：只影响冲突澄清策略，不改变意图类别
    frequent_contacts: dict = field(default_factory=dict)  # 名字→{count, aliases:[], preferred_app}
    favorite_apps: dict = field(default_factory=dict)       # 应用→次数
    music_pref: dict = field(default_factory=dict)           # 歌手/风格关键词→次数
    habits: dict = field(default_factory=dict)               # 时段→{意图:次数}
    intent_stats: dict = field(default_factory=dict)         # 意图→总次数
    custom_notes: list = field(default_factory=list)         # 显式偏好备注（用户说"以后叫我X"等）
    updated_at: str = ""

    def to_dict(self) -> dict:
        return asdict(self)

    @classmethod
    def from_dict(cls, d: dict) -> "UserProfile":
        known = {f for f in cls.__dataclass_fields__}  # 容忍旧版文件多余字段
        return cls(**{k: v for k, v in d.items() if k in known})

    def touch(self) -> None:
        self.updated_at = datetime.now().isoformat(timespec="seconds")

    # ---- Prompt 注入 ----

    def _top(self, counter: dict, n: int = 3) -> list[tuple[str, int]]:
        return sorted(counter.items(), key=lambda kv: -kv[1])[:n]

    def clear(self) -> None:
        """清空可个性化字段（隐私可控：用户说"忘掉我的偏好"时调用），保留 user_id。"""
        self.nickname = ""
        self.style = _default_style()
        self.decision_style = "默认"
        self.frequent_contacts = {}
        self.favorite_apps = {}
        self.music_pref = {}
        self.habits = {}
        self.intent_stats = {}
        self.custom_notes = []

    def to_prompt_text(self) -> str:
        parts = []
        if self.nickname:
            parts.append(f"称呼用户为「{self.nickname}」")
        s = self.style
        emoji = "可用emoji" if s.get("use_emoji") else "不用emoji"
        parts.append(f"应答风格:{s.get('reply_length', '适中')}/{s.get('tone', '自然')}/{emoji}")
        if self.decision_style and self.decision_style != "默认":
            parts.append(f"决策风格:{self.decision_style}")
        if self.frequent_contacts:
            top = sorted(self.frequent_contacts.items(), key=lambda kv: -kv[1].get("count", 0))[:3]
            cs = []
            for name, info in top:
                alias = f"(别名:{'/'.join(info['aliases'])})" if info.get("aliases") else ""
                app = f"常用{info['preferred_app']}" if info.get("preferred_app") else ""
                cs.append(f"{name}{alias}{app}")
            parts.append("常用联系人:" + "、".join(cs))
        if self.favorite_apps:
            parts.append("常用应用:" + "、".join(k for k, _ in self._top(self.favorite_apps)))
        if self.music_pref:
            parts.append("音乐偏好:" + "、".join(k for k, _ in self._top(self.music_pref)))
        if self.habits:
            hs = []
            now_h = datetime.now().hour
            now_bucket = "深夜" if now_h < 5 or now_h >= 23 else (
                "早晨" if now_h < 11 else "中午" if now_h < 14 else "下午" if now_h < 18 else "晚间"
            )
            if now_bucket in self.habits and self.habits[now_bucket]:
                k, v = max(self.habits[now_bucket].items(), key=lambda kv: kv[1])
                if v >= 2:
                    hs.append(f"当前{now_bucket}常{k}")
            for bucket, intents in self.habits.items():
                if intents:
                    top_intent = max(intents.items(), key=lambda kv: kv[1])
                    if top_intent[1] >= 3:  # 出现3次以上才算习惯
                        hs.append(f"{bucket}常{top_intent[0]}")
            if hs:
                parts.append("习惯:" + "、".join(hs[:3]))
        if self.custom_notes:
            parts.append("备注:" + "；".join(self.custom_notes[-3:]))
        return "[用户画像] " + " | ".join(parts)


# —— 显式偏好设置（set_preference 意图落地）——
# 把 LLM 解析出的偏好槽位写进画像，返回一句"改了啥"的人话（供应答确认）。空串=没识别到可改项。
_LENGTH_ALIASES = {
    "简短": "简短", "简洁": "简短", "短": "简短", "精简": "简短", "扼要": "简短",
    "适中": "适中", "正常": "适中", "一般": "适中",
    "详细": "详细", "详尽": "详细", "具体": "详细", "长": "详细", "丰富": "详细",
}


def _parse_bool(v) -> bool | None:
    s = str(v).strip().lower()
    if s in ("true", "1", "是", "要", "用", "可以", "开", "yes", "y", "好"):
        return True
    if s in ("false", "0", "否", "不", "不要", "别", "不用", "关", "no", "n"):
        return False
    return None


def apply_preference(profile: UserProfile, slots: dict) -> str:
    """把 set_preference 槽位应用到画像；返回变更摘要(人话)。"""
    changes: list[str] = []

    if "nickname" in slots:
        nick = str(slots["nickname"]).strip().strip("「」\"'")
        # "别称呼我/不用称呼" 之类 → 清空
        if nick in ("", "无", "不用", "别", "别称呼我", "不要称呼"):
            if profile.nickname:
                profile.nickname = ""
                changes.append("以后不特意称呼你")
        else:
            profile.nickname = nick
            changes.append(f"以后叫你「{nick}」")

    if slots.get("tone"):
        tone = str(slots["tone"]).strip()
        profile.style["tone"] = tone
        changes.append(f"语气{tone}")

    if slots.get("reply_length"):
        raw = str(slots["reply_length"]).strip()
        length = next((v for k, v in _LENGTH_ALIASES.items() if k in raw), "")
        if length:
            profile.style["reply_length"] = length
            changes.append(f"回答{length}")

    if "use_emoji" in slots:
        b = _parse_bool(slots["use_emoji"])
        if b is not None:
            profile.style["use_emoji"] = b
            changes.append("会用 emoji" if b else "不用 emoji")

    if slots.get("decision_style"):
        raw = str(slots["decision_style"]).strip()
        if any(k in raw for k in ("谨慎", "保守", "多问", "确认")):
            profile.decision_style = "谨慎型"
            changes.append("冲突时会更谨慎")
        elif any(k in raw for k in ("爽快", "直接", "少问", "果断")):
            profile.decision_style = "爽快型"
            changes.append("低风险操作会更爽快")
        elif any(k in raw for k in ("默认", "正常", "恢复")):
            profile.decision_style = "默认"
            changes.append("决策风格恢复默认")

    if slots.get("note"):
        note = str(slots["note"]).strip()
        if note and note not in profile.custom_notes:
            profile.custom_notes.append(note)
            profile.custom_notes = profile.custom_notes[-10:]  # 只留最近10条
            changes.append(f"记住了：{note}")

    return "、".join(changes)
