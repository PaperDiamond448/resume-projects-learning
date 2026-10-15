"""证据文本化 —— MultimodalFrame → 送入 LLM 的结构化中文描述（D5/D6 决策）。

这是隐私边界所在：出端的只有这里生成的文本，绝无图像/音频。
另含 ASCII 时间轴渲染器（调试 + 文档配图素材）。
"""
from __future__ import annotations

from ..core.schemas import MultimodalFrame, SpeechUtterance

_KIND_ZH = {"gesture": "手势", "expression": "表情", "head_motion": "头部动作", "gaze": "注视"}
_LABEL_ZH = {
    "thumb_up": "竖大拇指", "thumb_down": "拇指向下", "open_palm": "张开手掌",
    "pointing": "食指指向", "victory": "比V/胜利", "fist": "握拳", "ily": "我爱你手势",
    "positive": "积极", "negative": "消极", "neutral": "中性",
    "nod": "点头", "shake": "摇头", "at_screen": "看向屏幕", "away": "看向别处",
}
_RELATION_ZH = {"during": "与话语同时", "before": "在开口之前", "after": "在话语结束后"}
_VERDICT_ZH = {
    "agree": "语音与视觉极性一致",
    "conflict": "语音与视觉极性冲突",
    "no_polarity": "无极性对立",
    "vision_ambiguous": "视觉信号自身矛盾",
    "defer": "用户表达暂缓/稍等",
}


def frame_to_evidence_text(frame: MultimodalFrame) -> str:
    """生成意图理解 Prompt 中的【多模态证据】段。"""
    a = frame.anchor
    lines = []

    if isinstance(a, SpeechUtterance):
        para = a.paralinguistic
        extra = ""
        if para:
            extra = f"（语速{para.get('speech_rate', '?')}字/秒, 音量{para.get('volume_rms', '?')}）"
        lines.append(f"[语音] ({a.t_start:.1f}s-{a.t_end:.1f}s)「{a.text}」{extra}")
    else:
        lines.append(f"[文本输入] 「{a.text}」")

    if not frame.vision_summary:
        lines.append("[视觉] 窗口内无视觉事件")
    for ev in frame.vision_summary:
        kind = _KIND_ZH.get(ev["kind"], ev["kind"])
        label = _LABEL_ZH.get(ev["label"], ev["label"])
        count = f"×{ev['count']}" if ev["count"] > 1 else ""
        lines.append(
            f"[视觉-{kind}] {label}{count} ({ev['t_first']}s-{ev['t_last']}s, "
            f"{_RELATION_ZH[ev['relation']]}, 置信{ev['confidence']})"
        )

    p = frame.precheck
    if p:
        desc = _VERDICT_ZH.get(p["verdict"], p["verdict"])
        detail = []
        if p["speech_evidence"]:
            detail.append(f"语音极性={p['speech_polarity']}(命中「{p['speech_evidence']}」)")
        if p["vision_evidence"]:
            v_labels = "、".join(_LABEL_ZH.get(v, v) for v in p["vision_evidence"])
            detail.append(f"视觉极性={p['vision_polarity']}({v_labels})")
        if p.get("vision_details"):
            ds = []
            for d in p["vision_details"]:
                label = _LABEL_ZH.get(d.get("label"), d.get("label"))
                ds.append(
                    f"{label}(置信{d.get('confidence')}, {d.get('relation')}, "
                    f"overlap={d.get('overlap_ratio')}, {d.get('strength')})"
                )
            detail.append("视觉细节=" + "、".join(ds))
        lines.append(f"[规则预检] {desc}" + (f"：{'; '.join(detail)}" if detail else ""))
    return "\n".join(lines)


def render_timeline(frame: MultimodalFrame, width: int = 56) -> str:
    """ASCII 时间轴：话语与各视觉事件的时间关系一目了然（调试/文档用）。"""
    a = frame.anchor
    items: list[tuple[str, float, float]] = [(f"语音「{a.text}」", a.t_start, a.t_end)]
    for e in frame.vision_events:
        items.append((_LABEL_ZH.get(e.label, e.label), e.t_start, e.t_end))

    t0 = min(t for _, t, _ in items)
    t1 = max(t for _, _, t in items)
    span = max(t1 - t0, 1e-6)
    name_w = max(len(n) for n, _, _ in items) + 2

    out = [f"{'时间轴':<{name_w}}{t0:.1f}s {'─' * (width - 12)} {t1:.1f}s"]
    for name, s, e in items:
        lo = round((s - t0) / span * width)
        hi = max(lo + 1, round((e - t0) / span * width))
        bar = " " * lo + "█" * (hi - lo)
        out.append(f"{name:<{name_w}}{bar}")
    return "\n".join(out)
