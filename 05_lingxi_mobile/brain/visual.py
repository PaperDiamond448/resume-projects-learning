"""灵犀 Mobile · 视觉兜底（路线 C）—— Set-of-Mark 视觉选择。

何时触发：GUI-agent 的无障碍读屏稀疏 / 连续无进展时（a11y 认得出字、认不出元素的
视觉类型——直播间 vs 录播、歌单 vs 听歌识曲）。安卓端 MediaProjection 截图 + a11y
可点元素框上传 → 大脑在截图上画编号框（Set-of-Mark）→ 调 ≤3B 视觉模型出"点几号"
→ 映射回该元素中心点 → 安卓端 tap。

为什么 SoM 而不是让 VLM 出裸坐标（关键设计）：
- a11y 已经给出可靠的元素 bounds，VLM 只需做"选择题"（哪个编号是直播间），
  不需要像素级 grounding —— VLM 最不稳的那部分（精确定位）由 a11y 兜底。
- 与注视指代"a11y 离散候选 + 就近选"完全同构（同一套"离散化候选、模型只做选择题"哲学）。
- 返回坐标永远取自 a11y 的【原始 bounds】，绝不使用 VLM 的像素输出 → 零缩放误差。
  （截图发给 VLM 前会缩放到模型友好尺寸，但 mark→坐标走原始 bounds，不受缩放影响。）

合规（赛题红线）：只允许精确的 `qwen2.5-vl-3b-instruct`，占一个 3B 数据处理模型槽。
Cloud-3 默认接用户自托管的 OpenAI 兼容端点（已用台式机 LM Studio 跑通过）；
也可在未来显式配置其他合规的同名模型服务。
视觉Key只从独立的 `LINGXI_VLM_API_KEY` 读取，不复用中国区14B的`DASHSCOPE_API_KEY`，
绝不写入代码或回退到第三方替身。云端能力延期时由`LINGXI_CLOUD_VLM_ENABLED=false`硬关闭；
显式本地OpenAI兼容服务仍可在开发环境按原配置启用。
截图必须先通过 Android 本地隐私门和 Brain 二次证据校验；本模块不接收人脸/语音生物特征。
"""
from __future__ import annotations

import base64
import io
import json
import os
import re
from typing import Any

import httpx
from openai import OpenAI
from PIL import Image, ImageDraw, ImageFont

# —— VLM 端点配置（全 env 可配，零落盘）——
_DEF_BASE_URL = "http://127.0.0.1:1234/v1"
_DEF_MODEL = "qwen2.5-vl-3b-instruct"
_ALLOWED_MODELS = {_DEF_MODEL}
_MAX_SIDE = 1024            # 发给 VLM 前截图长边上限（Qwen2.5-VL 友好尺寸、省 token）
_BADGE_COLORS = ["#e6194B", "#3cb44b", "#4363d8", "#f58231", "#911eb4",
                 "#42d4f4", "#f032e6", "#bfef45", "#fabed4", "#469990"]

_client: OpenAI | None = None
_client_signature: tuple[str, str, str, float] | None = None


class VLMConfigurationError(RuntimeError):
    """Raised when the configured backend can violate the frozen Cloud-3 contract."""


def _cfg(name: str, default: str = "") -> str:
    return os.environ.get(name, default).strip()


def _vlm_model() -> str:
    return _cfg("LINGXI_VLM_MODEL") or _DEF_MODEL


def _get_client() -> OpenAI:
    """懒构造 VLM 客户端（OpenAI 兼容）。trust_env=False 直连绕开失效系统代理。"""
    global _client, _client_signature
    enabled = _cfg("LINGXI_CLOUD_VLM_ENABLED").lower()
    if enabled in {"0", "false", "off", "no"}:
        raise VLMConfigurationError("cloud_vlm_disabled")
    base_url = _cfg("LINGXI_VLM_BASE_URL") or _DEF_BASE_URL
    model = _vlm_model()
    key = _cfg("LINGXI_VLM_API_KEY")
    timeout = float(_cfg("LINGXI_VLM_TIMEOUT") or "30")
    if model not in _ALLOWED_MODELS:
        raise VLMConfigurationError(f"model_not_allowed:{model}")
    if not key:
        raise VLMConfigurationError("vlm_api_key_missing")
    signature = (base_url, model, key, timeout)
    if _client is None or _client_signature != signature:
        _client = OpenAI(
            base_url=base_url, api_key=key, timeout=timeout, max_retries=0,
            http_client=httpx.Client(trust_env=False, timeout=timeout),
        )
        _client_signature = signature
    return _client


def _load_font(size: int) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    """编号徽标字体：数字是 ASCII，默认字体也能渲染，但 truetype 更清晰。找不到退回默认。"""
    for p in ("C:/Windows/Fonts/arialbd.ttf", "C:/Windows/Fonts/msyhbd.ttc",
              "C:/Windows/Fonts/DejaVuSans-Bold.ttf"):
        if os.path.exists(p):
            try:
                return ImageFont.truetype(p, size)
            except Exception:  # noqa: BLE001
                pass
    return ImageFont.load_default()


def render_som(png_bytes: bytes, elements: list[dict]) -> tuple[bytes, dict[int, tuple[int, int]]]:
    """在截图上画编号框（Set-of-Mark）。
    返回 (标注后 PNG 字节, {编号: (原图坐标系下的中心 x, y)})。
    elements 每项: {"id": int, "text": str, "bounds": [left, top, right, bottom]}（原图像素）。
    """
    img = Image.open(io.BytesIO(png_bytes)).convert("RGB")
    W, H = img.size
    scale = min(1.0, _MAX_SIDE / max(W, H)) if max(W, H) > 0 else 1.0
    canvas = img.resize((max(1, int(W * scale)), max(1, int(H * scale)))) if scale < 1 else img.copy()
    draw = ImageDraw.Draw(canvas)
    font = _load_font(max(14, int(22 * scale)))
    centers: dict[int, tuple[int, int]] = {}
    for el in elements:
        try:
            i = int(el["id"])
            l, t, r, b = (int(v) for v in el["bounds"])
        except (KeyError, ValueError, TypeError):
            continue
        centers[i] = ((l + r) // 2, (t + b) // 2)  # 原图坐标系（返回给安卓用，不受缩放影响）
        sl, st, sr, sb = (int(v * scale) for v in (l, t, r, b))
        color = _BADGE_COLORS[(i - 1) % len(_BADGE_COLORS)]
        draw.rectangle([sl, st, sr, sb], outline=color, width=2)  # 🐛 bug6 学长 review：框线细一点、少糊缩略图（角标已在框上沿外侧）
        # 左上角编号徽标（实心底 + 数字），保证编号清晰可读
        tag = str(i)
        tw = draw.textlength(tag, font=font)
        bw, bh = int(tw) + 10, max(18, int(26 * scale))
        bx, by = sl, max(0, st - bh)  # 贴在框上沿外侧，框内空间不被遮挡
        draw.rectangle([bx, by, bx + bw, by + bh], fill=color)
        draw.text((bx + 5, by + 1), tag, fill="white", font=font)
    out = io.BytesIO()
    canvas.save(out, format="PNG")
    return out.getvalue(), centers


def _parse_choice(text: str, valid_ids: set[int]) -> dict[str, Any]:
    """从严到松解析 VLM 输出（借 UAVLLMs GUI-Agent 思路）：
    先试 JSON，再正则抠 action + 编号；都失败兜底 done。绝不抛异常中断 agent。"""
    raw = (text or "").strip()
    raw = re.sub(r"```(json)?|```", "", raw).strip()  # 剥代码块围栏

    action, mark, reason, thought = "", None, "", ""
    # ① 严：直接 JSON
    s, e = raw.find("{"), raw.rfind("}")
    if s != -1 and e > s:
        try:
            obj = json.loads(raw[s:e + 1])
            action = str(obj.get("action", "")).strip().lower()
            reason = str(obj.get("reason", "")).strip()[:20]
            thought = str(obj.get("thought", "")).strip()[:60]
            mv = obj.get("mark", obj.get("target"))
            if mv is not None:
                m = re.search(r"\d+", str(mv))
                if m:
                    mark = int(m.group())
        except (json.JSONDecodeError, ValueError, TypeError):
            pass
    # ② 松：正则兜动作词
    if not action:
        low = raw.lower()
        for a in ("done", "scroll", "back", "input", "tap", "click"):
            if a in low:
                action = "tap" if a == "click" else a
                break
    # ③ 松：兜编号——只认 "mark" 附近的数字（🐛 bug7 学长 review：别从全文抓首个数字，
    #    截断的 thought「第2个框看起来…」会误抓 2 硬点；抓不到就保持 None → 归一为收手，不硬点错编号）
    if mark is None:
        m = re.search(r'mark["\s:：=]*\s*(\d+)', raw, re.IGNORECASE)
        if m:
            mark = int(m.group(1))

    # 归一动作（VLM 常吐 "click"/"click|done" 等杂串，曾导致选对了编号却被当 garbage 丢弃→白白不点）：
    # P4②：如果同时给了 scroll 和合法 mark，这是自相矛盾；按“看到了可点候选”处理为 tap。
    # back 仍优先；否则只要给了【合法编号】就视为点击该编号；没有编号才 scroll/done。
    if "back" in action:
        action = "back"
    elif mark in valid_ids:
        action = "tap"
    elif "scroll" in action:
        action = "scroll"
    else:
        action = "done"
    if action == "tap" and mark not in valid_ids:  # 双保险
        return {"action": "done", "mark": None, "reason": reason or "无有效编号", "thought": thought}
    return {"action": action, "mark": mark, "reason": reason, "thought": thought}


def _parse_reflection(text: str) -> dict[str, Any]:
    """Parse action reflection output into a small, stable status set.

    Status meanings mirror MobileAgent/mobile-use style post-action checks:
    SUCCESS=the subgoal is now satisfied, WRONG_PAGE=action led to an unrelated
    page, NO_CHANGE=screen did not respond, UNSURE=do not trust the judgment.
    """
    raw = (text or "").strip()
    raw = re.sub(r"```(json)?|```", "", raw).strip()
    status, reason, thought = "UNSURE", "", ""
    s, e = raw.find("{"), raw.rfind("}")
    if s != -1 and e > s:
        try:
            obj = json.loads(raw[s:e + 1])
            status = str(obj.get("status", obj.get("result", ""))).strip().upper()
            reason = str(obj.get("reason", "")).strip()[:40]
            thought = str(obj.get("thought", "")).strip()[:80]
        except (json.JSONDecodeError, ValueError, TypeError):
            pass
    if status not in {"SUCCESS", "WRONG_PAGE", "NO_CHANGE", "UNSURE"}:
        upper = raw.upper()
        for candidate in ("WRONG_PAGE", "NO_CHANGE", "SUCCESS", "UNSURE"):
            if candidate in upper:
                status = candidate
                break
        else:
            status = "UNSURE"
    return {"status": status, "reason": reason, "thought": thought}


def visual_pick(goal: str, history: list[str], png_bytes: bytes,
                elements: list[dict], hints: str = "") -> dict[str, Any]:
    """SoM 视觉选择主入口：画编号 → VLM 选号 → 映射回原图中心坐标。
    返回 {action, x, y, mark, reason, model}；坐标取自 a11y 原始 bounds（零缩放误差）。
    hints=该 App 攻略要点（注入 prompt 让 3B 视觉带 App 知识选卡）；异常降级 error/done，不让 agent 卡死。"""
    valid_ids = {int(el["id"]) for el in elements if "id" in el}
    if not valid_ids:
        # a11y 没给可点元素（canvas/游戏/纯图标）→ 裸坐标兜底留待后续，本期先收手
        return {"action": "done", "x": None, "y": None, "mark": None,
                "reason": "无可点元素", "model": _vlm_model()}

    annotated, centers = render_som(png_bytes, elements)
    data_url = "data:image/png;base64," + base64.b64encode(annotated).decode()

    # 把每个编号对应的 a11y 文字一并给 VLM（文字+视觉双线索；文字判别交文本，视觉判别交它）
    listing = "\n".join(f"{el['id']}. {str(el.get('text', '')).strip()[:40]}"
                        for el in elements if "id" in el)
    hist = "；".join(history[-4:]) if history else "无"
    sys_p = ("你在操作安卓手机界面。截图上每个可点元素被框出并标了编号。"
             "你只需判断：为完成任务，下一步该点哪个【编号】，或滚动/返回/收工。"
             "只看截图里编号框的【视觉外观】做判断（如哪个是正在直播、哪个是视频缩略图），"
             "输出紧凑 JSON、不要任何多余解释。")
    hint_line = f"【本 App 操作要点(选号务必遵守)】：{hints}\n" if hints else ""
    user_p = (
        f"任务：{goal}\n已执行：{hist}\n"
        f"{hint_line}"
        f"屏上编号元素：\n{listing}\n"
        '先想后做：先在 "thought" 里说清【每个编号框看起来是什么类型（直播间/视频/歌单/按钮…）、'
        '哪个最符合任务目标、为什么】，再给动作。\n'
        '输出 JSON：{"thought":"…(一句话推理)","action":"tap|scroll|back|done",'
        '"mark":<编号或null>,"reason":"≤8字"}\n'
        "规则：①已达成最终目标→done；②目标在屏上→tap 对应编号；"
        "③要找的不在本屏→scroll（mark 留 null）；④进错页→back。"
    )
    msgs = [
        {"role": "system", "content": sys_p},
        {"role": "user", "content": [
            {"type": "image_url", "image_url": {"url": data_url}},
            {"type": "text", "text": user_p},
        ]},
    ]
    try:
        resp = _get_client().chat.completions.create(
            model=_vlm_model(), messages=msgs, temperature=0.0, max_tokens=96)
        choice = _parse_choice(resp.choices[0].message.content or "", valid_ids)
    except Exception as e:  # noqa: BLE001 — VLM 后端不可用：返回 error（区别于 done），让安卓端诚实收手而非假装完成
        return {"action": "error", "x": None, "y": None, "mark": None,
                "reason": f"VLM失败:{type(e).__name__}", "thought": "视觉模型未连接",
                "model": _vlm_model()}

    x = y = None
    if choice["action"] == "tap" and choice["mark"] in centers:
        x, y = centers[choice["mark"]]
    return {"action": choice["action"], "x": x, "y": y, "mark": choice["mark"],
            "reason": choice["reason"], "thought": choice.get("thought", ""), "model": _vlm_model()}


_NUM_RE = re.compile(r"[-+]?\d+(?:[.,]\d+)?")
_UNIT_MULT = {"亿": 1e8, "万": 1e4, "w": 1e4, "千": 1e3, "k": 1e3}


def _parse_number(raw: Any) -> float | None:
    """把 VLM 抽出的数值字符串（可能带单位/符号：1200万、4.8分、¥4999、销量2.3万+）解析成可比较的 float。"""
    if raw is None:
        return None
    s = str(raw).strip()
    if not s:
        return None
    m = _NUM_RE.search(s.replace(",", ""))
    if not m:
        return None
    val = float(m.group())
    for unit, mult in _UNIT_MULT.items():
        if unit in s.lower():
            val *= mult
            break
    return val


def _parse_extract(text: str, valid_ids: set[int]) -> list[dict[str, Any]]:
    """从严到松解析结构化抽取结果：期望 JSON 数组 [{"mark":N,"title":"…","value":"…"}]。
    解不出/编号不合法的条目直接丢弃，不让脏数据参与后续比较。"""
    raw = (text or "").strip()
    raw = re.sub(r"```(json)?|```", "", raw).strip()
    items: list[dict[str, Any]] = []
    s, e = raw.find("["), raw.rfind("]")
    if s == -1 or e <= s:
        return items
    try:
        arr = json.loads(raw[s:e + 1])
    except (json.JSONDecodeError, ValueError):
        return items
    if not isinstance(arr, list):
        return items
    for it in arr:
        if not isinstance(it, dict):
            continue
        mv = it.get("mark", it.get("id"))
        m = re.search(r"\d+", str(mv)) if mv is not None else None
        if not m:
            continue
        mark = int(m.group())
        if mark not in valid_ids:
            continue
        items.append({
            "mark": mark,
            "title": str(it.get("title", "")).strip()[:60],
            "value_raw": it.get("value"),
            "value": _parse_number(it.get("value")),
        })
    return items


_LOWER_IS_BETTER_RE = re.compile(r"便宜|最低|最少|价格低")


def infer_higher_is_better(text: str) -> bool:
    """从目标文本猜比较方向：'哪家便宜/最便宜'→越低越好；其余(播放量/点赞/评分/销量最高等)→越高越好。"""
    return not _LOWER_IS_BETTER_RE.search(text or "")


def extract_pick(goal: str, png_bytes: bytes, elements: list[dict],
                 metric: str = "", higher_is_better: bool | None = None,
                 hints: str = "") -> dict[str, Any]:
    """T-14 结构化抽取：VLM 只负责【看图抽数字】，"谁最大/最小"的比较交代码——
    与 SoM"视觉只选号、坐标交 a11y"同一套哲学的第二个落地（数值比较交代码、视觉只做抽取）。
    返回 {action, x, y, mark, reason, items, model}；items 是抽取到的全部候选，供上层展示证据/复核。
    坐标同 visual_pick，取自 a11y 原始 bounds（零缩放误差）。"""
    valid_ids = {int(el["id"]) for el in elements if "id" in el}
    if not valid_ids:
        return {"action": "done", "x": None, "y": None, "mark": None,
                "items": [], "reason": "无可点元素", "model": _vlm_model()}
    if higher_is_better is None:
        higher_is_better = infer_higher_is_better(f"{goal} {metric}")

    annotated, centers = render_som(png_bytes, elements)
    data_url = "data:image/png;base64," + base64.b64encode(annotated).decode()
    listing = "\n".join(f"{el['id']}. {str(el.get('text', '')).strip()[:40]}"
                        for el in elements if "id" in el)
    hint_line = f"【本 App 操作要点】：{hints}\n" if hints else ""
    metric_line = f"要比较的指标：{metric}\n" if metric else ""
    sys_p = ("你在操作安卓手机界面。截图上每个候选项被框出并标了编号。"
             "你只负责【抽取每个编号对应的标题和数值】，不要自己判断谁最大/最小——"
             "比较工作交给程序代码计算，不能由你估读。输出紧凑 JSON、不要任何多余解释。")
    user_p = (
        f"任务：{goal}\n{metric_line}{hint_line}"
        f"屏上编号元素：\n{listing}\n"
        '为每个能看清指标的编号抽取 {"mark":编号,"title":"标题(截断)",'
        '"value":"该项数值原文，如 1200万/4.8分/¥4999，看不清填 null"}\n'
        "输出 JSON 数组：[{...}, ...]，只列出屏上实际看到该指标的编号，不要瞎编数字。"
    )
    msgs = [
        {"role": "system", "content": sys_p},
        {"role": "user", "content": [
            {"type": "image_url", "image_url": {"url": data_url}},
            {"type": "text", "text": user_p},
        ]},
    ]
    try:
        resp = _get_client().chat.completions.create(
            model=_vlm_model(), messages=msgs, temperature=0.0, max_tokens=400)
        items = _parse_extract(resp.choices[0].message.content or "", valid_ids)
    except Exception as e:  # noqa: BLE001 — VLM 后端不可用：error（区别于 done），让安卓端诚实收手
        return {"action": "error", "x": None, "y": None, "mark": None,
                "items": [], "reason": f"VLM失败:{type(e).__name__}", "model": _vlm_model()}

    comparable = [it for it in items if it["value"] is not None]
    if not comparable:
        return {"action": "done", "x": None, "y": None, "mark": None,
                "items": items, "reason": "未抽到可比较数值", "model": _vlm_model()}

    winner = (max if higher_is_better else min)(comparable, key=lambda it: it["value"])
    x, y = centers.get(winner["mark"], (None, None))
    return {
        "action": "tap", "x": x, "y": y, "mark": winner["mark"],
        "reason": f"{winner['title']}={winner['value_raw']}"[:40], "items": items,
        "model": _vlm_model(),
    }


def reflect_action(goal: str, subgoal: str, action: str, target: str,
                   before_text: str, after_text: str, png_bytes: bytes | None,
                   hints: str = "", text_llm: Any = None) -> dict[str, Any]:
    """Post-action reflection: decide whether the last action actually helped.

    This is intentionally compact. The Android side uses it as a brake: after a
    high-risk tap (card/video/live-room), the agent must prove it is on the
    expected page before advancing.

    text_llm：没带截图时用来推理的通用文本 LLM（PC 的 14B，server.py 传入）。2026-07-03 真机
    实测抓到：纯文本验收（没截图，只比 before/after 读屏文本）以前也硬走 VLM 客户端，
    台式机 VLM(3B) 一断线，连这种本该由 14B 就能稳定判断的文本推理也跟着报废——GUI-agent
    因此在明明已经成功、只是"验收"这一步失败的情况下反复重试同一子步骤（用户真机观察到的
    "进了每日推荐页却还在原地反复上下滑"）。规则：**有截图才用 VLM，没截图优先用 text_llm**。
    """
    before = (before_text or "")[:1200]
    after = (after_text or "")[:1200]
    if before and after and before == after:
        return {
            "status": "NO_CHANGE",
            "reason": "屏幕未变化",
            "thought": "动作后读屏文本没有变化",
            "model": "heuristic",
        }

    hint_line = f"【本 App 操作要点】：{hints}\n" if hints else ""
    sys_p = (
        "你是安卓 GUI agent 的动作后验收器。你只判断刚才一步执行后，"
        "当前页面是否符合当前子步骤。输出紧凑 JSON，不要多余解释。"
    )
    user_p = (
        f"总目标：{goal}\n当前子步骤：{subgoal}\n刚执行动作：{action} {target}\n"
        f"{hint_line}"
        f"动作前读屏文本：\n{before}\n"
        f"动作后读屏文本：\n{after}\n"
        '输出 JSON：{"thought":"…","status":"SUCCESS|WRONG_PAGE|NO_CHANGE|UNSURE","reason":"≤12字"}\n'
        "判断标准：\n"
        "SUCCESS=当前子步骤已经达成，例如进入了目标直播间/视频播放页/正确栏目；\n"
        "WRONG_PAGE=进入了和子步骤不匹配的页面，例如本该进入直播栏目却进了推荐视频/颜值分区/广告页；\n"
        "NO_CHANGE=动作后页面基本没反应；\n"
        "UNSURE=证据不足，别硬判成功。\n"
        "**入口按钮和目标页标题常常同名**（如点【每日推荐】按钮进的就是标题也叫「每日推荐」的歌曲列表页）——"
        "别因为动作后文本里还留着这个关键词就犹豫；真正该看的是【动作后是否新增了一批并列的短条目】"
        "（多首歌名/多个列表项/多张卡片标题），这才是「已经从按钮页进入列表页」的实据，出现即判 SUCCESS；\n"
        "宁可 UNSURE，也不要把看不出目标状态的页面判成 SUCCESS。"
    )

    use_text_llm = not png_bytes and text_llm is not None
    model_name = "text-llm" if use_text_llm else _vlm_model()
    try:
        if use_text_llm:
            raw = text_llm.chat(
                [{"role": "system", "content": sys_p}, {"role": "user", "content": user_p}],
                temperature=0.0, max_tokens=96,
            )
        else:
            content: list[dict[str, Any]] | str
            if png_bytes:
                data_url = "data:image/png;base64," + base64.b64encode(png_bytes).decode()
                content = [
                    {"type": "image_url", "image_url": {"url": data_url}},
                    {"type": "text", "text": user_p},
                ]
            else:
                content = user_p
            msgs = [{"role": "system", "content": sys_p}, {"role": "user", "content": content}]
            resp = _get_client().chat.completions.create(
                model=_vlm_model(), messages=msgs, temperature=0.0, max_tokens=96)
            raw = resp.choices[0].message.content or ""
        out = _parse_reflection(raw)
        out["model"] = model_name
        return out
    except Exception as e:  # noqa: BLE001
        # 带截图走 VLM 失败（台式机 3B 掉线）→ 不直接 UNSURE：扔掉截图、降级 14B 文本再判一次。
        # 2026-07-03 真机实锤：用户开了屏幕视觉后验收自动带图→必走 VLM→VLM 断线连环 APIConnectionError，
        # 反而比不开视觉更糟（文本 before/after 本来就够判"已进列表页"）。截图只是加分证据，不该是单点故障。
        if text_llm is not None and not use_text_llm:
            try:
                raw = text_llm.chat(
                    [{"role": "system", "content": sys_p}, {"role": "user", "content": user_p}],
                    temperature=0.0, max_tokens=96,
                )
                out = _parse_reflection(raw)
                out["model"] = "text-llm-fallback"
                return out
            except Exception:  # noqa: BLE001
                pass
        return {
            "status": "UNSURE",
            "reason": f"反思失败:{type(e).__name__}",
            "thought": "动作后反思不可用",
            "model": model_name,
        }
