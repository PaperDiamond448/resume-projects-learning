"""意图理解引擎 —— 多模态帧 + 上下文 → 校验通过的 IntentResult。

降级链：chat_json → 校验失败则修复重试1次 → 仍失败返回 unclear+澄清。
后置规则（不依赖模型自觉）：
- confidence < 阈值 → 强制澄清
- 关键槽位缺失 → 强制澄清（宁可多问一句，不可误执行）
- 无待确认动作却输出 confirm/cancel → 降级处理
"""
from __future__ import annotations

import json
import re
import time

from ..core.config import Config
from ..core.log import get_logger
from ..core.schemas import MultimodalFrame
from ..llm import LLMClient, LLMError
from .context import DialogueContext
from .prompts import build_messages, build_repair_messages
from .schema import (
    DEVICE_CAPABILITY_INTENTS, DOMAIN_TO_ROUTER_LABEL, OOS_DOMAINS,
    REQUIRED_SLOTS, IntentResult, validate_llm_json,
)

logger = get_logger("intent.engine")

_DEFAULT_CLARIFY = "抱歉，我没太明白你的意思，能再说一遍吗？"

# 手机档高置信确定性快路径。仅覆盖无pending、无视觉证据、语义边界清晰的常用查询；
# 其余输入一律回到完整14B多模态理解，不能用关键词包含关系粗暴抢跑。
_MOBILE_WEATHER_CITIES = (
    "北京", "上海", "广州", "深圳", "天津", "重庆", "成都", "杭州",
    "南京", "武汉", "西安", "苏州", "长沙", "郑州", "青岛",
)
_FUTURE_WEATHER_MARKERS = (
    "明天", "明日", "后天", "大后天", "未来", "下周", "周末",
    "下个月", "下月", "明年", "下季度",
)
_COLLOQUIAL_WEATHER_SENSATION_RE = re.compile(
    r"冷不冷|热不热|暖和(?:吗|不)?|凉快(?:吗|不)?|闷热|"
    r"(?:有点|很|挺|太|这么|是不是|会不会|好像|似乎|看起来|怎么这么)?(?:冷|热|凉|暖)"
)
_COLLOQUIAL_WEATHER_BLOCK_RE = re.compile(
    r"空调|暖气|冰箱|冰柜|手机|电脑|平板|电池|充电|发动机|机器|服务器|"
    r"显卡|处理器|CPU|锅|水|汤|饭|菜|咖啡|茶|牛奶|房间|宿舍|屋里|室内|"
    r"办公室|教室|车里|手脚|手心|脚|身体|额头|发烧|生病|心里|心凉|"
    r"冷笑话|冷知识|冷门|冷战|冷淡|热搜|热点|热门|热情|热闹|热爱|"
    r"冷色|暖色|色温|制冷|散热|发热|过热|烫"
)
_COLLOQUIAL_WEATHER_OTHER_TASK_RE = re.compile(
    r"提醒|闹钟|发短信|发消息|发送|打电话|拨打|播放|放一首|推荐|唱歌|"
    r"讲(?:个|一个)?笑话|开空调|打开空调|关空调|关闭空调|调(?:低|高|整)|设置"
)
_COLLOQUIAL_WEATHER_OUTDOOR_RE = re.compile(r"外面|外边|外头|室外|户外|出门")
_COLLOQUIAL_WEATHER_LOCAL_START_RE = re.compile(
    r"^(?:今天|今晚|今夜|现在|目前|此刻|天儿|天)(?:外面|外边|外头|室外|户外)?"
)
_COLLOQUIAL_WEATHER_POLITE_PREFIX_RE = re.compile(
    r"^(?:请问|麻烦(?:你)?(?:帮我)?看看|帮我看看|我想知道)"
)
_MOBILE_TIME_QUERY_RE = re.compile(
    r"^(?:(?:请问|请|帮我|麻烦(?:你)?(?:帮我|告诉我)?|(?:你)?能告诉我|"
    r"可以告诉我|(?:你)?告诉我(?:一下)?|(?:你)?跟我说(?:一下|下)?|我想知道|我想问|我是想问|"
    r"我(?:是|就|就是|只是)?问你|我问的是|我(?:是)?说的是))?"
    r"(?:(?:现在|当前|此刻|这会儿|这会|这时候)?(?:到底|究竟)?(?:是)?"
    r"(?:几点(?:钟)?|什么时间|时间是多少)|"
    r"(?:现在|当前|此刻)?(?:给我)?报(?:个)?时)"
    r"(?:了|呢|啦|咯)?(?:吗|吧|呀|啊|啦|呢|咯|呗)?$"
)
_MOBILE_TIME_PUNCT_RE = re.compile(r"[\s，。！？!?、；;：:“”‘’'\"（）()]")
_MOBILE_GREETING_RE = re.compile(
    r"^(?:你好|嗨|哈喽|在吗|聊聊天|陪我聊聊|陪我聊两句)[呀啊吗吧呢]?[？?。！!]*$"
)
_MOBILE_TIRED_CHAT_RE = re.compile(
    r"^(?:(?:今天|我)?(?:有点|好|很)?累)"
    r"(?:[，,、 ]*(?:陪我)?聊(?:聊|聊天|两句))?[呀啊吗吧呢]?[？?。！!]*$"
)
_MOBILE_DEVICE_TASK_GUARD_RE = re.compile(
    r"(?:发(?:送)?(?:消息|短信|邮件)|打(?:个)?电话|拨打|提醒|闹钟|导航|路线|"
    r"打开|启动|播放|搜索|查询|查一下|天气|几点|时间|联系人|设置|关闭|开启|"
    r"拍照|截图|音量|日程|记(?:录|下)|转账|付款|支付|购买|买下|下单|结算|"
    r"购物车|订票|预订|预约)"
)
_MOBILE_COMPACT_CHAT_SYSTEM = (
    "你是安卓助手灵犀。该输入已经过确定性安全规则判定为不含设备动作的纯聊天。"
    "只回复自然、友善的中文，不调用工具，不声称执行了任何操作。"
)
_MOBILE_REPLY_LENGTH_PERSIST_RE = re.compile(
    r"以后|今后|往后|接下来|从现在(?:开始|起)|之后(?:都)?"
)
_MOBILE_REPLY_LENGTH_TARGET_RE = re.compile(r"回答|回复|答复|说话|说得|说")
_MOBILE_REPLY_LENGTH_SHORT_RE = re.compile(
    r"简短|简洁|精简|扼要|短(?:一|些|点)|少说"
)
_MOBILE_REPLY_LENGTH_LONG_RE = re.compile(
    r"详细|详尽|具体|丰富|长(?:一|些|点)|多说"
)
_MOBILE_REPLY_LENGTH_MEDIUM_RE = re.compile(r"适中|正常(?:一|些|点)?|一般(?:一|些|点)?")
_MOBILE_REPLY_LENGTH_STANDALONE_RE = re.compile(
    r"^(?:请|麻烦(?:你)?)?(?:把)?(?:回答|回复|答复|说话|说)"
    r"(?:的)?(?:长度|篇幅)?(?:设置|设|改|调整|调)?(?:得|为|成)?(?:再)?"
    r"(?:简短(?:一点|一些|点|些)?|简洁(?:一点|一些|点|些)?|精简(?:一点|一些|点|些)?|扼要(?:一点|一些|点|些)?|短(?:一点|一些|点|些)|少说|"
    r"详细(?:一点|一些|点|些)?|详尽(?:一点|一些|点|些)?|具体(?:一点|一些|点|些)?|丰富(?:一点|一些|点|些)?|长(?:一点|一些|点|些)|多说|"
    r"适中(?:一点|一些|点|些)?|正常(?:一点|一些|点|些)?|一般(?:一点|一些|点|些)?)"
    r"(?:吧|呀|啊|呢)?$"
)
_MOBILE_NICKNAME_IN_REPLY_RE = re.compile(
    r"(?:回答|回复|说话)(?:我)?(?:的)?(?:时|时候)"
    r"(?:必须|都|要|请)?(?:叫|喊|称呼)(?:我)?(?:为|做)?"
    r"(?P<nickname>[\u4e00-\u9fffA-Za-z0-9]{1,8})"
    r"(?=$|[，。！？!?、；;]|并且|而且|同时|然后)"
)
_MOBILE_NICKNAME_DIRECT_RE = re.compile(
    r"^(?:(?:以后|今后|往后|接下来|从现在(?:开始|起)|之后)(?:每次|都)?)?"
    r"(?:请)?(?:你)?(?:叫|喊|称呼)我(?:为|做)?"
    r"(?P<nickname>[\u4e00-\u9fffA-Za-z0-9]{1,8}?)(?:吧|呀|啊|呢)?$"
)
_MOBILE_PREFERENCE_GUARD_RE = re.compile(
    r"叫我|喊我|称呼我|(?:回答|回复|说话)我的时候|"
    r"以后.*(?:回答|回复|说话|语气|emoji|表情)|"
    r"(?:别|不要|不用|必须|以后).*(?:emoji|表情符号)"
)


def _mobile_compact_chat_prompt(context: DialogueContext) -> tuple[str, int]:
    """把已落盘的应答画像带入紧凑单调用，并给不同篇幅真实可见的生成预算。"""
    profile = (context.profile_summary or "").strip()
    if "应答风格:简短/" in profile:
        length_rule = "回答必须简短：恰好一句话，通常不超过20个汉字，只保留直接答案。"
        max_tokens = 48
    elif "应答风格:详细/" in profile:
        length_rule = (
            "回答必须明显比简短模式更详细：恰好写两句话。第一句直接回答，"
            "第二句补充关键原理或必要背景；全文通常45至70个汉字，不要重复结论。"
        )
        max_tokens = 112
    else:
        length_rule = "回答篇幅适中：用1至2句说明答案和最关键的依据，通常不超过45个汉字。"
        max_tokens = 80
    profile_rule = (
        f"\n当前用户画像：{profile}\n严格遵循其中的称呼、语气、篇幅和emoji偏好。"
        + ("画像包含称呼时，每次回答开头必须先用该称呼称呼用户一次。"
           if "称呼用户为「" in profile else "")
        if profile else ""
    )
    return f"{_MOBILE_COMPACT_CHAT_SYSTEM}\n{length_rule}{profile_rule}", max_tokens


def _mobile_reply_length_preference(text: str) -> str:
    """只识别明确的长期篇幅设置；“这次简短回答”仍作为当前问题处理。"""
    compact = _compact_utterance(text)
    if not compact or not _MOBILE_REPLY_LENGTH_TARGET_RE.search(compact):
        return ""
    persistent = bool(_MOBILE_REPLY_LENGTH_PERSIST_RE.search(compact))
    standalone = bool(_MOBILE_REPLY_LENGTH_STANDALONE_RE.fullmatch(compact))
    if not (persistent or standalone):
        return ""
    if _MOBILE_REPLY_LENGTH_SHORT_RE.search(compact):
        return "简短"
    if _MOBILE_REPLY_LENGTH_LONG_RE.search(compact):
        return "详细"
    if _MOBILE_REPLY_LENGTH_MEDIUM_RE.search(compact):
        return "适中"
    return ""


def _mobile_nickname_preference(text: str) -> str:
    """提取明确称呼设置，支持“回复我的时候必须叫大哥”等组合表达。"""
    compact = _compact_utterance(text)
    if not compact:
        return ""
    match = _MOBILE_NICKNAME_IN_REPLY_RE.search(compact)
    if match is None:
        match = _MOBILE_NICKNAME_DIRECT_RE.fullmatch(compact)
    if match is None:
        return ""
    nickname = match.group("nickname").strip("「」“”\"'")
    return nickname if 1 <= len(nickname) <= 8 else ""


def _compact_utterance(text: str) -> str:
    return re.sub(r"\s+", "", text or "").strip()


def _mobile_current_time_request(text: str) -> bool:
    """识别完整的当前本地报时请求；事件时间、提醒、世界时钟全部委托原链。"""
    compact = _MOBILE_TIME_PUNCT_RE.sub("", (text or "").strip())
    return bool(compact and _MOBILE_TIME_QUERY_RE.fullmatch(compact))


def _llm_failure_code(error: LLMError) -> str:
    """把供应商/解析异常归为稳定机器码；日志不输出原始模型内容或用户文本。"""
    message = str(error).lower()
    if any(marker in message for marker in ("deadline", "timeout", "timed out", "超时")):
        return "LLM_TIMEOUT"
    if "无法解析为 json" in message or "jsondecode" in message:
        return "LLM_JSON_PARSE_FAILED"
    return "LLM_CALL_FAILED"


def _mobile_colloquial_weather_request(text: str) -> dict | None:
    """识别当前户外冷热口语；不扩张到未来、其他物体或复合任务。"""
    compact = re.sub(r"[\s，。！？!?、；;：:]", "", (text or "").strip())
    if not compact or any(marker in compact for marker in _FUTURE_WEATHER_MARKERS):
        return None
    normalized = _COLLOQUIAL_WEATHER_POLITE_PREFIX_RE.sub("", compact, count=1)
    if (
        _COLLOQUIAL_WEATHER_BLOCK_RE.search(normalized)
        or _COLLOQUIAL_WEATHER_OTHER_TASK_RE.search(normalized)
        or not _COLLOQUIAL_WEATHER_SENSATION_RE.search(normalized)
    ):
        return None
    city_hits = [city for city in _MOBILE_WEATHER_CITIES if city in normalized]
    if len(city_hits) > 1:
        return None
    if not city_hits and not (
        _COLLOQUIAL_WEATHER_OUTDOOR_RE.search(normalized)
        or _COLLOQUIAL_WEATHER_LOCAL_START_RE.match(normalized)
    ):
        return None
    return {"location": city_hits[0] if city_hits else "", "date": "今天"}


class IntentEngine:
    def __init__(self, cfg: Config, llm: LLMClient | None = None) -> None:
        self.llm = llm or LLMClient(cfg)
        self.conf_threshold = float(cfg.get("intent.confidence_threshold", 0.6))
        self.temperature = float(cfg.get("intent.temperature", 0.1))
        self.repair_invalid_json = bool(cfg.get("intent.repair_invalid_json", True))
        self.high_risk_actions = set(cfg.get("intent.high_risk_actions", []) or [])
        # 设备能力档（mobile 经 cfg 注入）：原本 OOS 的域在本机可执行 → 提级、不降级 web_search。
        # PC 默认空 → 两道 OOS 闸与 prompt 行为零变化。
        self.device_caps = {d for d in (cfg.get("intent.device_capabilities", []) or [])
                            if d in DEVICE_CAPABILITY_INTENTS}
        self._promoted_router_labels = {
            label
            for d in self.device_caps
            for label in DOMAIN_TO_ROUTER_LABEL.get(d, ())
        }
        # P1-1 域路由小模型（端侧 CPU 毫秒级 OOS 前置守门，省大模型调用）
        self.router = None
        self.router_threshold = float(cfg.get("intent.router_oos_threshold", 0.8))
        if cfg.get("intent.use_domain_router", True):
            from ..core.config import PROJECT_ROOT
            from .domain_router import DomainRouter
            self.router = DomainRouter.load_default(PROJECT_ROOT)
            if self.router is None:
                logger.warning("域路由模型未找到(data/models/domain_router.pkl)，跳过前置路由")

    def understand(self, frame: MultimodalFrame, context: DialogueContext) -> IntentResult:
        t0 = time.perf_counter()
        routed = self._route_mobile_deterministic(frame, context)
        if routed is not None:
            routed.latency = round(time.perf_counter() - t0, 3)
            routed.frame_id = frame.frame_id
            logger.info("手机确定性快路径: %s slots=%s (%.3fs)",
                        routed.intent, routed.slots, routed.latency)
            return routed
        routed = self._route_mobile_compact_chat(frame, context)
        if routed is not None:
            routed.latency = round(time.perf_counter() - t0, 3)
            routed.frame_id = frame.frame_id
            logger.info("手机安全纯聊天单调用: %s (%.3fs)", routed.intent, routed.latency)
            return routed
        routed = self._route_oos(frame, context)  # P1-1 域路由前置：高置信 OOS 秒级兜底，不调大模型
        if routed is not None:
            routed.latency = round(time.perf_counter() - t0, 3)
            routed.frame_id = frame.frame_id
            logger.info("域路由拦截 OOS→web_search: 「%s」(%.3fs)", frame.anchor_text, routed.latency)
            return routed
        result = self._call_with_repair(frame, context)
        result.latency = round(time.perf_counter() - t0, 2)
        result.frame_id = frame.frame_id
        self._post_rules(frame, result, context)
        logger.info(
            "意图: %s slots=%s conf=%.2f conflict=%s clarify=%s (%.2fs)",
            result.intent, result.slots, result.confidence,
            result.conflict, result.need_clarification, result.latency,
        )
        return result

    def _route_mobile_deterministic(
        self, frame: MultimodalFrame, context: DialogueContext
    ) -> IntentResult | None:
        """只在手机能力档为边界清晰的当前天气/报时/寒暄省去14B调用。

        pending、视觉证据、多城市、未来天气和不在白名单的城市都不抢跑，继续进入
        完整多模态/上下文链。这样快路径不会绕过确认状态机、冲突消解或GeoAPI槽位抽取。
        """
        if not self.device_caps or context.pending_action is not None:
            return None
        if frame.vision_events or frame.vision_summary:
            return None
        compact = _compact_utterance(frame.anchor_text)
        if not compact:
            return None

        preference_slots: dict[str, object] = {}
        reply_length = _mobile_reply_length_preference(compact)
        nickname = _mobile_nickname_preference(compact)
        if reply_length:
            preference_slots["reply_length"] = reply_length
        if nickname:
            preference_slots["nickname"] = nickname
        if preference_slots:
            return IntentResult(
                intent="set_preference",
                slots=preference_slots,
                confidence=0.99,
                domain="preference",
                reasoning="DETERMINISTIC_EXPLICIT_PREFERENCE",
            )

        if _mobile_current_time_request(compact):
            return IntentResult(
                intent="query_time", slots={}, confidence=0.99, domain="time",
                reasoning="DETERMINISTIC_CURRENT_TIME",
            )

        # 只覆盖完整匹配的常见寒暄与疲惫陪聊，避免用“累/聊”等包含关系抢走
        # 提醒、发消息或需要理解具体语义的任务。复杂闲聊仍交给14B。
        if _MOBILE_TIRED_CHAT_RE.fullmatch(compact):
            return IntentResult(
                intent="chitchat", slots={}, confidence=0.99, domain="chitchat",
                reasoning="手机确定性疲惫陪聊快路径",
                direct_reply="辛苦了，我在呢。想聊聊今天最累的那件事吗？",
            )
        if _MOBILE_GREETING_RE.fullmatch(compact):
            return IntentResult(
                intent="chitchat", slots={}, confidence=0.99, domain="chitchat",
                reasoning="手机确定性寒暄快路径",
                direct_reply="我在呢，想聊点什么？",
            )

        colloquial_weather = _mobile_colloquial_weather_request(compact)
        if colloquial_weather is not None:
            return IntentResult(
                intent="query_weather",
                slots=colloquial_weather,
                confidence=0.99,
                domain="weather",
                reasoning="手机确定性当前户外冷热口语快路径",
            )

        if any(marker in compact for marker in _FUTURE_WEATHER_MARKERS):
            return None
        city_hits = [city for city in _MOBILE_WEATHER_CITIES if city in compact]
        if len(city_hits) != 1:
            return None
        city = city_hits[0]
        prefix = r"(?:请|请问|帮我|麻烦你)?(?:查|查询|看|看看|查一下|看一下)?(?:一下)?"
        day = r"(?:今天|现在|当前)?"
        suffix = r"(?:怎么样|如何|什么情况|情况|预报|是多少|多少|呢|吗|吧)?[？?。！!]*"
        weather_re = re.compile(
            rf"^{prefix}{day}{re.escape(city)}{day}(?:的)?(?:天气|气温|温度){suffix}$"
        )
        if not weather_re.fullmatch(compact):
            return None
        return IntentResult(
            intent="query_weather",
            slots={"location": city, "date": "今天"},
            confidence=0.99,
            domain="weather",
            reasoning="手机确定性当前天气快路径",
        )

    def _route_mobile_compact_chat(
        self, frame: MultimodalFrame, context: DialogueContext
    ) -> IntentResult | None:
        """对确定无设备动作的纯聊天使用同一个14B的紧凑单调用。

        安全边界同时依赖已有域路由和设备任务否定词表：域路由明确判为 chat，
        且文本不含终端动作时，直接进入自然语言单调用。这样百科问答、二选一、
        主观交流等普通问题不再依赖少量固定触发词，也不会被完整 JSON 意图链的
        格式校验误伤。即使域路由误把“发消息”判成 chat，确定性 task guard 仍会
        阻止旁路。调用失败直接报告对话服务异常，绝不叠加第二次 LLM 调用。
        """
        if (
            not self.device_caps
            or context.pending_action is not None
            or frame.vision_events
            or frame.vision_summary
            or self.router is None
        ):
            return None
        text = (frame.anchor_text or "").strip()
        if not text or len(text) > 120:
            return None
        if _MOBILE_PREFERENCE_GUARD_RE.search(text):
            return None
        if _MOBILE_DEVICE_TASK_GUARD_RE.search(text):
            return None
        try:
            prediction = self.router.predict(text)
        except Exception as e:  # noqa: BLE001
            logger.warning("纯聊天域路由失败(%s)，回完整意图链", type(e).__name__)
            return None
        if str(prediction.get("domain", "")) != "chat" or bool(prediction.get("is_oos", False)):
            return None
        system_prompt, max_tokens = _mobile_compact_chat_prompt(context)
        try:
            reply = self.llm.chat(
                [
                    {"role": "system", "content": system_prompt},
                    {"role": "user", "content": text},
                ],
                temperature=0.35,
                max_tokens=max_tokens,
            ).strip()
        except LLMError as e:
            failure_code = _llm_failure_code(e)
            question = (
                "这次回答生成超时了，请再问一次。"
                if failure_code == "LLM_TIMEOUT"
                else "对话服务刚才没有正常返回，请再试一次。"
            )
            return IntentResult(
                intent="unclear", slots={}, confidence=0.0, domain="chitchat",
                need_clarification=True,
                clarify_question=question,
                reasoning=f"{failure_code}；安全纯聊天未叠加第二次模型请求",
            )
        if not reply:
            return IntentResult(
                intent="unclear", slots={}, confidence=0.0, domain="chitchat",
                need_clarification=True,
                clarify_question="我在听，能换个说法再和我聊聊吗？",
                reasoning="安全纯聊天返回为空",
            )
        return IntentResult(
            intent="chitchat", slots={}, confidence=0.99, domain="chitchat",
            reasoning="确定性任务否定闸后的14B紧凑纯聊天单调用",
            direct_reply=reply[:120],
        )

    def _route_oos(self, frame: MultimodalFrame, context: DialogueContext) -> IntentResult | None:
        """域路由小模型前置闸门：高置信判定超出设备能力(OOS)→ 直接 web_search 兜底，省大模型调用。
        保守策略：有待确认动作时不抢跑（交大模型处理确认）；置信不足也交大模型（P0-2 仍兜底）。"""
        if self.router is None or context.pending_action is not None:
            return None
        text = (frame.anchor_text or "").strip()
        if not text:
            return None
        pr = self.router.predict(text)
        # 已提级的域（如手机上的导航/邮件）不在前置闸抢跑，放给大模型出可执行意图
        if pr["domain"] in self._promoted_router_labels:
            return None
        if pr["is_oos"] and pr["p_oos"] >= self.router_threshold:
            return IntentResult(
                intent="web_search", slots={"query": text}, confidence=pr["p_oos"],
                domain="router_oos",
                reasoning=f"域路由小模型判 OOS({pr['domain']}, p_oos={pr['p_oos']})→网页搜索兜底",
            )
        return None

    def _call_with_repair(self, frame: MultimodalFrame, context: DialogueContext) -> IntentResult:
        messages = build_messages(frame, context, self.device_caps)
        raw_text = ""
        fallback_code = "LLM_CALL_FAILED"
        try:
            raw = self.llm.chat_json(messages, temperature=self.temperature, max_tokens=260)
            raw_text = json.dumps(raw, ensure_ascii=False)
            result, errors = validate_llm_json(raw)
            if result is not None:
                if result.need_clarification:
                    logger.info(
                        "意图模型主动澄清 code=MODEL_CLARIFY intent=%s confidence=%.2f",
                        result.intent, result.confidence,
                    )
                return result
            if not self.repair_invalid_json:
                logger.error(
                    "意图降级 code=LLM_SCHEMA_INVALID repair=disabled errors=%s", errors
                )
                return IntentResult(
                    intent="unclear", confidence=0.0,
                    need_clarification=True, clarify_question=_DEFAULT_CLARIFY,
                    reasoning="LLM_SCHEMA_INVALID",
                )
            logger.warning(
                "意图输出需修复 code=LLM_SCHEMA_INVALID_RETRY errors=%s", errors
            )
            raw = self.llm.chat_json(
                build_repair_messages(frame, context, raw_text, errors, self.device_caps),
                temperature=self.temperature, max_tokens=400,
            )
            result, errors = validate_llm_json(raw)
            if result is not None:
                if result.need_clarification:
                    logger.info(
                        "意图模型主动澄清 code=MODEL_CLARIFY intent=%s confidence=%.2f",
                        result.intent, result.confidence,
                    )
                return result
            fallback_code = "LLM_SCHEMA_INVALID_AFTER_REPAIR"
            logger.error("意图降级 code=%s errors=%s", fallback_code, errors)
        except LLMError as e:
            fallback_code = _llm_failure_code(e)
            logger.error(
                "意图降级 code=%s error_type=%s",
                fallback_code, type(e).__name__,
            )
        return IntentResult(
            intent="unclear", confidence=0.0,
            need_clarification=True, clarify_question=_DEFAULT_CLARIFY,
            reasoning=fallback_code,
        )

    def _post_rules(self, frame: MultimodalFrame, r: IntentResult, context: DialogueContext) -> None:
        # 冲突是客观事实（规则预检已检出极性对立），不依赖模型自觉标记
        pc_verdict = frame.precheck.get("verdict")
        if pc_verdict == "conflict":
            r.conflict = True
        elif pc_verdict in ("agree", "defer") or (
            pc_verdict == "no_polarity"
            and str(frame.precheck.get("vision_polarity", "")).endswith("_weak")
        ):
            r.conflict = False
            r.conflict_resolution = ""
        if r.conflict and "决策风格:谨慎" in (context.profile_summary or ""):
            r.need_clarification = True
            r.clarify_question = r.clarify_question or "我看到有冲突，要继续吗？"
        if frame.precheck.get("verdict") == "defer" and context.pending_action is not None:
            r.intent = "chitchat"
            r.need_clarification = False
            r.clarify_question = ""
            r.conflict = False
            r.reasoning = (r.reasoning + "；用户表示暂缓，保留待确认动作").lstrip("；")
            return
        # 没有跨模态冲突时，待确认短答以语音极性为准；弱表情只作为记录，不翻转语音。
        if (
            context.pending_action is not None
            and pc_verdict in ("agree", "no_polarity")
            and frame.precheck.get("speech_polarity") in ("positive", "negative")
            and r.intent in ("confirm", "cancel", "chitchat", "unclear")
        ):
            r.intent = "confirm" if frame.precheck.get("speech_polarity") == "positive" else "cancel"
            r.need_clarification = False
            r.clarify_question = ""
            r.conflict = False
            r.conflict_resolution = ""
            r.reasoning = "无跨模态冲突，按语音短答极性处理"
        # —— 能力边界兜底（P0-2）：domain 落在设备做不到的领域 → 确定性拒识为 unclear ——
        # 解决实测痛点：LLM 把超范围请求硬塞进最接近的可执行意图（订票→query_schedule、
        # 股价→query_weather），真实部署会误执行。这里"识别领域→白名单拒识"是确定性规则，
        # 不靠模型自觉。confirm/cancel（对待确认动作的应答）与 chitchat（闲聊/知识问答）不参与。
        # 提级域（手机可执行）从有效 OOS 中扣除：导航/邮件不再降级，保留 LLM 出的 navigate/send_email
        effective_oos = OOS_DOMAINS - self.device_caps
        if r.intent not in ("confirm", "cancel", "chitchat") and r.domain in effective_oos:
            # 超出设备能力的查询/交易（股价/订票/翻译…）→ 不假装查到、也不生硬拒绝，
            # 而是降级为"帮你打开浏览器搜索"(web_search)，既诚实又真办事（用户洞察 2026-06-17）。
            r.reasoning = (r.reasoning + f"｜域[{r.domain}]无数据后端，转网页搜索兜底").lstrip("｜")
            r.intent = "web_search"
            r.slots = {"query": (frame.anchor_text or r.slots.get("query", "")).strip()}
            r.need_clarification = False
            r.conflict = False
            r.clarify_question = ""
            return
        # 确定性安全规则：高风险动作遇模态冲突必须澄清，不依赖 Prompt
        if (
            r.conflict
            and context.pending_action is not None
            and context.pending_action.get("type") in self.high_risk_actions
        ):
            r.need_clarification = True
            if not r.clarify_question:
                r.clarify_question = (
                    f"该操作（{context.pending_action.get('summary', '当前动作')}）风险较高，确认要继续吗？"
                )
        # 低风险待确认动作的冲突终裁确定化：强视觉动作压过口头极性。
        # 例："不要"+点头 → confirm；"好"+摇头 → cancel。弱视觉在 conflict.py 已不触发 conflict。
        if (
            r.conflict
            and not r.need_clarification
            and context.pending_action is not None
            and context.pending_action.get("type") not in self.high_risk_actions
        ):
            v_pol = frame.precheck.get("vision_polarity")
            if v_pol == "positive":
                r.intent = "confirm"
                r.need_clarification = False
                r.clarify_question = ""
                r.conflict_resolution = r.conflict_resolution or "低风险动作，强肯定视觉压过口头否定"
            elif v_pol == "negative":
                r.intent = "cancel"
                r.need_clarification = False
                r.clarify_question = ""
                r.conflict_resolution = r.conflict_resolution or "低风险动作，强否定视觉压过口头肯定"
        # LLM 偶尔把「好，发出去」「行了，谢谢」这类待确认短答归到闲聊；
        # 预检已给出明确语音极性时，确定性收回 confirm/cancel。
        if (
            r.intent == "chitchat"
            and context.pending_action is not None
            and frame.precheck.get("speech_polarity") in ("positive", "negative")
        ):
            r.intent = "confirm" if frame.precheck.get("speech_polarity") == "positive" else "cancel"
            r.need_clarification = False
            r.clarify_question = ""
        # 闲聊是自然对话，永不澄清——直接走对话通路回应（双模关键）
        if r.intent == "chitchat":
            r.need_clarification = False
            return
        # confirm/cancel 合法性：须有待确认动作 + 用户确有肯定/否定信号（语音极性或视觉动作）。
        # 否则是 LLM 在待确认动作压力下的误判（如"谢谢你啦"被硬当成确认）→ 降级闲聊。
        # 信号判定复用 precheck 已算好的极性，零额外开销、可解释。
        if r.intent in ("confirm", "cancel"):
            pc = frame.precheck
            no_signal = (pc.get("speech_polarity", "neutral") == "neutral"
                         and pc.get("vision_polarity", "neutral").replace("_weak", "") == "neutral")
            weak_visual_only = (
                pc.get("speech_polarity", "neutral") == "neutral"
                and str(pc.get("vision_polarity", "neutral")).endswith("_weak")
            )
            if context.pending_action is None or no_signal or weak_visual_only:
                why = "无待确认动作" if context.pending_action is None else "无强确认/否定信号"
                r.reasoning += f"（{why}，按闲聊处理）"
                r.intent = "chitchat"
                r.need_clarification = False
                return
        # 低置信强制澄清（仅对任务类意图；闲聊已在上面豁免）
        if r.confidence < self.conf_threshold and not r.need_clarification:
            r.need_clarification = True
        # 代词不算有效的联系人槽位（模型偶尔直接填"他/她"糊弄关键槽位检查）
        _PRONOUNS = {"他", "她", "它", "他们", "她们", "那个人"}
        for key in ("recipient", "contact"):
            if str(r.slots.get(key, "")).strip() in _PRONOUNS:
                del r.slots[key]
        # 关键槽位缺失强制澄清
        missing = [s for s in REQUIRED_SLOTS.get(r.intent, []) if s not in r.slots]
        if missing and not r.need_clarification:
            r.need_clarification = True
            names = "、".join(missing)
            r.clarify_question = r.clarify_question or f"请补充{names}信息？"
        if r.need_clarification and not r.clarify_question:
            r.clarify_question = _DEFAULT_CLARIFY
