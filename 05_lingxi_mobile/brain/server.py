"""灵犀 Mobile · 大脑服务端 —— 把随项目打包的 lingxi 核心管线包成 HTTP 服务。

设计（独立目录内自包含，不依赖外部 PC 项目源码）：
- 人脸/音频只留端侧；一般交互上传文本证据。GUI-agent截图另受Cloud-3明示同意、用途、敏感页和摘要双门约束。
- 服务端跑：意图理解 → 冲突消解 → 个性化应答（terminal=mock，服务端只决策、不真执行）。
- 真正的设备动作（发短信/打电话/开 App…）由【安卓端】根据返回的 device_action 落地。

适配层职责（不污染大脑）：把大脑的两段式确认流归一化成一个统一的 `device_action`
字段——安卓端只需判断 device_action 是否为空决定要不要在手机上真执行。

运行：
  python brain/server.py            # 默认 0.0.0.0:8000
  python brain/server.py --port 8000 --host 0.0.0.0
依赖：见 brain/requirements.txt。需环境变量 DASHSCOPE_API_KEY 才能跑需要 14B 的接口。
"""
from __future__ import annotations

import argparse
import copy
import json as _json
import logging
import os
import re
import sys
import threading
import time
import urllib.parse as _urlparse
import urllib.request as _urlreq
import uuid
from pathlib import Path

# —— 独立化：默认加载本目录随附的 lingxi 核心，不再引用外部 PC 项目。——
# LINGXI_CORE_PROJECT 只用于开发者显式替换核心实现；正常运行无需设置。
BUNDLED_PROJECT = Path(__file__).resolve().parents[1]
CORE_PROJECT = Path(
    os.environ.get("LINGXI_CORE_PROJECT", str(BUNDLED_PROJECT))
).expanduser().resolve()
if not CORE_PROJECT.exists():
    raise SystemExit(
        f"找不到大脑核心目录: {CORE_PROJECT}（应包含 lingxi/ 与 configs/）"
    )
if not (CORE_PROJECT / "lingxi").is_dir():
    raise SystemExit(f"大脑核心目录缺少 lingxi/ 包: {CORE_PROJECT}")
if not (CORE_PROJECT / "configs" / "config.yaml").is_file():
    raise SystemExit(f"大脑核心目录缺少 configs/config.yaml: {CORE_PROJECT}")
sys.path.insert(0, str(CORE_PROJECT))

# 运行时画像只留在本工程的 brain/data/profiles，不污染随附的只读核心代码/配置。
# 可用绝对路径环境变量覆盖，默认落到 brain/data/profiles（已在 .gitignore 排除）。
MOBILE_PROFILE_DIR = Path(
    os.environ.get(
        "LINGXI_MOBILE_PROFILE_DIR",
        str(Path(__file__).resolve().parent / "data" / "profiles"),
    )
).expanduser().resolve()

from fastapi import FastAPI, HTTPException  # noqa: E402
from pydantic import BaseModel, Field  # noqa: E402

from cloud_security import (  # noqa: E402
    AuthConfigurationError,
    CloudSecurityMiddleware,
    SecurityConfig,
    readiness as cloud_readiness,
)
from cloud_vision_policy import validate_cloud_screenshot  # noqa: E402

from lingxi.core.config import Config, load_config  # noqa: E402
from lingxi.core.deadline import HardDeadlineExceeded, run_with_deadline  # noqa: E402
from lingxi.core.schemas import (  # noqa: E402
    MultimodalFrame, SpeechUtterance, VisionEvent, VisionKind,
)
from lingxi.fusion.aligner import aggregate_vision  # noqa: E402
from lingxi.fusion.conflict import precheck  # noqa: E402
from lingxi.response.actions import MockTerminal  # noqa: E402
from lingxi.runtime import InteractionSession  # noqa: E402

# 需要安卓端真正在手机上落地的意图（查询类结果已在 reply 里，不必下发动作）
DEVICE_INTENTS = {
    "open_app", "play_music", "create_reminder", "take_note", "web_search",
    "make_call", "send_message", "navigate", "send_email", "run_agent",
    "open_camera", "show_alarms", "start_timer", "add_calendar",
    "query_calendar", "open_setting", "toggle_flashlight",
}

# L2.5 高风险硬守卫：这些动作不能让弱视觉肯定覆盖明确口头否定。
HIGH_RISK_PENDING_TYPES = {"send_message", "make_call", "send_email"}

CONFIRMATION_SOURCES = {
    "spoken", "typed", "vision_only", "spoken_visual", "typed_visual", "unknown",
}
VISUAL_ACCEPT_LABELS = {"nod", "thumb_up"}
VISUAL_REJECT_LABELS = {"shake", "thumb_down"}
VISUAL_MIN_STABLE_COUNT = 5
_PARAMETER_CHANGE_RE = re.compile(
    r"(?:收件人|联系人|号码|电话|正文|内容|消息|短信|应用|渠道|时间|金额)"
    r".{0,8}(?:改|换|变|修改|换成|改成)|"
    r"(?:不对|不是|改一下|换一下).{0,10}(?:发给|打给|拨给|内容|正文|号码)"
)
_PENDING_CONTEXT_QUESTION_RE = re.compile(
    r"(?:刚才|之前|这条|当前).{0,8}(?:发给谁|打给谁|收件人|联系人|内容|正文|号码)|"
    r"(?:发给谁|打给谁|内容是什么|正文是什么|号码是多少)"
)
_EXPLICIT_UNRELATED_NEW_TASK_RE = re.compile(
    r"(?:查|查询|查一下|看看|看一下).{0,24}(?:天气|日程|日历|路线|路况|时间|新闻)|"
    r"(?:打开|启动|关闭|播放|搜索|导航|带我去|设置|创建|新建|添加|记录|记下|提醒|"
    r"拨打|打电话|发邮件|发送邮件)"
)

# 设备能力档：手机具备系统级导航(geo:)/邮件(mailto:)能力 → 通知意图层把这两个原 OOS 域
# 提级为可执行意图(navigate/send_email)，不再降级 web_search。PC 项目不受影响（仅本进程注入）。
MOBILE_DEVICE_CAPABILITIES = ["navigation", "email_compose", "booking", "shopping", "media_tv"]

# "打开之外的动作"指示词：原话含这些 = 不只想开 App、还要在 App 里做事 → open_app 升级为 run_agent。
# 谨慎选词，避免把"打开设置/打开微信"这类纯打开误升（那些不含这些动作词）。
_INAPP_ACTION_RE = re.compile(
    r"播放|放一?[首下个]|听[首一]?|第[一二三四五六七八九十百零两\d]+[首个条集集部位]|"
    r"新建|创建|写一?[篇个条]|发(?:个|一条|一个|条|篇)?(?:朋友圈|动态|微博|帖|笔记)|"
    r"搜索?|搜一?[下个]|找一?[个下首]|查一下|点进|点开|点击|进去|刷一?[下会]|滑到|"
    r"评论|弹幕|点赞|关注|收藏|下单|购买|加入购物车|加到购物车|购物车|加购|结算|签到|抢|"
    r"比价|哪家便宜|最便宜|哪个便宜|评分最高|销量最高|三连|栏目|入口"
)

# 云端 Agent 恢复门：明确点名已知三方 App 且目标是低风险的搜索/播放/浏览/比价时，
# 不再把入口押给 4 秒通用 JSON 意图调用；先确定性建立 run_agent，再由 Agent 专用预算完成规划。
# 支付、下单、对外发布等动作故意不在此旁路，仍交完整意图/安全链处理。
_SAFE_DIRECT_AGENT_ACTION_RE = re.compile(
    r"播放|每日推荐|日推|搜索?|搜一?[下个]|找一?[个下首]|查找|"
    r"比价|比较.{0,12}价格|哪家便宜|哪个便宜|最便宜|"
    r"看(?:视频|直播|商品|价格|主页|内容)|点进|点开|点击.{0,16}(?:栏目|入口|页面|内容)"
)
_DIRECT_AGENT_HIGH_RISK_RE = re.compile(
    r"下单|购买|立即购买|买(?:下|一个|一件|这个|它)|订购|付款|支付|转账|结算|提交订单|确认订单|"
    r"加入购物车|加到购物车|购物车|加购|抢购|"
    r"发消息|发送消息|发短信|发送短信|发邮件|发送邮件|"
    r"发布|发朋友圈|发动态|发微博|发帖|评论|点赞|关注|收藏"
)


def _looks_like_inapp_task(text: str) -> bool:
    """原话是否含"打开之外的 App 内动作"（决定 open_app 是否升级为 run_agent）。"""
    return bool(_INAPP_ACTION_RE.search(text or ""))


_SOCIAL_POST_RE = re.compile(r"发(?:个|一条|一个|条|篇)?(?:朋友圈|动态|微博|帖|帖子|笔记)|发布(?:朋友圈|动态|微博|帖子|笔记)")


def _looks_like_social_post(text: str) -> bool:
    """公开内容发布属于 App 内多步操作，不是给某个联系人发消息。"""
    return bool(_SOCIAL_POST_RE.search(text or ""))


def _match_known_apps(text: str) -> list[str]:
    """按用户原话中的出现顺序返回已点名的已知第三方 App，单个 guide 只返回一次。"""
    import app_guides

    q = (text or "").lower()
    if not q:
        return []
    hits: list[tuple[int, str]] = []
    for guide in app_guides.APP_GUIDES.values():
        aliases = []
        for alias in guide["aliases"]:
            normalized = (alias or "").lower()
            if not normalized:
                continue
            pos = q.find(normalized)
            if pos >= 0:
                aliases.append((pos, -len(normalized), alias))
        if aliases:
            pos, _neg_len, alias = min(aliases)
            hits.append((pos, alias))
    return [alias for _pos, alias in sorted(hits, key=lambda item: item[0])]


def _match_known_app(text: str) -> str | None:
    """文本里提到的已知第三方 App（`app_guides.APP_GUIDES` 的任意别名）→ 该别名原文。
    用来判断"这句话到底有没有点名一个具体三方 App"，不重复维护别名表。"""
    apps = _match_known_apps(text)
    return apps[0] if apps else None


def _safe_direct_agent_apps(text: str) -> list[str]:
    """返回可直接建立 Agent 任务的 App；空列表表示继续走完整意图/安全链。"""
    compact = re.sub(r"\s+", "", text or "")
    if (
        not compact
        or _DIRECT_AGENT_HIGH_RISK_RE.search(compact)
        or not _SAFE_DIRECT_AGENT_ACTION_RE.search(compact)
    ):
        return []
    return _match_known_apps(compact)


# T-17 ①：booking/shopping/media_tv 类任务点了名动作、但没点名具体 App 时，大脑核心 LLM
# 常按"纯信息查询"降级成 web_search/unclear（prompts.py 的既有规则：只有点名 App 或明确
# App 内动作才出 run_agent）——15 任务集里"帮我点份水饺""机票哪家便宜""放个视频"这类自然说法
# 因此全部卡住，必须先说"打开X"才触发 agent。用确定性 App 建议表兜底为 run_agent（不碰 PC 提示词，
# 只在 mobile 这层加一道兜底）。多 App 比价类(机票/比价)先开建议表第一个，真正跨 App 编排是 T-13 的活。
_CAPABILITY_APP_HINTS: list[tuple[str, re.Pattern[str]]] = [
    ("美团外卖", re.compile(r"点(?:份|个|一份|一个)?(?:外卖|水饺|奶茶|咖啡|快餐|盒饭)|叫外卖|点外卖")),
    ("携程", re.compile(r"机票|火车票|高铁票|车票|订(?:酒店|房)|订票")),
    ("淘宝", re.compile(r"比价|哪家便宜|最便宜|哪个便宜|销量最高|评分最高")),
    ("哔哩哔哩", re.compile(r"放个?视频|看视频|刷视频|追剧|看直播|进直播间")),
]


def _capability_app_suggestion(text: str) -> str | None:
    for app, pattern in _CAPABILITY_APP_HINTS:
        if pattern.search(text or ""):
            return app
    return None


def _cn_num(raw: str) -> int:
    """中文/阿拉伯数字 → int，够解析计时器/日历粗粒度时间。"""
    s = (raw or "").strip()
    if not s:
        return -1
    if s.isdigit():
        return int(s)
    digit = {"零": 0, "〇": 0, "一": 1, "二": 2, "两": 2, "三": 3, "四": 4,
             "五": 5, "六": 6, "七": 7, "八": 8, "九": 9}
    if s == "十":
        return 10
    if "十" in s:
        i = s.index("十")
        tens = 1 if i == 0 else digit.get(s[i - 1], -1)
        ones = 0 if i == len(s) - 1 else digit.get(s[i + 1], -1)
        return -1 if tens < 0 or ones < 0 else tens * 10 + ones
    return digit.get(s[0], -1)


def _parse_duration_seconds(text: str) -> int:
    total = 0
    for raw, unit in re.findall(r"([0-9一二两三四五六七八九十]{1,4})\s*(小时|分钟|分|秒)", text or ""):
        n = _cn_num(raw)
        if n < 0:
            continue
        if unit == "小时":
            total += n * 3600
        elif unit in ("分钟", "分"):
            total += n * 60
        else:
            total += n
    return total


def _mobile_system_shortcut(text: str) -> dict | None:
    """手机端确定性系统能力：无需 LLM，直接下发安卓系统 Intent/API。"""
    t = (text or "").strip()
    compact = re.sub(r"\s+", "", t)
    if not compact:
        return None

    if any(k in compact for k in ("打开相机", "启动相机", "拍照", "照相")):
        return {"intent": "open_camera", "slots": {}, "reply": "已打开相机。"}

    if any(k in compact for k in ("打开闹钟", "查看闹钟", "闹钟列表", "显示闹钟")):
        return {"intent": "show_alarms", "slots": {}, "reply": "已打开闹钟列表。"}

    if ("计时" in compact or "倒计时" in compact or "定时器" in compact) and (
        seconds := _parse_duration_seconds(compact)
    ) > 0:
        return {
            "intent": "start_timer",
            "slots": {"seconds": str(seconds), "message": "灵犀计时器"},
            "reply": f"已为你设置 {seconds // 60 if seconds >= 60 else seconds}{'分钟' if seconds >= 60 else '秒'}计时器。",
        }

    if any(k in compact for k in ("开手电", "打开手电", "手电筒打开", "打开闪光灯")):
        return {"intent": "toggle_flashlight", "slots": {"state": "on"}, "reply": "已尝试打开手电筒。"}
    if any(k in compact for k in ("关手电", "关闭手电", "手电筒关闭", "关闭闪光灯")):
        return {"intent": "toggle_flashlight", "slots": {"state": "off"}, "reply": "已尝试关闭手电筒。"}

    setting_map = [
        (("wifi", "WiFi", "无线网", "无线网络"), "wifi", "Wi-Fi 设置"),
        (("蓝牙", "bluetooth"), "bluetooth", "蓝牙设置"),
        (("音量", "声音", "铃声"), "sound", "声音设置"),
        (("亮度", "显示", "屏幕"), "display", "显示设置"),
        (("定位", "位置"), "location", "位置信息设置"),
        (("电池", "省电"), "battery", "电池设置"),
        (("无障碍",), "accessibility", "无障碍设置"),
        (("网络", "移动数据", "流量"), "network", "网络设置"),
    ]
    if "设置" in compact or compact.startswith("打开"):
        for keys, setting, label in setting_map:
            if any(k.lower() in compact.lower() for k in keys):
                return {"intent": "open_setting", "slots": {"setting": setting}, "reply": f"已打开{label}。"}

    calendar_create_words = ("加入日历", "添加日程", "新建日程", "加到日历", "写到日历", "创建日程")
    calendar_query_words = ("查看", "查询", "查查", "看看", "有什么", "有哪些", "几点", "安排", "忙不忙", "空不空")
    if (
        not any(k in compact for k in calendar_create_words)
        and any(k in compact for k in ("日程", "日历", "安排"))
        and any(k in compact for k in calendar_query_words)
    ):
        if "大后天" in compact:
            day_offset = 3
        elif "后天" in compact:
            day_offset = 2
        elif "明天" in compact or "明日" in compact:
            day_offset = 1
        else:
            day_offset = 0

        days = 1
        if "今明两天" in compact or "未来两天" in compact or "接下来两天" in compact:
            day_offset, days = 0, 2
        else:
            future_days = re.search(r"(?:未来|接下来)([2-7二三四五六七])天", compact)
            if future_days:
                parsed = _cn_num(future_days.group(1))
                if 2 <= parsed <= 7:
                    day_offset, days = 0, parsed

        label = (
            "今天" if day_offset == 0 else
            "明天" if day_offset == 1 else
            "后天" if day_offset == 2 else
            "大后天"
        )
        if days > 1:
            label = f"未来{days}天"
        return {
            "intent": "query_calendar",
            "slots": {"day_offset": str(day_offset), "days": str(days)},
            "reply": f"正在手机本地读取{label}的日程。",
        }

    if any(k in compact for k in calendar_create_words):
        title = re.sub(r"(帮我|请|把|加入日历|添加日程|新建日程|加到日历|写到日历)", "", t).strip(" ，。")
        if not title:
            title = "灵犀日程"
        return {
            "intent": "add_calendar",
            "slots": {"title": title, "time_text": t},
            "reply": f"已打开日历编辑：{title}",
        }
    return None


def _mobile_send_message_request(text: str) -> dict | None:
    """保守解析“给某人发短信说某内容”，高风险动作只建立待确认态，不直接执行。

    这是手机端的确定性安全入口：同一句指令不能因 LLM 路由抖动时而 send_message、
    时而 chitchat。只匹配同时具备联系人和正文的明确句式；缺任一项仍交常规澄清链。
    """
    t = re.sub(r"\s+", "", (text or "").strip())
    if not t or _looks_like_social_post(t):
        return None
    patterns = (
        # 给妈妈发短信说晚点回家 / 给李娜发送消息：会议推迟
        r"^给(?P<recipient>[^，。,:：]{1,12}?)(?:发|发送)(?:一条)?(?:短信|消息)"
        r"(?:说|内容(?:是|为)?|[:：])?(?P<content>.+)$",
        # 给妈妈发送我晚点回家的消息
        r"^给(?P<recipient>[^，。,:：]{1,12}?)发送(?P<content>.+?)(?:的)?(?:短信|消息)$",
    )
    for pattern in patterns:
        m = re.match(pattern, t)
        if not m:
            continue
        recipient = m.group("recipient").strip("“”\"'「」 ，。,:：")
        content = m.group("content").strip("“”\"'「」 ，。,:：")
        if recipient and content:
            slots = {"recipient": recipient, "content": content, "app": "短信"}
            return {
                "intent": "send_message",
                "slots": slots,
                "summary": f"通过短信给{recipient}发送「{content}」",
            }
    return None

SECURITY_CONFIG = SecurityConfig.from_env(BUNDLED_PROJECT)
_secure_api = SECURITY_CONFIG.mode == "required"
if _secure_api:
    # The bundled core's legacy INFO logs include utterances, slots and replies.
    # Secure mode emits only the metadata-only cloud_security audit record.
    logging.getLogger("lingxi").setLevel(logging.CRITICAL)
    logging.getLogger("lingxi.cloud_security").setLevel(logging.INFO)
app = FastAPI(
    title="灵犀 Mobile · 大脑服务端",
    version="0.2.0",
    docs_url=None if _secure_api else "/docs",
    redoc_url=None if _secure_api else "/redoc",
    openapi_url=None if _secure_api else "/openapi.json",
)
app.add_middleware(CloudSecurityMiddleware, config=SECURITY_CONFIG)

_cfg = None
_sessions: dict[str, InteractionSession] = {}
_lock = threading.RLock()
_session_locks: dict[str, threading.RLock] = {}
_terminal_pending_ids: dict[str, list[str]] = {}


def _ensure_cfg():
    """加载并注入手机设备能力档的配置（单例）。注入只改本进程对象，不落盘。
    收口在此，保证无论 /interact 还是 /agent_step 先触发，引擎都拿到手机档。"""
    global _cfg
    if _cfg is None:
        _cfg = load_config()
        _cfg.raw().setdefault("intent", {})["device_capabilities"] = MOBILE_DEVICE_CAPABILITIES
        _cfg.raw().setdefault("persona", {})["profile_dir"] = str(MOBILE_PROFILE_DIR)
        # Railway公网档冻结单次14B调用预算：超时后由IntentEngine返回确定性澄清，
        # 不能让供应商长尾或非超时错误重试把单轮拖到赛题5秒红线之外。
        # 本地USB/PC调试继续沿用config.yaml，不被云端预算静默影响。
        if os.environ.get("LINGXI_DEPLOYMENT_STAGE", "").strip() == "railway":
            raw_timeout = os.environ.get("LINGXI_LLM_REQUEST_TIMEOUT_SECONDS", "").strip()
            if raw_timeout != "4.5":
                raise RuntimeError(
                    "Railway LLM request timeout must be the frozen 4.5-second budget"
                )
            llm_cfg = _cfg.raw().setdefault("llm", {})
            llm_cfg["request_timeout"] = 4.5
            llm_cfg["max_retries"] = 0
            llm_cfg["hard_deadline"] = 4.5
            _cfg.raw().setdefault("intent", {})["repair_invalid_json"] = False
            _cfg.raw().setdefault("response", {})["allow_chitchat_reply_fallback"] = False
    return _cfg


# —— 真实天气：只允许显式城市；云服务器IP绝不替代用户位置 ——
_weather_provider = None


def _get_weather_provider():
    """仅借 RealTerminal 的 .weather()(和风)；绝不调它的真实动作(开应用等)，那些仍交安卓端。"""
    global _weather_provider
    if _weather_provider is None:
        from lingxi.response.actions import RealTerminal
        _weather_provider = RealTerminal()
    return _weather_provider


class MobileTerminal(MockTerminal):
    """手机大脑终端：天气只查显式城市；其余动作仍mock并交安卓落地。"""

    _weather_cache: dict[tuple[str, str], tuple[float, str]] = {}
    _weather_cache_lock = threading.RLock()
    _weather_cache_ttl_seconds = 120.0
    _railway_weather_deadline_seconds = 3.5

    def weather(self, location: str, date: str) -> str:
        loc = (location or "").strip()
        day = (date or "").strip()
        key = (loc, day)
        now = time.monotonic()
        with self._weather_cache_lock:
            cached = self._weather_cache.get(key)
            if cached and cached[0] > now:
                return cached[1]
            if cached:
                self._weather_cache.pop(key, None)

        try:
            if os.environ.get("LINGXI_DEPLOYMENT_STAGE", "").strip() == "railway":
                value = run_with_deadline(
                    lambda: _get_weather_provider().weather(loc, day),
                    self._railway_weather_deadline_seconds,
                    label="weather",
                )
            else:
                value = _get_weather_provider().weather(loc, day)
        except HardDeadlineExceeded:
            return f"暂时无法获取{loc}的实时天气，请稍后再试"

        # 只缓存真实成功结果；凭据/网络失败的诚实回执不应掩盖服务恢复。
        unavailable = ("暂时无法获取", "服务暂不可用", "缺少明确城市")
        if value and not any(marker in value for marker in unavailable):
            with self._weather_cache_lock:
                self._weather_cache[key] = (
                    time.monotonic() + self._weather_cache_ttl_seconds,
                    value,
                )
        return value


# —— 真实路线：高德路径规划(AMAP_KEY)。仅"从A去B"(有出发地)时出真实文字方案，否则只开地图 ——
def _amap_get(url: str):
    op = _urlreq.build_opener(_urlreq.ProxyHandler({}))  # 禁代理直连
    return _json.load(op.open(url, timeout=5))


def _amap_locate(addr: str, key: str, city: str = ""):
    """地标名 → ("lon,lat", 城市名)。用 POI 搜索(对"五道口/清华大学"等地标比 geocode 准得多——
    geocode 会把它们乱配到信阳/镇江，POI 按知名度排返回北京)。city 非空则限定该城市。失败 (None,None)。"""
    try:
        url = (f"https://restapi.amap.com/v3/place/text?"
               f"keywords={_urlparse.quote(addr)}&offset=1&key={key}")
        if city:
            url += f"&city={_urlparse.quote(city)}&citylimit=true"
        j = _amap_get(url)
        pois = j.get("pois") or []
        if pois and pois[0].get("location"):
            return pois[0]["location"], (pois[0].get("cityname") or "")
    except Exception:  # noqa: BLE001
        pass
    return None, None


def _amap_route(origin: str, destination: str, mode: str) -> str | None:
    """高德路径规划 → 真实文字方案(距离/时间/线路)。守 <5s：失败/超时返回 None(只开地图、不阻塞)。
    安全：短途方式(地铁/步行/骑行)却跨城 = 定位多半错了 → 返回 None(只开地图，不报错数字)。"""
    key = os.environ.get("AMAP_KEY", "").strip()
    if not key or not origin or not destination:
        return None
    m = mode or ""
    want_transit = any(k in m for k in ("地铁", "公交", "公共交通", "巴士"))
    want_walk = any(k in m for k in ("步行", "走路", "走着", "散步"))
    want_bike = any(k in m for k in ("骑", "单车", "自行车"))
    short_mode = want_transit or want_walk or want_bike
    o_loc, o_city = _amap_locate(origin, key)
    # 终点定位策略按 mode 取舍：短途(地铁/步行/骑行)=同城→用起点城市找本地同名点(治"国贸"配到外地)；
    # 驾车/未指定→找最知名(治"天安门"被 citylimit 配成石家庄本地同名 POI，应得北京天安门、可跨城)。
    if short_mode:
        d_loc, d_city = _amap_locate(destination, key, city=o_city)
        if not d_loc:
            d_loc, d_city = _amap_locate(destination, key)
    else:
        d_loc, d_city = _amap_locate(destination, key)
    if not o_loc or not d_loc:
        return None
    if short_mode and o_city and d_city and o_city != d_city:
        return None  # 短途方式却跨城=定位错，只开地图不给错数字
    try:
        if want_transit and o_city:
            j = _amap_get(f"https://restapi.amap.com/v3/direction/transit/integrated?"
                          f"origin={o_loc}&destination={d_loc}"
                          f"&city={_urlparse.quote(o_city)}&key={key}")
            transits = (j.get("route") or {}).get("transits") or []
            if transits:
                t0 = transits[0]
                mins = int(int(t0.get("duration", 0)) / 60)
                cost = t0.get("cost", "")
                lines = [
                    nm.split("(")[0]
                    for seg in t0.get("segments", [])
                    for bl in (seg.get("bus") or {}).get("buslines", [])[:1]
                    if (nm := (bl.get("name", "") or ""))
                ]
                s = f"公交/地铁约{mins}分钟" + (f"、{cost}元" if cost else "")
                return s + ("，" + "→".join(lines) if lines else "")
        if want_walk or want_bike:
            j = _amap_get(f"https://restapi.amap.com/v3/direction/walking?"
                          f"origin={o_loc}&destination={d_loc}&key={key}")
            paths = (j.get("route") or {}).get("paths") or []
            if paths:
                km = round(int(paths[0].get("distance", 0)) / 1000, 1)
                mins = int(int(paths[0].get("duration", 0)) / 60)
                if km > 50:  # 步行/骑行不该这么远 → 定位多半错，只开地图
                    return None
                return (f"骑行约{km}公里、{max(1, mins // 3)}分钟" if want_bike
                        else f"步行约{km}公里、{mins}分钟")
        # 驾车（默认；同城跨城都稳）
        j = _amap_get(f"https://restapi.amap.com/v3/direction/driving?"
                      f"origin={o_loc}&destination={d_loc}&key={key}")
        paths = (j.get("route") or {}).get("paths") or []
        if paths:
            p = paths[0]
            km = round(int(p.get("distance", 0)) / 1000, 1)
            mins = int(int(p.get("duration", 0)) / 60)
            tolls = p.get("tolls", "0")
            return (f"驾车约{km}公里、{mins}分钟"
                    + (f"，过路费约{tolls}元" if tolls and tolls not in ("0", "0.00") else ""))
    except Exception:  # noqa: BLE001
        return None
    return None


def _get_session(user_id: str) -> InteractionSession:
    """每个 user_id 一个持久会话（保留对话历史/待确认动作生命周期）。"""
    with _lock:
        _ensure_cfg()
        sess = _sessions.get(user_id)
        if sess is None:
            # terminal_mode=mock：服务端只决策，真执行交安卓端；天气除外→真实和风
            sess = InteractionSession(_cfg, user_id=user_id, speak=False,
                                      terminal_mode="mock")
            sess.executor.terminal = MobileTerminal()
            _sessions[user_id] = sess
    return sess


def _get_session_lock(user_id: str) -> threading.RLock:
    with _lock:
        return _session_locks.setdefault(user_id, threading.RLock())


def _new_opaque_id(prefix: str) -> str:
    return f"{prefix}_{uuid.uuid4().hex}"


def _ensure_pending_contract(pending: dict, *, rotate_window: bool = False) -> dict:
    """Attach opaque one-shot IDs without changing the immutable action snapshot."""
    pending.setdefault("pending_id", _new_opaque_id("pending"))
    if rotate_window or not pending.get("confirmation_window_id"):
        pending["confirmation_window_id"] = _new_opaque_id("window")
    pending.setdefault("seen_event_ids", [])
    return pending


def _remember_terminal_pending(user_id: str, pending: dict | None) -> None:
    pending_id = (pending or {}).get("pending_id")
    if not pending_id:
        return
    history = _terminal_pending_ids.setdefault(user_id, [])
    if pending_id not in history:
        history.append(pending_id)
        del history[:-64]


def _pending_response_fields(pending: dict | None) -> dict:
    return {
        "pending_id": (pending or {}).get("pending_id"),
        "confirmation_window_id": (pending or {}).get("confirmation_window_id"),
    }


def _replace_pending_action(sess: InteractionSession, pending: dict | None) -> None:
    """手机适配层替换 pending 时同步重置外部会话的老化计数。"""
    if pending is not None:
        _ensure_pending_contract(pending)
    sess.context.pending_action = pending
    # InteractionSession 只会在自身 process() 新建 pending 时自动归零；本适配层的
    # 确定性短信入口绕过了 process()，若不显式归零，新动作会继承上一动作的年龄。
    if hasattr(sess, "_pending_age"):
        sess._pending_age = 0


class VisionMark(BaseModel):
    kind: str  # head_motion / gesture / expression
    label: str  # nod / shake / thumb_up / positive ...
    confidence: float = 0.9
    # 视觉事件在统一时间轴上的起止（秒）。端侧带真实时间戳→做真·时间对齐（overlap/relation）；
    # 缺省（旧端/纯文本）→ _build_frame 退回固定占位窗，行为不变。
    t_start: float | None = None
    t_end: float | None = None
    event_id: str | None = None
    window_id: str | None = None
    stable_count: int | None = None
    localized: bool | None = None
    quality: str | None = None


class CloudRequest(BaseModel):
    request_id: str = Field(
        default="",
        description="Cloud-2幂等请求ID；安全模式下必须是规范UUID，且与pending/window/event ID分离",
    )


class CloudScreenshotRequest(CloudRequest):
    """Cloud-3 evidence required whenever a request contains a screenshot."""
    cloud_vision_consent: bool = Field(
        default=False,
        description="用户是否在Android独立开启了必要界面截图上云；默认关闭",
    )
    cloud_vision_policy: str = Field(default="", description="Android截图隐私策略版本")
    screenshot_purpose: str = Field(default="", description="本次截图的唯一用途")
    screen_sensitivity: str = Field(default="unknown", description="clear/sensitive/unknown")
    screenshot_sha256: str = Field(default="", description="本请求PNG字节的SHA-256")


class InteractRequest(CloudRequest):
    user_id: str = "default"
    text: str = Field(..., description="ASR 识别出的用户话语文本")
    vision: list[VisionMark] = Field(default_factory=list, description="端侧视觉证据摘要")
    screen: dict | None = Field(default=None, description="当前界面上下文")
    gaze_target: str | None = Field(default=None, description="注视焦点（屏幕指代消解）")
    # 话语在统一时间轴上的起止（秒）。端侧 ASR 出 utterance 时长时带上，做模态异步对齐。
    speech_t_start: float | None = Field(default=None, description="话语起点（秒，统一时间轴）")
    speech_t_end: float | None = Field(default=None, description="话语终点（秒，统一时间轴）")
    pending_id: str | None = Field(default=None, description="确认轮绑定的不可变待执行动作 ID")
    confirmation_window_id: str | None = Field(default=None, description="确认轮绑定的窗口 ID")
    confirmation_source: str | None = Field(default=None, description="spoken/typed/vision_only/*_visual")
    apps: list[str] = Field(default_factory=list,
                            description="本机真实已装应用，每项 'label\\t包名'，用于 open_app 解析")


# user_id → [(label, 包名)]，缓存各端最近上报的已装清单（open_app 解析用，不必每轮重发也可命中）
_apps_cache: dict[str, list[tuple[str, str]]] = {}

# T-17 ⑤：澄清轮丢上下文（呆案例 #001-#003）——open_app/run_agent 因缺 App 名被追问"哪个应用"时，
# 原始整句里的目标（"日推第三首"）没地方存；用户只答了个 App 名，若不重新说完整句，目标就丢了。
# mobile-only 极简状态（不碰 PC DialogueContext）：记住"这轮问的是哪句话"，下一轮短答自动拼回去
# 再走一次完整路由。只针对 open_app/run_agent 缺 App 名这一种澄清，不吞掉其他澄清场景。
_pending_app_task: dict[str, dict] = {}
_PENDING_APP_MAX_TURNS = 2  # 超过这么多轮没等到短答就作废，避免误吞后续无关对话

_llm = None
_agent_llm = None
_AGENT_LLM_TIMEOUT_SECONDS = 12.0


def _get_llm():
    """共享 LLMClient（单例，带手机设备能力档配置）。供 open_app 解析与 ③ agent_step 复用。"""
    global _llm
    if _llm is None:
        from lingxi.llm import LLMClient
        _llm = LLMClient(_ensure_cfg())
    return _llm


def _get_agent_llm():
    """Agent规划/决策/复核专用14B客户端。

    普通对话在Railway继续执行4秒硬截止；Agent入口已由确定性白名单收口后，后续界面规划
    使用独立12秒单次预算。仍保持传输重试为0，避免一步网络失败叠加成不可控长尾。
    """
    global _agent_llm
    if _agent_llm is None:
        from lingxi.llm import LLMClient

        timeout = _AGENT_LLM_TIMEOUT_SECONDS
        if os.environ.get("LINGXI_DEPLOYMENT_STAGE", "").strip() == "railway":
            raw_timeout = os.environ.get(
                "LINGXI_AGENT_LLM_REQUEST_TIMEOUT_SECONDS", ""
            ).strip()
            if raw_timeout != "12.0":
                raise RuntimeError(
                    "Railway Agent LLM request timeout must be the frozen 12.0-second budget"
                )
        agent_raw = copy.deepcopy(_ensure_cfg().raw())
        llm_cfg = agent_raw.setdefault("llm", {})
        llm_cfg["request_timeout"] = timeout
        llm_cfg["hard_deadline"] = timeout
        llm_cfg["max_retries"] = 0
        _agent_llm = LLMClient(Config(agent_raw))
    return _agent_llm


def _parse_apps(raw: list[str]) -> list[tuple[str, str]]:
    out: list[tuple[str, str]] = []
    for s in raw:
        if "\t" in s:
            label, pkg = s.split("\t", 1)
            if label.strip() and pkg.strip():
                out.append((label.strip(), pkg.strip()))
    return out


def _resolve_app(app_name: str, apps: list[tuple[str, str]]) -> tuple[str, str] | None:
    """口语应用名 → (真实label, 包名)。①label精确 ②双向包含/包名含 ③LLM世界知识兜底(b站→bilibili)。"""
    if not apps or not app_name.strip():
        return None
    q = app_name.strip().lower()
    for label, pkg in apps:
        if label.lower() == q:
            return (label, pkg)
    for label, pkg in apps:
        l = label.lower()
        if (q in l) or (len(l) >= 2 and l in q) or (q in pkg.lower()):
            return (label, pkg)
    return _llm_resolve_app(app_name, apps)


def _llm_resolve_app(app_name: str, apps: list[tuple[str, str]]) -> tuple[str, str] | None:
    """字符串匹配不中时，让 LLM 用世界知识从【真实已装清单】里挑（治中英标签错位、新装App免维护别名）。"""
    labels = [label for label, _ in apps]
    sys_p = ("你从【已装应用清单】里挑出用户想打开的那一个。只输出清单里那一项的【原样名称】，"
             "无匹配则输出 NONE，禁止任何解释。")
    user_p = f"想打开：{app_name}\n已装应用：{labels}"
    try:
        pick = _get_llm().chat(
            [{"role": "system", "content": sys_p}, {"role": "user", "content": user_p}],
            temperature=0.0, max_tokens=32).strip()
    except Exception:  # noqa: BLE001 — 解析失败不阻塞，交安卓端别名表兜底
        return None
    if not pick or pick.upper() == "NONE":
        return None
    for label, pkg in apps:
        if label.lower() == pick.lower():
            return (label, pkg)
    p = pick.lower()
    for label, pkg in apps:  # LLM 可能略改写 → 再包含匹配一次
        if p in label.lower() or label.lower() in p:
            return (label, pkg)
    return None


def _build_frame(req: InteractRequest) -> MultimodalFrame:
    """文本 + 视觉证据摘要 → MultimodalFrame（与 chat_demo 同构）。

    端侧带真实时间戳（话语起止 + 每个视觉事件起止）时，用真实值做窗口对齐——
    aggregate_vision 据此算出 overlap_ratio 与 before/during/after 关系（赛题"模态异步对齐"）。
    缺省（旧端/纯文本/未带时间戳的视觉）退回固定占位窗，老行为零回归。
    """
    if req.speech_t_start is not None and req.speech_t_end is not None:
        t0, t1 = float(req.speech_t_start), float(req.speech_t_end)
    else:
        t0, t1 = 10.0, 12.0  # 占位窗：纯文本/旧端无时间轴时的兜底
    events: list[VisionEvent] = []
    for m in req.vision:
        try:
            kind = VisionKind(m.kind)
        except ValueError:
            continue  # 未知视觉类型跳过，不让脏数据炸管线
        # 视觉事件时间：优先端侧真实起止；缺省落在话语窗内（保持旧版"during"行为）
        ev_start = float(m.t_start) if m.t_start is not None else t0 + 0.3
        ev_end = float(m.t_end) if m.t_end is not None else t1 - 0.2
        events.append(VisionEvent(t_start=ev_start, t_end=ev_end,
                                  kind=kind, label=m.label, confidence=m.confidence))
    anchor = SpeechUtterance(t_start=t0, t_end=t1, text=req.text.strip())
    summary = aggregate_vision(events, t0, t1)
    frame = MultimodalFrame(anchor=anchor, vision_events=events,
                            vision_summary=summary, precheck=precheck(anchor, summary))
    if events and not _secure_api:  # secure日志不得包含话语、视觉或生物特征摘要
        rels = ", ".join(
            f"{s['kind']}:{s['label']}({s['relation']},ov={s['overlap_ratio']},c={s['confidence']})"
            for s in summary)
        print(f"[multimodal] 话语窗[{t0:.2f},{t1:.2f}]「{anchor.text}」 视觉=[{rels}] "
              f"预检={frame.precheck['verdict']}（语音{frame.precheck['speech_polarity']}/"
              f"视觉{frame.precheck['vision_polarity']}）", flush=True)
    return frame


def _device_action(intent: str, slots: dict, status: str,
                   pending_before: dict | None) -> dict | None:
    """归一化出安卓端要在手机上真执行的动作（两段式确认流收口在这里）。"""
    if status != "executed":
        return None
    if intent == "confirm" and pending_before:
        return {"type": pending_before.get("type"),
                "slots": pending_before.get("slots", {})}
    if intent in DEVICE_INTENTS:
        return {"type": intent, "slots": slots}
    return None


def _is_confirmation_retry(text: str) -> bool:
    """待确认态里的"再来一次/重试"是在请求重开采集窗，不是确认执行。"""
    compact = re.sub(r"\s+", "", text or "")
    if not compact:
        return False
    # 如果带了明确动作词，交给正常意图理解；这里只挡截图里这种纯重试话术。
    if any(k in compact for k in ("确认", "发送", "发出", "拨打", "取消", "不要", "发吧", "打吧")):
        return False
    return compact in {
        "再来一次", "再试一次", "重来一次", "重新来一次", "重试一下", "重试",
        "重新识别", "没看清", "没识别到", "没有识别到", "再采一次", "再看一次",
    }


def _is_explicit_refusal(text: str) -> bool:
    """确认轮中的明确口头否定；宁可取消，也不让其他模态覆盖后误执行高风险动作。"""
    compact = re.sub(r"\s+", "", text or "")
    return any(k in compact for k in (
        "不要", "不用", "取消", "别发", "不发", "不要发", "不用发", "别发送",
        "取消发送", "别打", "不要打", "不用打", "停止", "算了", "不确认",
    ))


def _is_explicit_high_risk_confirmation(text: str) -> bool:
    """高风险动作只接受不易与“不要”混淆的明确授权，拒绝单字“要/好/嗯”。"""
    compact = re.sub(r"\s+", "", text or "")
    return any(k in compact for k in (
        "确认发送", "确认执行", "确认拨打", "确认发出", "可以发送", "可以拨打",
        "同意发送", "同意拨打", "发出去", "发送吧", "拨打吧", "确认",
    ))


def _is_parameter_change(text: str) -> bool:
    return bool(_PARAMETER_CHANGE_RE.search(re.sub(r"\s+", "", text or "")))


def _is_pending_context_question(text: str) -> bool:
    return bool(_PENDING_CONTEXT_QUESTION_RE.search(re.sub(r"\s+", "", text or "")))


def _is_ambiguous_confirmation_answer(text: str) -> bool:
    compact = re.sub(r"\s+", "", text or "")
    return compact in {
        "", "要", "好", "好的", "嗯", "嗯嗯", "行", "可以", "可以吧", "就这样",
        "继续", "随便", "都行", "没问题",
    }


def _is_explicit_unrelated_new_task(req: InteractRequest) -> bool:
    """Recognize a complete task carried inside an otherwise valid confirm window.

    KWS necessarily labels speech captured during that window as ``spoken``.  The
    source describes where the evidence came from; it must not turn an explicit
    weather/query/device command into an answer to the old pending action.
    """
    if req.confirmation_source not in {"spoken", "typed", "spoken_visual", "typed_visual"}:
        return False
    compact = re.sub(r"\s+", "", req.text or "")
    if not compact:
        return False
    if (
        _is_explicit_refusal(compact)
        or _is_explicit_high_risk_confirmation(compact)
        or _is_parameter_change(compact)
        or _is_pending_context_question(compact)
        or _is_confirmation_retry(compact)
        or _is_ambiguous_confirmation_answer(compact)
        or any(mark.label in VISUAL_REJECT_LABELS for mark in req.vision)
    ):
        return False
    return bool(
        _mobile_system_shortcut(compact)
        or _mobile_send_message_request(compact)
        or _EXPLICIT_UNRELATED_NEW_TASK_RE.search(compact)
    )


def _contract_response(
    *,
    reply: str,
    intent: str,
    status: str,
    decision_reason: str,
    pending: dict | None = None,
    conflict: bool = False,
    conflict_resolution: str = "",
    need_clarification: bool = False,
    clarify_question: str = "",
    device_action: dict | None = None,
    slots: dict | None = None,
) -> dict:
    return {
        "reply": reply,
        "intent": intent,
        "slots": slots or {},
        "status": status,
        "confidence": 0.99,
        "conflict": conflict,
        "conflict_resolution": conflict_resolution,
        "need_clarification": need_clarification,
        "clarify_question": clarify_question,
        "card": None,
        "device_action": device_action,
        "decision_reason": decision_reason,
        **_pending_response_fields(pending),
        "latency": {"intent": 0.0, "reply": 0.0, "total": 0.0},
    }


def _visual_support_threshold() -> float:
    return float(_ensure_cfg().get("intent.visual_support_threshold", 0.75))


def _vision_only_authorization() -> tuple[bool, float | None]:
    enabled = bool(_ensure_cfg().get("intent.vision_only_enabled", False))
    raw = _ensure_cfg().get("intent.visual_authorize_threshold", None)
    if raw in (None, ""):
        return False, None
    threshold = float(raw)
    if threshold <= _visual_support_threshold():
        return False, threshold
    return enabled, threshold


def _confirmation_envelope_error(req: InteractRequest, pending: dict) -> str | None:
    source = (req.confirmation_source or "").strip()
    if source not in CONFIRMATION_SOURCES or source == "unknown":
        return "SOURCE_MISMATCH"
    if req.pending_id != pending.get("pending_id"):
        return "PENDING_ID_MISMATCH"
    if req.confirmation_window_id != pending.get("confirmation_window_id"):
        return "WINDOW_ID_MISMATCH"

    has_visual = bool(req.vision)
    if source in {"spoken", "typed"} and has_visual:
        return "SOURCE_MISMATCH"
    if source in {"vision_only", "spoken_visual", "typed_visual"} and not has_visual:
        return "SOURCE_MISMATCH"
    if source in {"spoken", "typed", "spoken_visual", "typed_visual"} and not req.text.strip():
        return "SOURCE_MISMATCH"

    seen = set(pending.get("seen_event_ids", []))
    request_ids: set[str] = set()
    for mark in req.vision:
        if not mark.event_id:
            return "INCOMPLETE_VISUAL_EVIDENCE"
        if mark.event_id in seen or mark.event_id in request_ids:
            return "REPLAY"
        request_ids.add(mark.event_id)
        if mark.window_id != pending.get("confirmation_window_id"):
            return "WINDOW_ID_MISMATCH"
        if (
            mark.stable_count is None
            or mark.stable_count < 1
            or mark.t_start is None
            or mark.t_end is None
            or mark.t_start < 0
            or mark.t_end < mark.t_start
            or mark.confidence < 0
            or mark.confidence > 1
        ):
            return "INCOMPLETE_VISUAL_EVIDENCE"
        if mark.quality != "ok":
            return "LOW_VISUAL_CONFIDENCE"
    pending["seen_event_ids"] = [*seen, *request_ids]
    return None


def _strong_visual(req: InteractRequest, labels: set[str], threshold: float) -> bool:
    return any(
        mark.label in labels
        and mark.confidence >= threshold
        and (mark.stable_count or 0) >= VISUAL_MIN_STABLE_COUNT
        and mark.localized is True
        and mark.quality == "ok"
        for mark in req.vision
    )


def _weak_visual_reply(
    req: InteractRequest,
    labels: set[str],
    threshold: float,
    *,
    polarity: str,
) -> str:
    candidates = [mark for mark in req.vision if mark.label in labels]
    if not candidates:
        return f"视觉{polarity}证据不足，本次没有执行。"
    best = max(candidates, key=lambda mark: (mark.stable_count or 0, mark.confidence))
    stable_count = best.stable_count or 0
    if stable_count < VISUAL_MIN_STABLE_COUNT:
        return (
            f"识别到 {best.label}，但仅稳定 {stable_count} 帧，"
            f"至少需要 {VISUAL_MIN_STABLE_COUNT} 帧；本次没有执行。"
        )
    return (
        f"识别到 {best.label}，稳定 {stable_count} 帧，但置信度 "
        f"{best.confidence:.2f} 低于当前支持阈值 {threshold:.2f}；本次没有执行。"
    )


def _cancel_pending(
    req: InteractRequest,
    sess: InteractionSession,
    pending: dict,
    *,
    reason: str,
    conflict: bool = False,
    resolution: str = "",
) -> dict:
    summary = pending.get("summary", "当前高风险操作")
    _remember_terminal_pending(req.user_id, pending)
    _replace_pending_action(sess, None)
    return _contract_response(
        reply=f"已取消{summary}，本次没有执行。",
        intent="cancel",
        status="cancelled",
        decision_reason=reason,
        conflict=conflict,
        conflict_resolution=resolution,
    )


def _invalidate_pending(
    req: InteractRequest,
    sess: InteractionSession,
    pending: dict,
    *,
    reason: str,
    reply: str,
) -> dict:
    _remember_terminal_pending(req.user_id, pending)
    _replace_pending_action(sess, None)
    return _contract_response(
        reply=reply,
        intent="clarify",
        status="invalidated",
        decision_reason=reason,
        need_clarification=True,
        clarify_question="请重新说出完整、可独立理解的指令。",
    )


def _preserve_pending(
    sess: InteractionSession,
    pending: dict,
    *,
    reply: str,
    reason: str,
    intent: str = "clarify",
    conflict: bool = False,
    resolution: str = "",
) -> dict:
    if hasattr(sess, "_pending_age"):
        sess._pending_age += 1
    return _contract_response(
        reply=reply,
        intent=intent,
        status="pending_confirmation",
        decision_reason=reason,
        pending=pending,
        conflict=conflict,
        conflict_resolution=resolution,
        need_clarification=True,
        clarify_question="请明确确认或取消；也可以询问当前动作内容。",
    )


def _execute_high_risk_pending(
    req: InteractRequest, sess: InteractionSession, pending: dict, *, reason: str
) -> dict:
    action = {"type": pending.get("type"), "slots": dict(pending.get("slots", {}))}
    _remember_terminal_pending(req.user_id, pending)
    _replace_pending_action(sess, None)
    return _contract_response(
        reply=f"已确认{pending.get('summary', '当前操作')}，交由手机执行。",
        intent="confirm",
        status="executed",
        decision_reason=reason,
        device_action=action,
        slots=action["slots"],
    )


def _handle_high_risk_pending_confirmation(
    req: InteractRequest, sess: InteractionSession, pending: dict
) -> dict:
    """Final Brain-side authorization gate. Caller holds the per-user lock."""
    text_refusal = _is_explicit_refusal(req.text)
    visual_positive_hint = any(m.label in VISUAL_ACCEPT_LABELS for m in req.vision)
    if text_refusal and req.confirmation_source != "vision_only":
        return _cancel_pending(
            req,
            sess,
            pending,
            reason="EXPLICIT_TEXT_REJECTION",
            conflict=visual_positive_hint,
            resolution=(
                "明确文字或语音拒绝优先，视觉肯定不能覆盖取消。"
                if visual_positive_hint else ""
            ),
        )

    error = _confirmation_envelope_error(req, pending)
    if error:
        return _preserve_pending(
            sess,
            pending,
            reply="确认来源、动作或窗口无法验证，本次没有执行。请重新确认。",
            reason=error,
        )

    support_threshold = _visual_support_threshold()
    visual_reject = _strong_visual(req, VISUAL_REJECT_LABELS, support_threshold)
    text_confirm = _is_explicit_high_risk_confirmation(req.text)
    if visual_reject:
        return _cancel_pending(
            req,
            sess,
            pending,
            reason="EXPLICIT_VISUAL_REJECTION",
            conflict=text_confirm,
            resolution=(
                "口头或文字肯定与可靠视觉否定冲突，按明确拒绝终止。"
                if text_confirm else ""
            ),
        )

    source = req.confirmation_source
    if text_confirm and source in {"spoken", "typed", "spoken_visual", "typed_visual"}:
        return _execute_high_risk_pending(req, sess, pending, reason="EXPLICIT_TEXT_CONFIRMATION")

    if source == "vision_only":
        labels = {mark.label for mark in req.vision}
        if labels & VISUAL_ACCEPT_LABELS:
            if not _strong_visual(req, VISUAL_ACCEPT_LABELS, support_threshold):
                return _preserve_pending(
                    sess,
                    pending,
                    reply=_weak_visual_reply(
                        req, VISUAL_ACCEPT_LABELS, support_threshold, polarity="肯定",
                    ),
                    reason="LOW_VISUAL_CONFIDENCE",
                )
        elif labels & VISUAL_REJECT_LABELS:
            return _preserve_pending(
                sess,
                pending,
                reply=_weak_visual_reply(
                    req, VISUAL_REJECT_LABELS, support_threshold, polarity="否定",
                ),
                reason="LOW_VISUAL_CONFIDENCE",
            )
        else:
            return _preserve_pending(
                sess,
                pending,
                reply="该视觉事件不在高风险确认白名单中，本次没有执行。",
                reason="UNSUPPORTED_VISUAL_EVENT",
            )
        enabled, threshold = _vision_only_authorization()
        if threshold is not None and _strong_visual(req, VISUAL_ACCEPT_LABELS, threshold) and enabled:
            return _execute_high_risk_pending(req, sess, pending, reason="AUTHORIZED_VISUAL_CONFIRMATION")
        reason = "VISION_ONLY_DISABLED" if not enabled else "LOW_VISUAL_CONFIDENCE"
        return _preserve_pending(
            sess,
            pending,
            reply="视觉确认目前只记录不执行，请改用明确语音或文字确认。",
            reason=reason,
        )

    return _preserve_pending(
        sess,
        pending,
        reply="当前回答不足以授权高风险操作，请明确确认或取消。",
        reason="AMBIGUOUS_CONFIRMATION",
    )


def _high_risk_refusal_response(req: InteractRequest, pending_before: dict | None) -> dict | None:
    """在外部融合/执行之前拦截“明确否定 + 高风险 pending”，保证 device_action 永远为空。"""
    if not pending_before or pending_before.get("type") not in HIGH_RISK_PENDING_TYPES:
        return None
    if not _is_explicit_refusal(req.text):
        return None
    summary = pending_before.get("summary", "当前高风险操作")
    has_visual = bool(req.vision)
    return {
        "reply": f"已取消{summary}。明确口头否定优先，本次未执行。",
        "intent": "cancel",
        "slots": {},
        "status": "executed",
        "confidence": 0.99,
        "conflict": has_visual,
        "conflict_resolution": (
            "高风险硬守卫：明确口头否定优先，肯定动作不构成执行授权。" if has_visual else ""
        ),
        "need_clarification": False,
        "clarify_question": "",
        "card": None,
        "device_action": None,
        "latency": {"intent": 0.0, "reply": 0.0, "total": 0.0},
    }


def _high_risk_ambiguous_confirmation_response(
    req: InteractRequest, pending_before: dict | None
) -> dict | None:
    """高风险 pending 中的含糊肯定不执行，避免 ASR 把“不要”截成“要”后误授权。"""
    if not pending_before or pending_before.get("type") not in HIGH_RISK_PENDING_TYPES:
        return None
    if _is_explicit_refusal(req.text) or _is_explicit_high_risk_confirmation(req.text):
        return None
    summary = pending_before.get("summary", "当前高风险操作")
    has_visual = bool(req.vision)
    return {
        "reply": (
            f"为防止误操作，我没有执行{summary}。"
            "请明确说“确认发送/确认执行”，或说“取消”。"
        ),
        "intent": "clarify",
        "slots": {},
        "status": "pending_confirmation",
        "confidence": 0.99,
        "conflict": has_visual,
        "conflict_resolution": (
            "高风险可信决策：单字或含糊肯定不构成执行授权，保留待确认并重新询问。"
            if has_visual else ""
        ),
        "need_clarification": True,
        "clarify_question": "请明确说“确认发送/确认执行”，或说“取消”。",
        "card": None,
        "device_action": None,
        "latency": {"intent": 0.0, "reply": 0.0, "total": 0.0},
    }


def _orphan_confirmation_response(
    req: InteractRequest, pending_before: dict | None
) -> dict | None:
    """Any orphan confirmation envelope must fail closed before ordinary routing/LLM."""
    if pending_before is not None:
        return None
    has_envelope = any((
        req.confirmation_source is not None,
        req.pending_id is not None,
        req.confirmation_window_id is not None,
    ))
    if not has_envelope and not _is_explicit_high_risk_confirmation(req.text):
        return None
    replay = bool(
        req.pending_id and req.pending_id in _terminal_pending_ids.get(req.user_id, [])
    )
    source = (req.confirmation_source or "").strip()
    if replay:
        reason = "REPLAY"
    elif has_envelope and (source not in CONFIRMATION_SOURCES or source == "unknown"):
        reason = "SOURCE_MISMATCH"
    else:
        reason = "ORPHAN"
    return _contract_response(
        reply="当前没有有效的待确认短信或电话，本次没有执行。请重新说完整指令。",
        intent="clarify",
        status="refused",
        decision_reason=reason,
        need_clarification=True,
        clarify_question="请重新说完整的联系人和操作内容。",
    )


def _expired_high_risk_confirmation_response(
    req: InteractRequest,
    pending_before: dict | None,
    intent: str,
    status: str,
) -> dict | None:
    """外部会话若已让确认窗超时，适配层必须覆盖其 chat/LLM 假完成文案。"""
    if not pending_before or pending_before.get("type") not in HIGH_RISK_PENDING_TYPES:
        return None
    if not _is_explicit_high_risk_confirmation(req.text):
        return None
    if intent == "confirm" and status == "executed":
        return None
    summary = pending_before.get("summary", "上一项高风险操作")
    return {
        "reply": f"{summary}的确认窗口已超时，本次没有执行。请重新说完整指令。",
        "intent": "clarify",
        "slots": {},
        "status": "clarify",
        "confidence": 0.99,
        "conflict": False,
        "conflict_resolution": "",
        "need_clarification": True,
        "clarify_question": "请重新说完整的联系人和操作内容。",
        "card": None,
        "device_action": None,
        "latency": {"intent": 0.0, "reply": 0.0, "total": 0.0},
    }


def _honest_confirmation_reply(reply: str, status: str, heard_text: str) -> str:
    """确认文案必须与机器状态一致，禁止 chat/clarify 伪装成已进入确认态。"""
    if status != "pending_confirmation" and any(
        phrase in reply for phrase in ("等待用户确认", "等待确认", "请确认后发送")
    ):
        return (
            f"我只听到「{heard_text}」，还没有进入确认状态。"
            "请把联系人和短信内容完整说一遍。"
        )
    return reply


@app.get("/live")
def live() -> dict:
    """Public minimal liveness: no provider, version, session, or dependency detail."""
    return {"status": "alive"}


@app.get("/ready")
def ready() -> dict:
    """Detailed readiness; CloudSecurityMiddleware authenticates this in secure mode."""
    try:
        return cloud_readiness(SECURITY_CONFIG)
    except (AuthConfigurationError, OSError, RuntimeError) as exc:
        raise HTTPException(
            status_code=503,
            detail={"status": "not_ready", "reason": "SECURITY_DEPENDENCY_UNAVAILABLE"},
        ) from exc


@app.get("/health")
def health() -> dict:
    """不调 LLM 的详细健康检查；安全模式下必须先通过设备认证。"""
    cfg = load_config()
    return {
        "status": "ok",
        "service": "lingxi-mobile-brain",
        "llm_provider": cfg.get("llm.provider", "?"),
        "active_sessions": len(_sessions),
    }


@app.post("/interact")
def interact(req: InteractRequest) -> dict:
    """Serialize one user's state transition so IDs and pending consumption stay atomic."""
    with _get_session_lock(req.user_id):
        return _interact_locked(req)


def _interact_locked(req: InteractRequest) -> dict:
    """单轮多模态交互：文本+视觉证据 → 意图/应答/待执行设备动作。"""
    sess = _get_session(req.user_id)
    if req.screen is not None:
        sess.context.screen = req.screen
    if req.gaze_target is not None and hasattr(sess.context, "gaze_target"):
        sess.context.gaze_target = req.gaze_target
    if req.apps:  # 缓存本端已装清单（供 open_app 解析）
        _apps_cache[req.user_id] = _parse_apps(req.apps)

    pending_before = sess.context.pending_action
    invalidated_for_new_task = False
    if pending_before and pending_before.get("type") in HIGH_RISK_PENDING_TYPES:
        _ensure_pending_contract(pending_before)

    # A confirmation packet is never a new task. When no pending exists, reject its
    # source/IDs before deterministic parsers or the ordinary LLM can recreate history.
    if guarded := _orphan_confirmation_response(req, pending_before):
        return guarded

    # 完整的新短信请求建立全新的不可变快照；旧 pending 只失效，不继承任何槽位。
    if message := _mobile_send_message_request(req.text):
        if pending_before is not None:
            _remember_terminal_pending(req.user_id, pending_before)
        new_pending = {
            "type": "send_message",
            "summary": message["summary"],
            "slots": dict(message["slots"]),
        }
        _replace_pending_action(sess, new_pending)
        return _contract_response(
            reply=f"已准备好{message['summary']}，等待用户确认。",
            intent="send_message",
            status="pending_confirmation",
            decision_reason="NEW_PENDING",
            pending=new_pending,
            slots=message["slots"],
        )

    # KWS/Activity may capture an explicit new task while the old confirmation
    # window is still open.  Validate the envelope first (stale IDs never bypass
    # the safety gate), then atomically invalidate the old snapshot and route the
    # text normally.  Source metadata describes provenance, not user intent.
    if (
        pending_before
        and pending_before.get("type") in HIGH_RISK_PENDING_TYPES
        and _is_explicit_unrelated_new_task(req)
    ):
        if error := _confirmation_envelope_error(req, pending_before):
            return _preserve_pending(
                sess,
                pending_before,
                reply="确认来源、动作或窗口无法验证，本次没有执行。请重新确认。",
                reason=error,
            )
        _remember_terminal_pending(req.user_id, pending_before)
        _replace_pending_action(sess, None)
        pending_before = None
        invalidated_for_new_task = True

    # 高风险 pending 完全由本适配层确定性处理，不再交给 LLM 临场解释。
    if pending_before and pending_before.get("type") in HIGH_RISK_PENDING_TYPES:
        confirmation_like = (
            req.confirmation_source is not None
            or bool(req.vision)
            or _is_explicit_refusal(req.text)
            or _is_explicit_high_risk_confirmation(req.text)
            or _is_ambiguous_confirmation_answer(req.text)
            or _is_confirmation_retry(req.text)
            or _is_pending_context_question(req.text)
        )
        if getattr(sess, "_pending_age", 0) >= sess.max_pending_turns and confirmation_like:
            summary = pending_before.get("summary", "上一项高风险操作")
            _remember_terminal_pending(req.user_id, pending_before)
            _replace_pending_action(sess, None)
            return _contract_response(
                reply=f"{summary}的确认窗口已超时，本次没有执行。请重新说完整指令。",
                intent="clarify",
                status="expired",
                decision_reason="TIMEOUT",
                need_clarification=True,
                clarify_question="请重新说完整的联系人和操作内容。",
            )
        if _is_parameter_change(req.text):
            return _invalidate_pending(
                req,
                sess,
                pending_before,
                reason="PARAMETER_CHANGED",
                reply="动作关键参数已发生变化，之前的确认已失效。请重新说出完整指令。",
            )
        if _is_pending_context_question(req.text):
            if hasattr(sess, "_pending_age"):
                sess._pending_age += 1
            return _contract_response(
                reply=f"当前等待确认的是：{pending_before.get('summary', '当前操作')}。",
                intent="query_pending",
                status="pending_confirmation",
                decision_reason="PENDING_CONTEXT_QUERY",
                pending=pending_before,
            )
        if _is_confirmation_retry(req.text):
            _ensure_pending_contract(pending_before, rotate_window=True)
            if hasattr(sess, "_pending_age"):
                sess._pending_age += 1
            return _contract_response(
                reply=(
                    f"好，重新确认：{pending_before.get('summary', '当前操作')}。"
                    "请明确说确认或取消；视觉单独授权目前仅记录不执行。"
                ),
                intent="clarify",
                status="pending_confirmation",
                decision_reason="CONFIRMATION_RETRY",
                pending=pending_before,
            )
        if (
            req.confirmation_source is not None
            or bool(req.vision)
            or _is_explicit_refusal(req.text)
            or _is_explicit_high_risk_confirmation(req.text)
        ):
            return _handle_high_risk_pending_confirmation(req, sess, pending_before)
        if _is_ambiguous_confirmation_answer(req.text):
            return _preserve_pending(
                sess,
                pending_before,
                reply="当前回答含义不够明确，本次没有执行。请明确确认或取消。",
                reason="AMBIGUOUS_CONFIRMATION",
            )

        # 其余完整输入按新任务处理：先原子失效旧 pending，再进入普通路由。
        _remember_terminal_pending(req.user_id, pending_before)
        _replace_pending_action(sess, None)
        pending_before = None
        invalidated_for_new_task = True

    # T-17 ⑤：上一轮问过"哪个应用"且还没作废 → 本轮大概率是短答（"酷狗音乐"），
    # 合并回原始整句再走完整路由，让"日推第三首"这类目标不再因为一次追问就丢失。
    pending_task = _pending_app_task.get(req.user_id)
    if pending_task is not None:
        pending_task["age"] = pending_task.get("age", 0) + 1
        if pending_task["age"] > _PENDING_APP_MAX_TURNS:
            _pending_app_task.pop(req.user_id, None)
            pending_task = None
    if pending_task is not None and len(req.text.strip()) <= 12:
        req.text = f"{pending_task['text']}，{req.text.strip()}"
        _pending_app_task.pop(req.user_id, None)

    # Agent专用入口：已知App + 低风险应用内任务直接建立run_agent，不让4秒通用意图JSON
    # 成为唯一入口。存在真实视觉证据时仍走完整融合链，避免确定性文本规则覆盖模态冲突。
    direct_agent_apps = [] if req.vision else _safe_direct_agent_apps(req.text)
    if direct_agent_apps:
        slots = {"app": "+".join(direct_agent_apps), "goal": req.text}
        reply = f"好的，我在手机上边看边操作：{req.text}"
        sess.context.add_turn("user", req.text)
        sess.context.add_turn("assistant", reply)
        return _contract_response(
            reply=reply,
            intent="run_agent",
            slots=slots,
            status="executed",
            decision_reason=(
                "NEW_TASK" if invalidated_for_new_task else "DETERMINISTIC_AGENT_ENTRY"
            ),
            device_action=_device_action("run_agent", slots, "executed", pending_before),
        )

    # P1 系统确定性能力：相机/计时器/设置直达/手电/日历等手机通用动作不必走 LLM。
    if shortcut := _mobile_system_shortcut(req.text):
        intent = shortcut["intent"]
        slots = shortcut.get("slots", {})
        return {
            "reply": shortcut.get("reply", "好的。"),
            "intent": intent,
            "slots": slots,
            "status": "executed",
            "confidence": 0.99,
            "conflict": False,
            "conflict_resolution": "",
            "need_clarification": False,
            "clarify_question": "",
            "card": None,
            "device_action": {"type": intent, "slots": slots},
            "decision_reason": "NEW_TASK" if invalidated_for_new_task else "SYSTEM_SHORTCUT",
            "pending_id": None,
            "confirmation_window_id": None,
            "latency": {"intent": 0.0, "reply": 0.0, "total": 0.0},
        }

    result = sess.process(_build_frame(req))

    intent = result.intent.intent
    slots = result.intent.slots
    status = result.outcome.status
    current_pending = sess.context.pending_action
    if (
        status == "pending_confirmation"
        and current_pending
        and current_pending.get("type") in HIGH_RISK_PENDING_TYPES
    ):
        _ensure_pending_contract(current_pending)

    # pending_before 是进入外部会话前的快照；若外部会话在本轮判定超时并退化成
    # chitchat/chat，绝不能把其自由生成的“已完成”交给手机。清掉过期动作并要求重说。
    if guarded := _expired_high_risk_confirmation_response(
        req, pending_before, intent, status
    ):
        _replace_pending_action(sess, None)
        return guarded

    # 确定性兜底（仅手机端本服务、不碰 PC eval）：open_app 但原话还含"打开之外的动作"(播放/新建/第N首/找…)
    # → 升级为 run_agent。治意图层 temperature 噪声把"打开X并做Y"错抽成只 open_app→只开App就停（用户体感"像跟傻子说话"）。
    upgraded = (intent == "open_app" and status == "executed" and _looks_like_inapp_task(req.text))
    # 🐛 2026-07-03 真机连续两次实锤（Fable5 定位）：模型明明已经把 app 槽抽对了
    # （slots={'app':'酷狗音乐'}），却还输出 need_clarification=True 反问"打开哪个应用"——
    # 复合动作("打开X点击Y栏目")把模型绕晕，自相矛盾地拿着答案又问问题。原升级判据要求
    # status=="executed"，但这种情况 status 是 "clarify"，永远进不了升级分支，直接卡死在
    # 反问循环（用户原话"他连酷狗音乐都忘了是啥了"）。修法：app 槽只要有值 + 原话像 App 内任务，
    # 不管 status 是不是 clarify 都放行升级——不该反问一个模型自己已经填对的槽位。
    if not upgraded and intent == "open_app" and _looks_like_inapp_task(req.text):
        named_app = (slots.get("app") or slots.get("app_name", "")).strip()
        if named_app:
            upgraded = True
            status = "executed"
            sess.context.pending_action = pending_before
            slots = {"app": named_app, "goal": req.text}
    if intent == "send_message" and _looks_like_social_post(req.text):
        upgraded = True
        status = "executed"
        sess.context.pending_action = pending_before
        slots = {"app": slots.get("app") or slots.get("recipient", ""), "goal": req.text}
    # T-17 ⑥：play_music 命中但原话点名了具体三方音乐 App + App 专属动作语义（每日推荐/日推/
    # 听歌识曲等）→ 该走 run_agent 真操作那个 App，不该走泛用 Intent.ACTION_MEDIA_SEARCH（治不到
    # "每日推荐第一首"这种需要具体导航的语义）。真机 benchmark 实测(2026-07-03)同句时而分对
    # 时而分错(T-01/eval/attribution_bench.py K1)，此处收口成确定性规则、不依赖模型自觉。
    if not upgraded and intent == "play_music" and status == "executed":
        named_app = _match_known_app(req.text)
        if named_app and _looks_like_inapp_task(req.text):
            upgraded = True
            slots = {"app": named_app, "goal": req.text}
    # T-17 ①：booking/shopping/media_tv 类任务被压成 web_search/unclear 且没点名具体 App
    # → 确定性 App 建议表兜底为 run_agent（见 _capability_app_suggestion 上方注释）。
    if not upgraded and intent in ("web_search", "unclear"):
        suggested_app = _capability_app_suggestion(req.text)
        if suggested_app:
            upgraded = True
            status = "executed"
            sess.context.pending_action = pending_before
            slots = {"app": suggested_app, "goal": req.text}
    if upgraded:
        slots = {"app": slots.get("app") or slots.get("app_name", ""), "goal": slots.get("goal") or req.text}
        intent = "run_agent"

    # T-17 ⑤：本轮又是"open_app/run_agent 缺 App 名被追问"→ 记下这句话供下一轮短答合并；
    # 只认这一种澄清（缺收件人/缺日期等其他澄清不记录），避免误吞不相关的短答。
    # 已凭有效 App 槽升级为 run_agent 时，本轮澄清已经被确定性解决；不能把刚消费掉的
    # pending 再挂回去，否则下一轮正常话语会被旧任务误吞。
    if not upgraded and intent in ("open_app", "run_agent") and result.intent.need_clarification:
        _pending_app_task[req.user_id] = {"text": req.text, "age": 0}

    device_action = _device_action(intent, slots, status, pending_before)
    # ③ open_app：用本端真实已装清单把口语名解析到真实包名 → 安卓端按包名直开（治 b站→bilibili、新装App）
    if device_action and device_action.get("type") == "open_app":
        resolved = _resolve_app(device_action["slots"].get("app_name", ""),
                                _apps_cache.get(req.user_id, []))
        if resolved:
            label, pkg = resolved
            device_action["slots"] = {**device_action["slots"], "app_name": label, "package": pkg}

    # navigate "从A去B"（有出发地）→ 高德路径规划出【真实】距离/时间/线路，覆盖回复（不靠 LLM 编造）
    reply = (f"好的，我在手机上边看边操作：{req.text}" if upgraded else result.reply)
    # 可信决策口径：回复文案不能声称“等待确认”，而机器状态却仍是 chat/clarify。
    # 真机 ASR 截断时曾出现这种假确认，用户会一直等一个根本不会开启的确认窗。
    reply = _honest_confirmation_reply(reply, status, req.text)
    if intent == "navigate" and slots.get("origin"):
        route = _amap_route(slots["origin"], slots.get("destination", ""), slots.get("mode", ""))
        if route:
            reply = (f"已为你规划{slots['origin']}到{slots.get('destination', '')}的路线："
                     f"{route}。已打开地图看实时路线。")
    return {
        "reply": reply,
        "intent": intent,
        "slots": slots,
        "status": status,
        "confidence": result.intent.confidence,
        "conflict": result.intent.conflict,
        "conflict_resolution": result.intent.conflict_resolution,
        "need_clarification": result.intent.need_clarification,
        "clarify_question": result.intent.clarify_question,
        "card": result.card,
        "device_action": device_action,
        "decision_reason": (
            "NEW_PENDING" if status == "pending_confirmation" and current_pending
            else "NEW_TASK" if invalidated_for_new_task
            else ""
        ),
        **_pending_response_fields(current_pending if status == "pending_confirmation" else None),
        "latency": result.latency,
    }


class AgentPlanRequest(CloudRequest):
    goal: str
    app: str = ""
    skip_demo: bool = False


class MultiAppPlanRequest(CloudRequest):
    goal: str
    apps: list[str] = Field(default_factory=list, description="Optional app allowlist/order from Android installed apps")


def _unique_keep_order(items: list[str]) -> list[str]:
    out = []
    seen = set()
    for x in items:
        x = (x or "").strip()
        if x and x not in seen:
            out.append(x)
            seen.add(x)
    return out


def _extract_product_query(goal: str) -> str:
    text = re.sub(r"\s+", " ", goal or "").strip()
    quoted = re.search(r"[\"“「']([^\"”」']+)[\"”」']", text)
    if quoted:
        return quoted.group(1).strip()
    m = re.search(r"(?:搜索|搜一下|搜|查一下|查)(.+?)(?:比价|价格|哪家|哪个|首屏|销量|评分|最便宜|$)", text)
    if m:
        q = m.group(1)
    else:
        # Product names in our task set often contain latin letters/digits.
        m2 = re.search(r"([A-Za-z][A-Za-z0-9 _\\-]{2,}[A-Za-z0-9])", text)
        q = m2.group(1) if m2 else text
    q = re.sub(r"(淘宝|京东|拼多多|首屏|比价|价格|哪家便宜|哪个便宜|最便宜|帮我|给我|一下|看看)", "", q)
    return q.strip(" ，。,.") or text[:30]


def _extract_route_slots(goal: str) -> dict:
    compact = re.sub(r"\s+", "", goal or "")
    date = "明天" if "明天" in compact else ("后天" if "后天" in compact else ("今天" if "今天" in compact else ""))
    route_text = compact
    if date:
        route_text = route_text.replace(date, "", 1)
    route_text = re.split(r"(?:机票|高铁|火车票|火车|车票|哪家|哪个|最便宜|比价|价格)", route_text, maxsplit=1)[0]
    city = r"(?:北京|上海|广州|深圳|哈尔滨|杭州|南京|成都|重庆|武汉|西安|沈阳|天津)"
    m = re.search(rf"({city})(?:到|去|→|->|-)({city})", route_text)
    if not m:
        m = re.search(rf"({city})({city})", route_text)
    if not m:
        m = re.search(r"([\u4e00-\u9fa5]{2,6})(?:到|去|→|->|-)([\u4e00-\u9fa5]{2,6})", route_text)
    return {
        "date": date or "待确认日期",
        "origin": m.group(1) if m else "待确认出发地",
        "destination": m.group(2) if m else "待确认目的地",
    }


def _shopping_apps(goal: str, requested: list[str]) -> list[str]:
    text = goal or ""
    mentioned = []
    for app_name in ("淘宝", "京东", "拼多多"):
        if app_name in text:
            mentioned.append(app_name)
    requested_hits = [a for a in requested if any(k in a for k in ("淘宝", "京东", "拼多多"))]
    return _unique_keep_order(mentioned + requested_hits + ["淘宝", "京东"])[:3]


def _travel_apps(goal: str, requested: list[str]) -> list[str]:
    text = goal or ""
    mentioned = []
    for app_name in ("携程", "智行", "去哪儿"):
        if app_name in text:
            mentioned.append(app_name)
    requested_hits = [a for a in requested if any(k in a for k in ("携程", "智行", "去哪儿"))]
    return _unique_keep_order(mentioned + requested_hits + ["携程", "智行", "去哪儿"])[:3]


def _shopping_compare_workflow(goal: str, requested_apps: list[str]) -> dict | None:
    compact = re.sub(r"\s+", "", goal or "")
    if not any(k in compact for k in ("比价", "哪家便宜", "哪个便宜", "最便宜", "价格")):
        return None
    if not any(k in compact for k in ("淘宝", "京东", "拼多多", "商品", "首屏", "ThinkPad", "电脑", "键盘", "手机")):
        return None
    query = _extract_product_query(goal)
    apps = _shopping_apps(goal, requested_apps)
    tasks = []
    for app_name in apps:
        key = f"{app_name}_product_price"
        tasks.append({
            "id": f"{app_name}_price",
            "app": app_name,
            "goal": f"搜索{query}并停在首屏商品结果页",
            "steps": [
                f"搜索「{query}」",
                "停在能看到商品标题和价格的首屏结果页",
                f"REMEMBER {key}: 调用 /agent_extract 抽取商品标题、价格、店铺/来源，并记录当前截图证据",
            ],
            "remember": {
                "key": key,
                "kind": "product_price",
                "query": query,
                "fields": ["app", "title", "price", "currency", "source", "screenshot"],
                "extract_goal": f"抽取{app_name}首屏里最可比的{query}商品价格；价格越低越好",
                "metric": "price",
                "higher_is_better": False,
            },
        })
    return {
        "workflow": True,
        "source": "cross_app_rule",
        "mode": "shopping_compare",
        "goal": goal,
        "query": query,
        "tasks": tasks,
        "merge": {
            "strategy": "min",
            "metric": "price",
            "inputs": [t["remember"]["key"] for t in tasks],
            "summary": "比较各 App 记录的 price，选最低价；若某 App 抽取失败，明确说明基于其余 App 比较。",
        },
        "final": {
            "action": "summarize",
            "template": f"汇总{query}在" + "、".join(apps) + "的首屏价格，给出最低价和证据截图。",
        },
    }


def _travel_compare_workflow(goal: str, requested_apps: list[str]) -> dict | None:
    compact = re.sub(r"\s+", "", goal or "")
    if not any(k in compact for k in ("机票", "高铁", "火车票", "车票")):
        return None
    if not any(k in compact for k in ("哪家便宜", "哪个便宜", "最便宜", "比价", "价格")):
        return None
    slots = _extract_route_slots(goal)
    apps = _travel_apps(goal, requested_apps)
    ticket_kind = "flight_price" if "机票" in compact else "train_price"
    metric_label = "机票" if ticket_kind == "flight_price" else "车票"
    tasks = []
    for app_name in apps:
        key = f"{app_name}_{ticket_kind}"
        tasks.append({
            "id": f"{app_name}_{ticket_kind}",
            "app": app_name,
            "goal": f"查询{slots['date']}{slots['origin']}到{slots['destination']}的{metric_label}价格并停在结果页",
            "steps": [
                f"选择出发地「{slots['origin']}」和目的地「{slots['destination']}」",
                f"选择日期「{slots['date']}」并搜索{metric_label}",
                f"REMEMBER {key}: 调用 /agent_extract 抽取最低可购买价格、班次/航班号、出发时间，并记录当前截图证据",
            ],
            "remember": {
                "key": key,
                "kind": ticket_kind,
                "route": slots,
                "fields": ["app", "title", "price", "currency", "departure_time", "screenshot"],
                "extract_goal": f"抽取{app_name}结果页最低可购买{metric_label}价格；价格越低越好",
                "metric": "price",
                "higher_is_better": False,
            },
        })
    return {
        "workflow": True,
        "source": "cross_app_rule",
        "mode": "travel_compare",
        "goal": goal,
        "route": slots,
        "tasks": tasks,
        "merge": {
            "strategy": "min",
            "metric": "price",
            "inputs": [t["remember"]["key"] for t in tasks],
            "summary": "比较各 App 记录的 price，选最低价；失败项不编造，汇报可用来源范围。",
        },
        "final": {
            "action": "summarize",
            "template": f"汇总{slots['date']}{slots['origin']}到{slots['destination']}在" + "、".join(apps) + f"的{metric_label}价格。",
        },
    }


def _multi_app_workflow(goal: str, requested_apps: list[str]) -> dict:
    workflow = _shopping_compare_workflow(goal, requested_apps) or _travel_compare_workflow(goal, requested_apps)
    if workflow:
        return workflow
    return {
        "workflow": False,
        "source": "none",
        "mode": "",
        "goal": goal,
        "tasks": [],
        "merge": {},
        "final": {},
        "reason": "未识别为跨 App 比较/汇总任务",
    }


@app.post("/agent_plan_multi")
def agent_plan_multi(req: MultiAppPlanRequest) -> dict:
    """T-13 跨 App 编排协议：把一次目标拆成多个 App 子任务，并声明每个子任务要 REMEMBER 的结构化结果。

    这里只做稳定、可回归的 planner/protocol，不直接执行手机动作。安卓执行层后续按 tasks 顺序调用
    run_agent，每个结果页用 T-14 /agent_extract 填 remember，最后按 merge 汇总。
    """
    return _multi_app_workflow(req.goal, req.apps)


def _zhihu_search_answer_recipe(goal: str, app_name: str) -> list[str] | None:
    app_l = (app_name or "").lower()
    if "知乎" not in app_name and "zhihu" not in app_l:
        return None
    compact = re.sub(r"\s+", "", goal or "")
    # "搜索结果页" describes the current page. It is not a query to search.
    if re.search(r"搜索结果页.*(?:点击|点开|打开).*第(?:一|1)", compact):
        return None
    if "搜" not in compact or not any(k in compact for k in ("回答", "回复", "第一个")):
        return None

    m = re.search(
        r"(?:搜索|搜一下|搜)(.+?)(?:[，,。.]?(?:并且|并|然后|再|接着|之后|打开|点开|点击|第一个|第一条|回答|回复)|$)",
        compact,
    )
    query = (m.group(1) if m else "").strip("，。,. ")
    query = re.sub(r"[，,。.]?(然后|并且|并|再|接着|之后).*$", "", query).strip("，。,. ")
    query = re.sub(r"^(知乎|在知乎|知乎上)", "", query).strip()
    if not query:
        return None
    return [
        f"在知乎顶部左侧搜索框输入'{query}'并搜索（不要点AI搜索，不要点右侧搜索热搜）",
        "在知乎搜索结果页点击第一个回答结果（不是顶部搜索框、标签、广告或热搜词）",
        "确认已打开第一个回答详情页",
    ]


@app.post("/agent_plan")
def agent_plan(req: AgentPlanRequest) -> dict:
    """分步计划：把"在App里完成某任务"拆成【有序、可逐个验证】的子步骤（治反应式agent来回瞎点/倒退）。
    P2：命中手写【App 攻略】→ 直接用其带避坑知识的步骤；否则把该 App 操作要点注入 LLM 拆计划。
    安卓端据此逐步推进、做完一步绝不回头。拆解失败退回整目标=等价旧的单目标行为，不阻塞。"""
    import app_guides
    import app_memory
    # 0) 真人演示（教学模式）最高优先：用户亲手示范过→直接返回元素级可【确定性回放】步骤，
    #    彻底跳过 3B 视觉选不准的死结（人验证过的路径，不靠模型猜）。
    demo = None if req.skip_demo else app_memory.lookup_demo(req.app, req.goal)
    if demo:
        labels = [f"{i + 1}. {(s.get('text') or '点卡片').strip()}" for i, s in enumerate(demo["steps"])]
        return {"steps": labels, "source": "demo", "demo": demo["steps"]}
    # 1) 命中手写攻略 recipe → 直接用（确定性、自带避坑知识、无 LLM 方差，最可靠）
    zhihu_recipe = _zhihu_search_answer_recipe(req.goal, req.app)
    if zhihu_recipe:
        return {"steps": zhihu_recipe, "source": "guide"}
    recipe = app_guides.recipe_for(req.app, req.goal)
    if recipe:
        return {"steps": recipe, "source": "guide"}
    # 2) 复用本机成功经验：用户/真机跑通过的 App 任务，下次相似目标优先走已验证步骤。
    memory = app_memory.lookup(req.app, req.goal)
    if memory:
        return {"steps": memory["steps"], "source": "memory", "memory": memory}
    # 3) 否则 LLM 拆，并注入该 App 的操作要点（避坑知识进 prompt → 生成的子步骤也带知识）
    hints = app_guides.hints_for(req.app)
    skill_hints = app_memory.skill_hints_for(req.app, req.goal)
    hint_parts = []
    # 🐛 2026-07-03 真机实锤（Fable5 定位）："务必遵守"这个措辞 + 手写 hints 里恰好反复举
    # "每日推荐"的具体例子（当前唯一写了 recipe 的场景），把模型的任务目标带偏——用户要"点击
    # 猜你喜欢栏目"，模型却计划去点"每日推荐"，完全无视了真实目标、抄了 hints 里的示例。
    # 根因：hints 是【App 通用背景知识】，不该被措辞暗示成"本次任务要做的事"。
    if hints:
        hint_parts.append(f"【{req.app} 界面背景知识，仅帮助理解该 App 的通用操作方式，"
                          f"不代表本次任务目标】：{hints}")
    if skill_hints:
        hint_parts.append(f"【从真人示教沉淀的控件技能，仅供参考】\n{skill_hints}")
    hint_line = ("\n".join(hint_parts) + "\n") if hint_parts else ""
    llm = _get_agent_llm()
    sys_p = ("你把'在手机App里完成某任务'拆成有序、可逐个验证的子步骤。"
             "每步是一个明确的里程碑，能独立判断做没做到。只输出 JSON、不要多余解释。")
    user_p = (
        f"App：{req.app or '未知'}\n任务：{req.goal}\n"
        f"{hint_line}"
        "**App 已经自动打开好了**——【绝对不要】包含'打开App'、'进入App'、'进入主界面/首页'这类步骤，"
        "直接从【App 内要做的第一件实事】开始拆。\n"
        "**子步骤必须紧扣上面「任务」字段写的目标，背景知识只是帮你理解界面/操作方式，"
        "如果背景知识里举的例子和「任务」目标不是同一件事，以「任务」为准，别偷换成背景知识里的例子。**\n"
        "拆成 2~4 个有序子步骤，每步可独立判断是否完成；"
        "**最后一步必须是'确认最终目标已达成'**（如：确认视频正在播放 / 已进入直播间）。\n"
        "例：进b站第一个直播间 → "
        '["进入直播栏目","找到并点进第一个正在直播的直播间(不是录播/预告)","确认已在直播间内观看"]\n'
        '输出紧凑 JSON：{"steps":["子步骤1","子步骤2",...]}'
    )
    msgs = [{"role": "system", "content": sys_p}, {"role": "user", "content": user_p}]
    try:
        d = llm.chat_json(msgs, temperature=0.2, max_tokens=220)
        steps = [str(s).strip() for s in (d.get("steps") or []) if str(s).strip()][:6]
        return {"steps": steps or [req.goal]}
    except Exception:  # noqa: BLE001 — 拆解失败退回整目标，等价旧单目标行为
        return {"steps": [req.goal]}


class AgentStepRequest(CloudRequest):
    goal: str
    screen: str = ""
    history: list[str] = Field(default_factory=list)
    subgoal: str = ""   # 当前要完成的子步骤（分步推进时聚焦它；空=旧的整目标模式）
    app: str = Field(default="", description="当前 App 名→注入该 App 攻略 hints（治单步决策没有App知识）")


@app.post("/agent_step")
def agent_step(req: AgentStepRequest) -> dict:
    """③ GUI Agent 单步决策：看当前屏幕 + 目标 → 决定下一步动作（边看边决策范式）。"""
    import app_guides

    agent_llm = _get_agent_llm()
    # 🐛 2026-07-03 真机回归实测（用户指出淘宝/知乎搜索框机制）：/agent_plan、/agent_step_visual、
    # /agent_reflect 都会注入该 App 的操作要点，唯独真正决定"这一步具体点哪/输什么"的 /agent_step
    # 从来没有 app 字段、注入不了任何 App 知识——"点搜索按钮 vs 点搜索框本体"这类坑恰恰就是在
    # 这一步决策错的，之前写的 hints 全用不上。补上 app 字段+hints 注入，四个端点终于对齐。
    guide_hints = app_guides.hints_for(req.app)
    # AgentCPM-GUI 启发的【紧凑动作空间】：动作词最小化、JSON 紧凑、reason 限长 ——
    # 砍每步输入/输出 token，治 ③ 多步撞 5s（实测静态脚手架 309tok/步、4 步白背一大坨）。
    # 注意：原先焊在这里的「闹钟/设时间·12 小时制表盘」整段逻辑已【删除】——按既定铁结论，
    # 精细/有状态任务走 ①系统API(ACTION_SET_ALARM)，③ 不再背这 187tok 死重量，也避免它
    # 干扰其它任务的决策（"给通用 prompt 堆任务补丁是无底洞"）。字段名 action/target/reason
    # 保持与安卓 Api.kt 的 AgentStep 对齐，不动端侧一行。
    has_sub = bool(req.subgoal.strip())
    sys_p = ("你是操作安卓界面的智能体，每次只决定一步动作。"
             "先想后做：先在 thought 里简要推理，再给动作。只输出 JSON。")
    focus_line = (f"【当前只需完成这一个子步骤】：{req.subgoal}\n"
                  if has_sub else "")
    hint_line = f"【{req.app} 操作要点，仅帮助理解界面/操作方式】：{guide_hints}\n" if guide_hints else ""
    user_p = (
        f"总目标：{req.goal}\n"
        f"{focus_line}"
        f"{hint_line}"
        f"屏幕可见控件文本（空格分隔）：\n{req.screen[:1500]}\n"
        f"已执行：{req.history or '无'}\n"
        '先想后做：先在 "thought" 里说清【当前这步做到哪了、屏上看到什么关键内容、下一步为什么这么做】。\n'
        '输出 JSON：{"thought":"…(一句话推理)","subgoal_done":true/false,'
        '"action":"tap|input|scroll|back|vision|none","target":"…","reason":"≤8字"}\n'
        + ("判断顺序：①若【当前子步骤已经达成】→ subgoal_done=true、action=none"
           "（系统会自动推进到下一步，你【不要】再多点；判达成要看屏上实据，如已在直播间/视频在播放。"
           "**入口按钮和目标页常同名**：点【每日推荐】进的页面标题也叫'每日推荐'——屏上已出现目标页特征"
           "（一批并列的歌名/视频条目、日期头、'播放全部'等）就是已达成，别再点同名文字）；\n"
           "②**这步若要靠【看界面外观/缩略图】才分得清该点哪个**（如哪个是真的直播间/视频/歌曲卡片，"
           "而不是搜索框、标签栏、横幅广告里的文字）、单凭文本分不清→**action=vision**"
           "（交给视觉模型看屏幕选，别拿文本瞎点）；\n"
           "③没达成且文本能定位→ subgoal_done=false，推进一步：目标不在当前屏(在下方)→scroll(target留空)；"
           "进错页→back；否则 tap 屏上出现过的文本 / input 关键词。\n"
           if has_sub else
           "判断顺序：①【最终目标达成】才 subgoal_done=true,action=none(要'播放视频'必须看到确实在播放才算)；"
           "②要靠看界面外观分辨该点哪个(真卡片vs搜索框/标签/广告)→action=vision；"
           "③内容不在当前屏→scroll(target留空)；④进错页→back；⑤否则推进一步 tap/input。\n")
        + "动作语义：tap=点屏幕上【出现过】的文本；input=向已聚焦输入框填【关键词本身】"
        "(去掉'搜索/打开/播放/第一个'等动词和App名，别填整句)；scroll=当前屏没有可点目标时下滑；"
        "vision=文本分不清该点哪个、交视觉模型看屏选。\n"
        "**铁律：①只做当前这步的事，别跳步、更别回头去做【已执行】里完成过的步骤；"
        "②同一个 target 绝不连点，点了没变化就改 scroll 或 back，别重复点同一个词。**"
    )
    msgs = [{"role": "system", "content": sys_p}, {"role": "user", "content": user_p}]
    # 🐛 bug1（学长 review · 成功率元凶）：决策失败【绝不】返回 subgoal_done=True——那会被安卓当"这步完成"
    #    推进到下一步、在错误页面状态上瞎跑，整条链崩却表面"走完"。改返回 action="error"，让安卓诚实收手
    #    （与视觉路径 /agent_step_visual 的 error 语义一致）。DashScope 尾延时/JSON 截断都走这条。
    # 🐛 bug2：max_tokens 220→300（thought 排 JSON 首字段，太小会半截截断致 chat_json 解析失败）；
    #    解析失败先携错重试一次（仿意图引擎的"validate→携错重试1次"），仍失败才 error。
    last_err: Exception | None = None
    for attempt in range(2):
        try:
            return agent_llm.chat_json(msgs, temperature=0.2, max_tokens=300)
        except Exception as e:  # noqa: BLE001
            last_err = e
            if attempt == 0:  # 多半是 JSON 被 thought 撑爆截断 → 追加约束再试一次
                msgs.append({"role": "user",
                             "content": "上次输出无法解析为 JSON。请【只】输出紧凑 JSON、thought≤20字，别带多余文字。"})
    return {"action": "error", "subgoal_done": False, "target": "",
            "reason": f"决策失败: {last_err}"}


class VisualPickRequest(CloudScreenshotRequest):
    """路线C 视觉兜底：a11y 啃不动时，安卓端上传 MediaProjection 截图 + 可点元素框，
    大脑画 Set-of-Mark 编号 → ≤3B 视觉模型选号 → 回该元素中心坐标给安卓 tap。"""
    goal: str
    history: list[str] = Field(default_factory=list)
    screenshot: str = Field(..., description="MediaProjection 截图，base64(PNG)")
    elements: list[dict] = Field(
        default_factory=list,
        description="a11y 可点元素，每项 {id:int, text:str, bounds:[l,t,r,b]}（原图像素）")
    app: str = Field(default="", description="当前 App 名→注入该 App 攻略 hints 进视觉 prompt")


@app.post("/agent_step_visual")
def agent_step_visual(req: VisualPickRequest) -> dict:
    """③ GUI-agent 视觉兜底单步：SoM 选号。坐标取自 a11y 原始 bounds（零缩放误差）。
    隐私：界面截图非生物特征、赛题允许推理大模型上云；人脸/语音生物特征仍端侧闭环（本端点不碰）。"""
    import app_guides
    import app_memory
    import visual  # 同目录模块（运行 brain/server.py 时 brain 目录在 sys.path）
    gate = validate_cloud_screenshot(
        screenshot_b64=req.screenshot,
        consent=req.cloud_vision_consent,
        policy_version=req.cloud_vision_policy,
        purpose=req.screenshot_purpose,
        expected_purpose="visual_pick",
        screen_sensitivity=req.screen_sensitivity,
        screenshot_sha256=req.screenshot_sha256,
    )
    if not gate.allowed:
        return {"action": "blocked", "x": None, "y": None, "mark": None,
                "reason": gate.code, "thought": "云端截图隐私门已阻止上传", "model": ""}
    png = gate.png or b""
    # 注入该 App 攻略 hints → 3B 视觉模型带着"直播间=带缩略图+在线人数的卡片、不是搜索框/横幅广告"这类知识选号
    guide_hints = app_guides.hints_for(req.app)
    skill_hints = app_memory.skill_hints_for(req.app, req.goal)
    hints = "\n".join(x for x in (guide_hints, skill_hints) if x)
    return visual.visual_pick(req.goal, req.history, png, req.elements, hints)


class AgentExtractRequest(CloudScreenshotRequest):
    """T-14 结构化抽取：结果页截图 + 候选元素框 →（VLM 抽数字，代码比大小）→ 该点哪个编号。
    #8/#9/#11/#12/#13（播放量最高/点赞最多/评分最高/最便宜等）共用同一个端点。"""
    goal: str
    screenshot: str = Field(..., description="MediaProjection 截图，base64(PNG)")
    elements: list[dict] = Field(
        default_factory=list,
        description="a11y 候选元素，每项 {id:int, text:str, bounds:[l,t,r,b]}（原图像素）")
    metric: str = Field(default="", description="要比较的指标，如'播放量'/'评分'/'价格'，可留空由 goal 推断")
    higher_is_better: bool | None = Field(
        default=None, description="越大越好=true/越小越好=false；留空按 goal+metric 文本自动推断")
    app: str = Field(default="", description="当前 App 名→注入该 App 攻略 hints")


@app.post("/agent_extract")
def agent_extract(req: AgentExtractRequest) -> dict:
    """T-14：数值比较交代码、视觉只做抽取——与 SoM(视觉只选号、坐标交a11y)同一套哲学。
    隐私口径同 /agent_step_visual：界面截图非生物特征，人脸/语音生物特征不经此端点。"""
    import app_guides
    import visual  # 同目录模块
    gate = validate_cloud_screenshot(
        screenshot_b64=req.screenshot,
        consent=req.cloud_vision_consent,
        policy_version=req.cloud_vision_policy,
        purpose=req.screenshot_purpose,
        expected_purpose="structured_extract",
        screen_sensitivity=req.screen_sensitivity,
        screenshot_sha256=req.screenshot_sha256,
    )
    if not gate.allowed:
        return {"action": "blocked", "x": None, "y": None, "mark": None,
                "items": [], "reason": gate.code, "model": ""}
    png = gate.png or b""
    hints = app_guides.hints_for(req.app)
    return visual.extract_pick(req.goal, png, req.elements, req.metric, req.higher_is_better, hints)


class AgentReflectRequest(CloudScreenshotRequest):
    """动作后反思：高风险点击后验收是否真的到达当前子步骤目标。
    隐私口径同视觉兜底：只传界面截图/读屏文本，不涉及人脸、语音生物特征。"""
    goal: str
    subgoal: str = ""
    action: str = ""
    target: str = ""
    before_screen: str = ""
    after_screen: str = ""
    screenshot: str = Field(default="", description="动作后 MediaProjection 截图，base64(PNG)，可为空")
    app: str = Field(default="", description="当前 App 名→注入 App 攻略 hints")


@app.post("/agent_reflect")
def agent_reflect(req: AgentReflectRequest) -> dict:
    """P4-A 动作后反思：SUCCESS / WRONG_PAGE / NO_CHANGE / UNSURE。
    安卓端用它给 GUI-agent 加刹车：没证明成功就不推进，错页则回退/重试。"""
    import app_guides
    import app_memory
    import visual
    png = None
    if req.screenshot:
        gate = validate_cloud_screenshot(
            screenshot_b64=req.screenshot,
            consent=req.cloud_vision_consent,
            policy_version=req.cloud_vision_policy,
            purpose=req.screenshot_purpose,
            expected_purpose="action_reflect",
            screen_sensitivity=req.screen_sensitivity,
            screenshot_sha256=req.screenshot_sha256,
        )
        if not gate.allowed:
            return {"status": "UNSURE", "reason": gate.code,
                    "thought": "云端截图隐私门已阻止上传", "model": "privacy-gate"}
        png = gate.png
    guide_hints = app_guides.hints_for(req.app)
    skill_hints = app_memory.skill_hints_for(req.app, req.goal, req.after_screen)
    hints = "\n".join(x for x in (guide_hints, skill_hints) if x)
    # 没截图的纯文本验收交给更稳的 14B 文本模型，不要被台式机 VLM(3B) 掉线株连
    # （2026-07-03 真机实测抓到：纯文本反思以前也硬走 VLM，一断线连这种本该稳的判断都报废）。
    return visual.reflect_action(
        req.goal,
        req.subgoal or req.goal,
        req.action,
        req.target,
        req.before_screen,
        req.after_screen,
        png,
        hints,
        text_llm=_get_agent_llm(),
    )


class AgentMemoryRecordRequest(CloudRequest):
    """安卓端 run_agent 完成/失败后上报的文本经验。只含文字步骤和动作历史，不含截图/音频/人脸。"""
    app: str = ""
    goal: str
    steps: list[str] = Field(default_factory=list)
    history: list[str] = Field(default_factory=list)
    trace: list[dict] = Field(default_factory=list)
    success: bool = False
    note: str = ""


@app.post("/agent_memory/record")
def agent_memory_record(req: AgentMemoryRecordRequest) -> dict:
    import app_memory
    return app_memory.record(req.app, req.goal, req.steps, req.history, req.trace, req.success, req.note)


@app.get("/agent_memory")
def agent_memory_list(limit: int = 50) -> dict:
    import app_memory
    return {"entries": app_memory.list_entries(limit)}


class RecordDemoRequest(CloudRequest):
    """教学模式：用户手动示范一次后上报的【元素级】可回放步骤。
    隐私：每步只含 元素文字/坐标/页面文本签名，绝不含截图/音频/人脸。"""
    app: str = ""
    goal: str
    steps: list[dict] = Field(default_factory=list)   # [{action, text, bounds:[l,t,r,b], sig}]


@app.post("/agent_memory/record_demo")
def agent_memory_record_demo(req: RecordDemoRequest) -> dict:
    """存一条真人演示，成为该 (App, 目标) 的最高优先【可确定性回放】技能。"""
    import app_memory
    return app_memory.record_demo(req.app, req.goal, req.steps)


@app.get("/agent_demos")
def agent_demos_list(limit: int = 50) -> dict:
    """查看已学的真人演示（隐私/调试用：只列文字化步骤）。"""
    import app_memory
    return {"demos": app_memory.list_demos(limit)}


@app.get("/agent_skills")
def agent_skills_list(limit: int = 80) -> dict:
    """查看从示教/成功轨迹沉淀出的控件级技能文档。隐私：只含文字、bounds、页面关键词。"""
    import app_memory
    return {"skills": app_memory.list_skill_docs(limit)}


@app.post("/agent_skills/rebuild")
def agent_skills_rebuild() -> dict:
    """把历史真人演示重建成控件级技能文档。升级/调试用，不存截图。"""
    import app_memory
    return app_memory.rebuild_skill_docs_from_demos()


@app.post("/agent_skills/rebuild_trace")
def agent_skills_rebuild_trace() -> dict:
    """把历史 run_agent memory 重建成成功/避坑技能文档。升级/调试用，不存截图。"""
    import app_memory
    return app_memory.rebuild_trace_skill_docs_from_memory()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8000)
    args = parser.parse_args()
    import uvicorn

    uvicorn.run(app, host=args.host, port=args.port)


if __name__ == "__main__":
    main()
