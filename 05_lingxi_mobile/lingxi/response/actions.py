"""模拟终端执行器 —— 意图落地为终端动作（mock），输出回执与 GUI 卡片。

执行策略（写入方案文档）：
- 查询类（天气/日程）与低风险即时类（播音乐/开应用/设提醒）：立即执行
- 有外部副作用类（发消息/打电话）：两段式——先生成待确认动作+确认卡片，
  用户用语音或**点头/摇头**确认后才真正执行。这是多模态确认闭环的落点。

GUI 卡片为纯 JSON 结构（阶段8前端渲染），形如:
  {"type": "confirm|result|clarify", "title": str, "fields": [{"label","value"}], "actions": [...]}
"""
from __future__ import annotations

import os
from dataclasses import dataclass, field
from datetime import datetime, timedelta, timezone
from zoneinfo import ZoneInfo, ZoneInfoNotFoundError

from ..core.log import get_logger
from ..intent.schema import IntentResult

logger = get_logger("response.actions")

_WEATHER_LOCATION_MISSING = {"", "今天", "明天", "后天", "本地", "当前", "现在", "未知"}
_TIMEZONE_ENV = "LINGXI_TIMEZONE"
_DEFAULT_USER_TIMEZONE = "Asia/Shanghai"
_CHINA_STANDARD_TIME = timezone(timedelta(hours=8), name=_DEFAULT_USER_TIMEZONE)


def _utc_now() -> datetime:
    """Return an aware UTC instant so process/container TZ cannot leak into replies."""
    return datetime.now(timezone.utc)


def _configured_user_timezone():
    """Resolve the deployment timezone, with a deterministic China fallback.

    Railway validates the IANA name before startup.  The fixed UTC+08 fallback
    only keeps local/offline builds correct if the host image lacks tzdata.
    """
    name = os.environ.get(_TIMEZONE_ENV, _DEFAULT_USER_TIMEZONE).strip()
    name = name or _DEFAULT_USER_TIMEZONE
    try:
        return ZoneInfo(name)
    except (ZoneInfoNotFoundError, ValueError):
        logger.error("无效或不可用的用户时区 %s，回退到 Asia/Shanghai", name)
        try:
            return ZoneInfo(_DEFAULT_USER_TIMEZONE)
        except ZoneInfoNotFoundError:
            return _CHINA_STANDARD_TIME


def _now_for_user() -> datetime:
    return _utc_now().astimezone(_configured_user_timezone())


@dataclass
class ActionOutcome:
    status: str                      # executed / pending_confirmation / clarify / chat / none
    feedback: str = ""               # 给应答生成器的执行回执（自然语言）
    card: dict = field(default_factory=dict)
    pending_action: dict | None = None  # 新建的待确认动作（None=清除/无）


class MockTerminal:
    """模拟终端状态：被执行器读写，被 demo/评测断言。真实终端集成时替换此类。"""

    def __init__(self) -> None:
        self.outbox: list[dict] = []          # 已发出的消息
        self.calls: list[str] = []
        self.music_playing: str | None = None
        self.reminders: list[dict] = []
        self.notes: list[str] = []
        self.searches: list[str] = []
        self.opened_app: str | None = None
        self.navigations: list[str] = []     # 已发起的导航目的地
        self.emails: list[dict] = []         # 已起草的邮件
        self.agent_tasks: list[str] = []     # 已派发给 GUI-agent 的多步任务
        tomorrow = (datetime.now() + timedelta(days=1)).strftime("%m月%d日")
        self.schedule = {tomorrow: ["10:00 项目评审会", "15:30 牙医预约"]}

    def weather(self, location: str, date: str) -> str:
        return f"{location or '本地'}{date or '今天'}：晴转多云，24~31℃，东南风2级"  # mock 数据

    # 副作用动作下沉为方法：MockTerminal 只记可断言状态，RealTerminal 覆盖为真实执行
    def open_app(self, name: str) -> str:
        self.opened_app = name
        return f"已打开{name}"

    def play_music(self, query: str) -> str:
        self.music_playing = query
        return f"已开始播放：{query}"

    def set_reminder(self, time: str, content: str) -> str:
        self.reminders.append({"time": time, "content": content})
        return f"提醒已设置：{time} {content}".strip()

    def take_note(self, content: str) -> str:
        self.notes.append(content)
        return f"已记录：{content}"

    def web_search(self, query: str) -> str:
        self.searches.append(query)
        return f"已为你打开浏览器搜索：{query}"

    def navigate(self, destination: str, mode: str = "") -> str:
        self.navigations.append(destination)
        return f"正在为你导航到{destination}".strip()

    def compose_email(self, to: str, subject: str = "", body: str = "") -> str:
        self.emails.append({"to": to, "subject": subject, "body": body})
        return f"已为你打开给{to}的邮件草稿".strip()

    def run_agent(self, goal: str) -> str:
        self.agent_tasks.append(goal)
        return f"好的，我在手机上边看边操作：{goal}"


def _card(type_: str, title: str, fields: dict, actions: list[str] | None = None) -> dict:
    return {
        "type": type_,
        "title": title,
        "fields": [{"label": k, "value": str(v)} for k, v in fields.items() if v],
        "actions": actions or [],
    }


class ActionExecutor:
    def __init__(self, terminal: MockTerminal | None = None) -> None:
        self.terminal = terminal or MockTerminal()

    def execute(self, r: IntentResult, pending_action: dict | None = None) -> ActionOutcome:
        if r.need_clarification:
            return ActionOutcome(
                status="clarify", feedback="需要向用户澄清",
                card=_card("clarify", "需要确认", {"问题": r.clarify_question}),
                pending_action=pending_action,  # 澄清不破坏现有待确认动作
            )
        handler = getattr(self, f"_do_{r.intent}", None)
        if handler is None:
            return ActionOutcome(status="none", feedback="", pending_action=pending_action)
        return handler(r, pending_action)

    # ---- 两段式：发消息 / 打电话 ----

    def _do_send_message(self, r: IntentResult, _pending) -> ActionOutcome:
        slots = r.slots
        summary = f"通过{slots.get('app', '默认应用')}给{slots['recipient']}发送「{slots.get('content', '')}」"
        return ActionOutcome(
            status="pending_confirmation",
            feedback=f"已准备好{summary}，等待用户确认",
            card=_card("confirm", f"发送消息给 {slots['recipient']}",
                       {"内容": slots.get("content"), "应用": slots.get("app")},
                       ["确认发送", "取消"]),
            pending_action={"type": "send_message", "summary": summary, "slots": dict(slots)},
        )

    def _do_make_call(self, r: IntentResult, _pending) -> ActionOutcome:
        contact = r.slots["contact"]
        return ActionOutcome(
            status="pending_confirmation",
            feedback=f"已准备拨打{contact}的电话，等待用户确认",
            card=_card("confirm", f"拨打电话：{contact}", {}, ["拨打", "取消"]),
            pending_action={"type": "make_call", "summary": f"拨打{contact}的电话",
                            "slots": dict(r.slots)},
        )

    # ---- confirm / cancel 对待确认动作收口 ----

    def _do_confirm(self, r: IntentResult, pending: dict | None) -> ActionOutcome:
        if not pending:
            return ActionOutcome(status="none", feedback="没有待确认的动作")
        slots = pending.get("slots", {})
        ptype = pending.get("type")
        if ptype == "send_message":
            self.terminal.outbox.append(slots)
            logger.info("已发送: %s", slots)
        elif ptype == "make_call":
            self.terminal.calls.append(slots.get("contact", ""))
        else:
            logger.info("执行待确认动作: %s", pending.get("summary"))
        return ActionOutcome(
            status="executed",
            feedback=f"{pending.get('summary', '动作')}——已完成",
            card=_card("result", "已完成", {"动作": pending.get("summary")}),
            pending_action=None,
        )

    def _do_cancel(self, r: IntentResult, pending: dict | None) -> ActionOutcome:
        if not pending:
            return ActionOutcome(status="none", feedback="没有正在进行的操作")
        return ActionOutcome(
            status="executed",
            feedback=f"已取消：{pending.get('summary', '当前操作')}",
            card=_card("result", "已取消", {"动作": pending.get("summary")}),
            pending_action=None,
        )

    # ---- 即时执行类 ----

    def _do_play_music(self, r: IntentResult, _pending) -> ActionOutcome:
        query = r.slots.get("query", "随机推荐")
        feedback = self.terminal.play_music(query)
        return ActionOutcome(
            status="executed", feedback=feedback,
            card=_card("result", "正在播放", {"曲目": query}), pending_action=None,
        )

    def _do_query_weather(self, r: IntentResult, pending) -> ActionOutcome:
        location = str(r.slots.get("location", "") or "").strip()
        if location in _WEATHER_LOCATION_MISSING:
            # Cloud-3: server IP is not the user's location.  A cloud Brain must
            # never silently turn “本地/当前” into its datacenter city.
            return ActionOutcome(
                status="clarify",
                feedback="请告诉我你要查询的城市，例如“查询明天杭州天气”。",
                card=_card("clarify", "需要城市", {"缺少": "城市"}),
                pending_action=pending,
            )
        info = self.terminal.weather(location, r.slots.get("date", ""))
        return ActionOutcome(
            status="executed", feedback=f"天气查询结果：{info}",
            card=_card("result", "天气", {"详情": info}), pending_action=pending,
        )

    def _do_query_schedule(self, r: IntentResult, pending) -> ActionOutcome:
        date = r.slots.get("date", "明天")
        items = next(iter(self.terminal.schedule.values()), [])
        text = "；".join(items) if items else "没有安排"
        return ActionOutcome(
            status="executed", feedback=f"{date}的日程：{text}",
            card=_card("result", f"{date}的日程", {"安排": text}), pending_action=pending,
        )

    def _do_query_time(self, r: IntentResult, pending) -> ActionOutcome:
        now = _now_for_user()
        hm, ymd = now.strftime("%H:%M"), now.strftime("%Y年%m月%d日")
        return ActionOutcome(
            status="executed", feedback=f"现在是 {hm}（{ymd}）",
            card=_card("result", "当前时间", {"时间": hm, "日期": ymd}), pending_action=pending,
        )

    def _do_create_reminder(self, r: IntentResult, _pending) -> ActionOutcome:
        item = {"time": r.slots.get("time", ""), "content": r.slots.get("content", "")}
        feedback = self.terminal.set_reminder(item["time"], item["content"])
        return ActionOutcome(
            status="executed", feedback=feedback,
            card=_card("result", "提醒已设置", item), pending_action=None,
        )

    def _do_take_note(self, r: IntentResult, _pending) -> ActionOutcome:
        content = r.slots.get("content", "")
        feedback = self.terminal.take_note(content)
        return ActionOutcome(
            status="executed", feedback=feedback,
            card=_card("result", "已记到记事本", {"内容": content}), pending_action=None,
        )

    def _do_open_app(self, r: IntentResult, _pending) -> ActionOutcome:
        app = r.slots["app_name"]
        feedback = self.terminal.open_app(app)
        return ActionOutcome(
            status="executed", feedback=feedback,
            card=_card("result", f"已打开 {app}", {}), pending_action=None,
        )

    def _do_web_search(self, r: IntentResult, pending) -> ActionOutcome:
        query = r.slots.get("query", "")
        feedback = self.terminal.web_search(query)
        return ActionOutcome(
            status="executed", feedback=feedback,
            card=_card("result", "网页搜索", {"搜索": query}), pending_action=pending,
        )

    # ---- 设备能力档·即时执行（手机：geo: / mailto:，低风险不走两段式确认）----
    def _do_navigate(self, r: IntentResult, _pending) -> ActionOutcome:
        dest = r.slots.get("destination", "")
        feedback = self.terminal.navigate(dest, r.slots.get("mode", ""))
        return ActionOutcome(
            status="executed", feedback=feedback,
            card=_card("result", "导航", {"目的地": dest, "方式": r.slots.get("mode", "")}),
            pending_action=None,
        )

    def _do_send_email(self, r: IntentResult, _pending) -> ActionOutcome:
        to = r.slots.get("to", "")
        feedback = self.terminal.compose_email(to, r.slots.get("subject", ""),
                                               r.slots.get("body", ""))
        return ActionOutcome(
            status="executed", feedback=feedback,
            card=_card("result", "邮件草稿",
                       {"收件人": to, "主题": r.slots.get("subject", "")}),
            pending_action=None,
        )

    def _do_run_agent(self, r: IntentResult, _pending) -> ActionOutcome:
        goal = r.slots.get("goal", "")
        if not goal:
            return ActionOutcome(status="clarify", feedback="要我在手机上帮你做什么呢？")
        feedback = self.terminal.run_agent(goal)   # 实际多步操作在安卓端无障碍 agent 跑
        return ActionOutcome(
            status="executed", feedback=feedback,
            card=_card("result", "边看边操作", {"任务": goal}), pending_action=None,
        )

    def _do_chitchat(self, r: IntentResult, pending) -> ActionOutcome:
        return ActionOutcome(status="chat", feedback="闲聊，直接回答用户",
                             pending_action=pending)

    def _do_unclear(self, r: IntentResult, pending) -> ActionOutcome:
        return ActionOutcome(status="clarify", feedback="需要向用户澄清",
                             pending_action=pending)


class RealTerminal(MockTerminal):
    """真实执行**低风险**动作（打开应用/音乐文件夹/系统提醒弹窗），同时保留 mock 状态可断言。

    仅限本地、零配置、无封号风险的操作；发消息/打电话仍走父类 mock（个人微信无API、真发有风险）。
    桌宠演示用（config response.terminal=real 或 pet_brain 注入），评测/批处理保持 MockTerminal。
    """

    _APPS = {
        "计算器": "calc.exe", "calculator": "calc.exe",
        "记事本": "notepad.exe", "notepad": "notepad.exe",
        "画图": "mspaint.exe", "写字板": "write.exe",
        "文件管理器": "explorer.exe", "资源管理器": "explorer.exe", "文件夹": "explorer.exe",
        "任务管理器": "taskmgr.exe", "命令提示符": "cmd.exe", "终端": "wt.exe",
        "截图": "snippingtool.exe",
    }
    _URIS = {
        "设置": "ms-settings:", "日历": "outlookcal:", "邮件": "outlookmail:",
        "浏览器": "https://www.bing.com", "地图": "bingmaps:",
    }

    def open_app(self, name: str) -> str:
        super().open_app(name)  # 记 mock 状态(opened_app)
        try:
            self._launch(name.strip())
        except Exception as e:  # noqa: BLE001 — 真实启动失败不应让会话报错
            logger.warning("真实打开 %s 失败，仅记录: %s", name, e)
        return f"已为你打开{name}"

    def _launch(self, name: str) -> None:
        import os
        import subprocess
        import webbrowser

        low = name.lower()
        for alias, exe in self._APPS.items():
            if alias.lower() in low or low in alias.lower():
                subprocess.Popen(exe, shell=True)
                return
        for alias, uri in self._URIS.items():
            if alias.lower() in low or low in alias.lower():
                os.startfile(uri)
                return
        lnk = self._find_installed_app(name)  # 扫开始菜单快捷方式：能开任意已装app(WeGame/Steam/微信…)
        if lnk:
            os.startfile(lnk)
            return
        try:  # 再当本地可执行/文档，最后兜底网页搜索
            os.startfile(name)
        except OSError:
            webbrowser.open(f"https://www.bing.com/search?q={name}")

    @staticmethod
    def _find_installed_app(name: str) -> str | None:
        """扫描「开始菜单」快捷方式(.lnk)按名字模糊匹配——Windows 上枚举已装程序最可靠的方式。
        WeGame/Steam/微信/Chrome… 几乎都在此留有快捷方式。返回最佳匹配的 .lnk 路径，无则 None。"""
        import glob
        import os

        key = name.lower().replace(" ", "").replace(".exe", "").replace(".lnk", "")
        if not key:
            return None
        roots = [
            os.path.join(os.environ.get("APPDATA", ""), r"Microsoft\Windows\Start Menu\Programs"),
            os.path.join(os.environ.get("PROGRAMDATA", ""), r"Microsoft\Windows\Start Menu\Programs"),
        ]
        exact, contains = [], []
        for root in roots:
            if not root or not os.path.isdir(root):
                continue
            for lnk in glob.glob(os.path.join(root, "**", "*.lnk"), recursive=True):
                s = os.path.splitext(os.path.basename(lnk))[0].lower().replace(" ", "")
                if s == key:
                    exact.append(lnk)
                elif key in s or s in key:
                    contains.append((len(s), lnk))
        if exact:
            return exact[0]
        return min(contains)[1] if contains else None

    def play_music(self, query: str) -> str:
        super().play_music(query)
        import os
        from pathlib import Path

        try:
            music_dir = Path.home() / "Music"
            if music_dir.exists():
                os.startfile(str(music_dir))
            else:
                import webbrowser
                webbrowser.open(f"https://music.163.com/#/search/m/?s={query}")
        except Exception as e:  # noqa: BLE001
            logger.warning("真实播放失败，仅记录: %s", e)
        return f"已开始播放：{query}"

    def set_reminder(self, time: str, content: str) -> str:
        super().set_reminder(time, content)
        self._notify("灵犀 · 提醒已设置", f"{time} {content}".strip())
        delay = self._parse_delay(time)
        if delay and 0 < delay <= 3600:  # 仅"X秒/分钟后"的近场定时演示
            import threading
            threading.Timer(delay, lambda: self._notify("⏰ 灵犀提醒", content)).start()
        return f"提醒已设置：{time} {content}".strip()

    def take_note(self, content: str) -> str:
        super().take_note(content)  # 记 mock 状态(notes)
        import os
        from datetime import datetime

        from ..core.config import PROJECT_ROOT

        try:
            notes_dir = PROJECT_ROOT / "data" / "notes"
            notes_dir.mkdir(parents=True, exist_ok=True)
            note_file = notes_dir / "灵犀笔记.txt"
            enc = "utf-8-sig" if not note_file.exists() else "utf-8"  # 首行 BOM 防记事本乱码
            stamp = datetime.now().strftime("%Y-%m-%d %H:%M")
            with open(note_file, "a", encoding=enc) as f:
                f.write(f"[{stamp}] {content}\n")
            os.startfile(str(note_file))  # 用记事本打开(已带内容)
        except Exception as e:  # noqa: BLE001
            logger.warning("真实记笔记失败，仅记录: %s", e)
        return f"已记到记事本：{content}"

    def web_search(self, query: str) -> str:
        super().web_search(query)  # 记 mock 状态(searches)
        try:
            import webbrowser
            webbrowser.open(f"https://www.bing.com/search?q={query}")
        except Exception as e:  # noqa: BLE001
            logger.warning("真实网页搜索失败，仅记录: %s", e)
        return f"已帮你打开浏览器搜索：{query}"

    def _notify(self, title: str, msg: str) -> None:
        """真实系统弹窗（零依赖；线程化避免 MessageBox 模态阻塞会话）。"""
        import ctypes
        import threading

        def _box():
            try:
                ctypes.windll.user32.MessageBoxW(0, msg, title, 0x40)  # MB_ICONINFORMATION
            except Exception:  # noqa: BLE001
                pass

        threading.Thread(target=_box, daemon=True).start()

    @staticmethod
    def _parse_delay(s: str):
        import re

        if not s:
            return None
        m = re.search(r"(\d+)\s*秒", s)
        if m:
            return int(m.group(1))
        m = re.search(r"(\d+)\s*分", s)
        if m:
            return int(m.group(1)) * 60
        return None

    def weather(self, location: str, date: str) -> str:
        """天气只返回显式城市的真实结果；失败时诚实说明不可用。

        和风新版账号需要专属 ``QWEATHER_HOST``。配置了专属 Host 时优先和风，
        否则优先复用项目已有的高德 Web Service 天气能力；两者均不得用云服务器
        IP 猜用户位置。仅剩旧版和风凭据且没有高德凭据时，保留原兼容路径。
        """
        import os

        qweather_key = os.environ.get("QWEATHER_KEY", "").strip()
        qweather_host = os.environ.get("QWEATHER_HOST", "").strip()
        amap_key = os.environ.get("AMAP_KEY", "").strip()
        loc = (location or "").strip()
        if loc in _WEATHER_LOCATION_MISSING:
            return "缺少明确城市，无法从云服务器位置推断你的所在地"
        if not qweather_key and not amap_key:
            return f"{loc}天气服务暂不可用（服务端未配置天气凭据）"

        real = None
        if qweather_key and qweather_host:
            real = self._qweather(loc, qweather_key)
        if real is None and amap_key:
            real = self._amap_weather(loc, amap_key)
        # 兼容仅配置旧和风 key 的本地部署；公有云同时有 AMAP_KEY 时不会再把
        # 已逐步停用的共享 Host 放在真实天气主链上。
        if real is None and qweather_key and not amap_key:
            real = self._qweather(loc, qweather_key)
        return real or f"暂时无法获取{loc}的实时天气，请稍后再试"

    # 和风公开城市编码：只授权"天气预报"也能查这些常用城市，无需 GeoAPI 权限
    _CITY_ID = {
        "北京": "101010100", "上海": "101020100", "广州": "101280101", "深圳": "101280601",
        "天津": "101030100", "重庆": "101040100", "成都": "101270101", "杭州": "101210101",
        "南京": "101190101", "武汉": "101200101", "西安": "101110101", "苏州": "101190401",
        "长沙": "101250101", "郑州": "101180101", "青岛": "101120201",
    }

    # 高德天气接口使用行政区 adcode。这里只覆盖与手机确定性天气路由相同的城市，
    # 表外城市继续交给和风专属 Host/完整意图链，绝不模糊匹配成其他城市。
    _AMAP_ADCODE = {
        "北京": "110000", "上海": "310000", "广州": "440100", "深圳": "440300",
        "天津": "120000", "重庆": "500000", "成都": "510100", "杭州": "330100",
        "南京": "320100", "武汉": "420100", "西安": "610100", "苏州": "320500",
        "长沙": "430100", "郑州": "410100", "青岛": "370200",
    }

    def _amap_weather(self, loc: str, key: str):
        """高德 Web Service 实况天气；只接受白名单城市与官方成功码。"""
        import json
        import urllib.parse
        import urllib.request

        city, adcode = None, None
        for name, code in self._AMAP_ADCODE.items():
            if name in loc or loc in name:
                city, adcode = name, code
                break
        if adcode is None:
            return None

        query = urllib.parse.urlencode({
            "city": adcode,
            "extensions": "base",
            "output": "JSON",
            "key": key,
        })
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        try:
            with opener.open(
                f"https://restapi.amap.com/v3/weather/weatherInfo?{query}",
                timeout=3.0,
            ) as response:
                payload = json.loads(response.read().decode("utf-8"))
            if payload.get("status") != "1" or payload.get("infocode") != "10000":
                raise ValueError(f"高德返回 infocode={payload.get('infocode')}")
            lives = payload.get("lives") or []
            if not lives:
                raise ValueError("高德返回空 lives")
            now = lives[0]
            reported_city = str(now.get("city") or city).strip()
            weather = str(now.get("weather") or "").strip()
            temperature = str(now.get("temperature") or "").strip()
            wind_direction = str(now.get("winddirection") or "").strip()
            wind_power = str(now.get("windpower") or "").strip()
            humidity = str(now.get("humidity") or "").strip()
            if not weather or not temperature:
                raise ValueError("高德天气关键字段缺失")
            wind = f"，{wind_direction}风{wind_power}级" if wind_direction and wind_power else ""
            humid = f"，湿度{humidity}%" if humidity else ""
            return f"{reported_city}：{weather}，{temperature}℃{wind}{humid}"
        except Exception as e:  # noqa: BLE001
            logger.warning("高德天气失败(%s)", type(e).__name__)
            return None

    def _qweather(self, loc: str, key: str):
        """和风天气(配 QWEATHER_KEY 才走)：先查内置城市表(只需"天气预报"权限)，表外走 GeoAPI。
        新版和风需**专属 API Host**(控制台获取) → 设环境变量 QWEATHER_HOST；失败返回 None。"""
        import gzip
        import json
        import os
        import urllib.parse
        import urllib.request

        host = (os.environ.get("QWEATHER_HOST", "devapi.qweather.com")
                .strip().replace("https://", "").replace("http://", "").rstrip("/"))
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))  # 禁代理直连

        def fetch(url):
            with opener.open(url, timeout=4) as r:
                raw = r.read()
            if raw[:2] == b"\x1f\x8b":  # 和风响应是 gzip 压缩，需解压
                raw = gzip.decompress(raw)
            return json.loads(raw.decode("utf-8"))

        cid, city = None, loc
        for name, _id in self._CITY_ID.items():
            if name in loc or loc in name:
                cid, city = _id, name
                break
        if cid is None:  # 表外 → GeoAPI（需额外授权 GeoAPI；失败时绝不回退服务器城市/北京）
            try:
                gj = fetch(f"https://{host}/geo/v2/city/lookup?"
                           f"location={urllib.parse.quote(loc)}&key={key}")
                cid, city = gj["location"][0]["id"], gj["location"][0]["name"]
            except Exception as e:  # noqa: BLE001
                logger.warning("GeoAPI 失败(%s，可能未授权)，拒绝猜测城市", type(e).__name__)
                return None
        try:
            wj = fetch(f"https://{host}/v7/weather/now?location={cid}&key={key}")
            if wj.get("code") != "200":
                raise ValueError(f"和风返回 code={wj.get('code')}")
            n = wj["now"]
            return (f"{city}：{n['text']}，{n['temp']}℃（体感{n['feelsLike']}℃），"
                    f"{n['windDir']}{n['windScale']}级")
        except Exception as e:  # noqa: BLE001
            logger.warning("和风 now 失败(%s)", type(e).__name__)
            return None


def make_terminal(cfg, override: str | None = None) -> MockTerminal:
    """按配置/覆盖创建终端：real=真实执行低风险动作；其余=mock(可断言,评测安全)。"""
    mode = (override or cfg.get("response.terminal", "mock") or "mock").lower()
    if mode == "real":
        logger.info("终端执行器: RealTerminal(真实低风险动作)")
        return RealTerminal()
    return MockTerminal()
