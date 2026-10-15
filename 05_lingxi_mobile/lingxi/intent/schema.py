"""意图体系 v1 与意图理解结果的校验/规范化。

场景建模（赛题要求）：意图清单即场景清单，每个意图定义其槽位。
LLM 输出必须落在此体系内，校验失败走修复重试 → 仍失败降级 unclear。
"""
from __future__ import annotations

from dataclasses import dataclass, field

# intent → (中文名, 槽位说明)。槽位均可缺省，缺关键槽位由引擎触发澄清。
INTENTS: dict[str, tuple[str, dict[str, str]]] = {
    "send_message":   ("发消息", {"recipient": "收件人", "content": "消息内容", "app": "应用(微信/短信等)"}),
    "make_call":      ("打电话", {"contact": "联系人"}),
    "play_music":     ("播放音乐", {"query": "歌名/歌手/风格, 可空"}),
    "query_weather":  ("查天气", {"location": "地点, 可空=当前", "date": "日期, 可空=今天"}),
    "query_schedule": ("查日程", {"date": "日期, 可空=今天"}),
    "query_time":     ("查时间/报时", {}),
    "create_reminder": ("设提醒", {"time": "时间", "content": "提醒内容"}),
    "take_note":      ("记笔记/记到记事本", {"content": "要记录的文本内容"}),
    "open_app":       ("打开应用", {"app_name": "应用名"}),
    "web_search":     ("帮你上网搜索", {"query": "搜索关键词"}),  # OOS 兜底：能力外查询→帮你搜
    # ↓ 个性化记忆（设备无关，PC/手机通用）：把"怎么称呼我/说话风格/喜好"沉淀进本地画像
    "set_preference": ("记住对我的称呼/说话风格/喜好", {
        "nickname": "对你的称呼(如老板/小名;'别称呼我'则留空)",
        "tone": "语气(温柔/活泼/正式/简洁等)",
        "reply_length": "详略(简短/适中/详细)",
        "use_emoji": "是否用emoji(true/false)",
        "decision_style": "决策风格(谨慎/爽快/默认)",
        "note": "其他要记住的喜好(如喜欢周杰伦/家在朝阳区)",
    }),
    "query_profile":  ("查看或清除我记住的偏好", {"op": "查看 或 清除(可空默认查看)"}),
    # ↓ 设备能力档专属意图：仅在具备相应系统能力的设备(手机)上启用，PC 默认不暴露给 LLM
    "navigate":       ("导航/带路", {"destination": "目的地", "origin": "出发地(用户说'从X'才填,默认当前位置)", "mode": "出行方式(驾车/步行/公交/地铁,可空)"}),
    "send_email":     ("发邮件", {"to": "收件人", "subject": "主题,可空", "body": "正文,可空"}),
    # 在 App 内边看屏边操作完成多步任务（搜索/播放/连续点击）；仅手机(有无障碍 agent)启用，PC 不暴露
    "run_agent":      ("在App内多步操作完成任务", {"app": "要打开并操作的App名(如b站/淘宝/网易云)", "goal": "完整任务描述(原话)"}),
    "confirm":        ("确认待执行动作", {}),
    "cancel":         ("取消/否定", {}),
    "chitchat":       ("闲聊问答", {}),
    "unclear":        ("无法判断", {}),
}

# 触发执行前必须齐备的关键槽位（缺失 → 澄清而非贸然执行）
REQUIRED_SLOTS: dict[str, list[str]] = {
    "send_message": ["recipient"],
    "make_call": ["contact"],
    "create_reminder": ["content"],
    "take_note": ["content"],
    "open_app": ["app_name"],
    "navigate": ["destination"],
    "send_email": ["to"],
    "run_agent": ["goal"],
}

# —— 设备能力档（device profile）——
# 原本列入 OOS_DOMAINS 的领域，在具备相应系统能力的设备上其实"做得到"（如手机有 geo:/mailto: ）。
# 引擎据 cfg["intent.device_capabilities"] 决定启用哪些；启用后该域不再降级 web_search，而是
# 提级为对应可执行意图。PC 默认 device_capabilities 为空 → 维持原 OOS 行为，零变化。
DEVICE_CAPABILITY_INTENTS: dict[str, str] = {
    "navigation": "navigate",        # 我方 domain → 提级后的可执行 intent
    "email_compose": "send_email",
    # 手机端具备无障碍/屏幕视觉 Agent：这些原 OOS 域若用户点名 App 或搜索/比价/观看，
    # 应进入 run_agent，而不是被前置路由/后置规则降级到 web_search。
    "booking": "run_agent",
    "shopping": "run_agent",
    "media_tv": "run_agent",
}
# 我方 domain → 域路由小模型(SMP 标签)，供前置闸对"已提级"的域放行（不抢跑到 web_search）。
DOMAIN_TO_ROUTER_LABEL: dict[str, tuple[str, ...]] = {
    "navigation": ("map",),
    "email_compose": ("email",),
    "booking": ("flight", "train", "bus"),
    "shopping": ("website",),
    "media_tv": ("video", "tvchannel", "radio", "novel", "epg"),
}

# 设备能力边界（P0-2）：以下领域当前设备做不到 → 引擎强制拒识为 unclear。
# 这是「确定性兜底、不靠模型自觉」——LLM 倾向把任何输入硬塞进最接近的已知意图
# （实测："查上海到北京的航班"→query_schedule、"股价多少"→query_weather），
# 故由引擎据 LLM 输出的 domain 字段做白名单兜底。
# 注：纯知识问答(knowledge_qa)与闲聊归 chitchat 正常应对，不在此列；other 不强制(交模型自判)防误伤。
OOS_DOMAINS: set[str] = {
    "booking",       # 订机票/火车/汽车票/酒店/餐厅
    "finance",       # 股票/基金/汇率/彩票/理财
    "translation",   # 翻译
    "navigation",    # 导航/路线规划
    "media_tv",      # 电视频道/视频/直播/电台节目/小说阅读等内容消费
    "shopping",      # 网购/比价/下单
    "email_compose", # 收发邮件
}


@dataclass
class IntentResult:
    intent: str
    slots: dict = field(default_factory=dict)
    confidence: float = 0.0
    conflict: bool = False
    conflict_resolution: str = ""
    need_clarification: bool = False
    clarify_question: str = ""
    reasoning: str = ""
    # 单次14B调用合并闲聊意图与应答。任务类始终留空并使用确定性执行回执；
    # 旧模型/fixture缺失该字段时保持兼容，由应答层走原有闲聊兜底。
    direct_reply: str = ""
    latency: float = 0.0          # 引擎填充：LLM 调用耗时(秒)
    domain: str = ""              # LLM 输出的请求所属领域；引擎据此做能力边界拒识(P0-2)
    frame_id: str = ""

    def to_dict(self) -> dict:
        return self.__dict__.copy()


def validate_llm_json(d: dict) -> tuple[IntentResult | None, list[str]]:
    """校验并规范化 LLM 输出。返回 (结果, 错误列表)；错误用于修复重试提示。"""
    errors: list[str] = []
    if not isinstance(d, dict):
        return None, ["输出不是JSON对象"]

    intent = str(d.get("intent", "")).strip()
    if intent not in INTENTS:
        errors.append(f"intent「{intent}」不在意图清单中")

    slots = d.get("slots") or {}
    if not isinstance(slots, dict):
        errors.append("slots 必须是对象")
        slots = {}
    slots = {k: v for k, v in slots.items() if v not in (None, "", [])}

    try:
        confidence = max(0.0, min(1.0, float(d.get("confidence", 0))))
    except (TypeError, ValueError):
        errors.append("confidence 必须是 0~1 数值")
        confidence = 0.0

    if errors:
        return None, errors

    direct_reply = str(d.get("direct_reply", "") or "").strip().strip('"「」')
    if intent != "chitchat":
        direct_reply = ""
    elif len(direct_reply) > 120:
        direct_reply = direct_reply[:120].rstrip()

    return IntentResult(
        intent=intent,
        slots=slots,
        confidence=confidence,
        conflict=bool(d.get("conflict", False)),
        conflict_resolution=str(d.get("conflict_resolution", "") or ""),
        need_clarification=bool(d.get("need_clarification", False)),
        clarify_question=str(d.get("clarify_question", "") or ""),
        reasoning=str(d.get("reasoning", "") or ""),
        direct_reply=direct_reply,
        domain=str(d.get("domain", "") or "").strip().lower(),
    ), []
