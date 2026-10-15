"""多模态冲突预检 —— 规则层（三段式消解的第一段）。

只做"快而透明"的极性对照：语音文本极性 × 视觉动作极性。
结论仅作为证据注入 LLM Prompt（第二段终裁），不直接决定意图——
预检的价值是让 LLM 显式注意到冲突，并给可解释性文档提供规则依据。

极性词表为启发式，覆盖确认/拒绝类短语；完整语义判断交给 LLM。
"""
from __future__ import annotations

from ..core.schemas import SpeechUtterance, TextInput

# 语音极性词表（按"更长的短语优先匹配"排列，避免"不用了"被"用"误判）
NEGATIVE_EXCLUDES = ("不错", "没关系", "不客气", "不用谢", "差不多")
DEFER_PHRASES = ("先别", "等等", "等一下", "等会", "稍等", "先等等", "再说吧", "再说",
                 "暂时不用", "先不用", "先别急", "别急")
NEGATIVE_PHRASES = (
    "不要", "不用", "不行", "不发", "不去", "不可以", "不用提醒", "别提醒", "别发", "别去", "算了", "行了",
    "取消", "不对", "没有", "不是", "别", "不",
)
POSITIVE_PHRASES = (
    "好的", "可以的", "可以", "好啊", "好嘞", "行吧", "发吧", "去吧", "打吧", "付吧", "付了吧", "确认",
    "确定", "同意", "没问题", "是的", "嗯嗯", "OK", "ok", "发", "嗯", "好", "行", "要", "对",
)

# 视觉极性映射: (kind, label) → polarity
VISION_POLARITY = {
    ("head_motion", "nod"): "positive",
    ("head_motion", "shake"): "negative",
    ("gesture", "thumb_up"): "positive",
    ("gesture", "thumb_down"): "negative",
    ("gesture", "victory"): "positive",
    ("expression", "positive"): "positive",
    ("expression", "negative"): "negative",
}
# 强语义动作（点头/摇头/拇指）权重高于表情（表情可能是底色情绪而非应答）
STRONG_VISION = {("head_motion", "nod"), ("head_motion", "shake"),
                 ("gesture", "thumb_up"), ("gesture", "thumb_down")}
STRONG_CONF_THRESH = 0.75
BEFORE_OVERLAP_WEAK_THRESH = 0.30
SHORT_REPLY_MAX_CHARS = 8


def speech_polarity(anchor: SpeechUtterance | TextInput) -> tuple[str, str | None]:
    """返回 (polarity, 命中短语)。仅在短句/应答语境有意义，长句多为中性指令。"""
    text = anchor.text.strip()
    bare = text.rstrip("！。!.~～？?，, ")
    compact = "".join(bare.split())
    if len(compact) > SHORT_REPLY_MAX_CHARS:
        return "neutral", None
    for p in NEGATIVE_EXCLUDES:
        if p in compact:
            return "neutral", None
    for p in DEFER_PHRASES:
        if p in compact:
            return "defer", p
    for p in NEGATIVE_PHRASES:
        if p in compact and len(p) / max(len(compact), 1) >= 0.25:
            return "negative", p
    for p in POSITIVE_PHRASES:
        if p in compact and len(p) / max(len(compact), 1) >= 0.25:
            return "positive", p
    # 带逗号的短确认（如「好，发出去」）会被标点拉低命中占比；在短答语境下仍视为确认。
    if len(compact) <= 6 and any(p in compact for p in ("好", "行", "发")):
        return "positive", "短答肯定"
    # 短句祈使兜底：「付吧」「打吧打吧」等以"吧"收尾的短句（无否定词，上面已排除）
    # 在应答语境下是肯定语气
    if len(compact) <= 8 and compact.endswith("吧"):
        return "positive", "短句祈使「~吧」"
    return "neutral", None


def vision_polarity(vision_summary: list[dict]) -> tuple[str, list[str], list[dict]]:
    """聚合视觉证据极性。强语义动作一票定调；仅表情时弱判。"""
    strong_pos, strong_neg, weak_pos, weak_neg = [], [], [], []
    details = []
    for ev in vision_summary:
        key = (ev["kind"], ev["label"])
        pol = VISION_POLARITY.get(key)
        if pol is None:
            continue
        name = f"{ev['label']}"
        conf = float(ev.get("confidence", 0.0) or 0.0)
        relation = ev.get("relation", "")
        overlap = float(ev.get("overlap_ratio", 0.0) or 0.0)
        is_temporally_weak = relation == "before" and overlap < BEFORE_OVERLAP_WEAK_THRESH
        is_strong = key in STRONG_VISION and conf >= STRONG_CONF_THRESH and not is_temporally_weak
        details.append({
            "label": name,
            "polarity": pol,
            "confidence": round(conf, 2),
            "relation": relation,
            "overlap_ratio": round(overlap, 2),
            "strength": "strong" if is_strong else "weak",
        })
        if is_strong:
            (strong_pos if pol == "positive" else strong_neg).append(name)
        else:
            (weak_pos if pol == "positive" else weak_neg).append(name)
    if strong_pos and not strong_neg:
        return "positive", strong_pos + weak_pos, details
    if strong_neg and not strong_pos:
        return "negative", strong_neg + weak_neg, details
    if strong_pos and strong_neg:
        return "mixed", strong_pos + strong_neg, details
    if weak_pos and not weak_neg:
        return "positive_weak", weak_pos, details
    if weak_neg and not weak_pos:
        return "negative_weak", weak_neg, details
    return "neutral", [], details


def precheck(anchor: SpeechUtterance | TextInput, vision_summary: list[dict]) -> dict:
    """冲突预检主入口，结果挂到 MultimodalFrame.precheck。"""
    s_pol, s_hit = speech_polarity(anchor)
    v_pol, v_hits, v_details = vision_polarity(vision_summary)
    v_base = v_pol.replace("_weak", "")

    if s_pol == "defer":
        verdict = "defer"
    elif s_pol == "neutral" or v_pol == "neutral":
        verdict = "no_polarity"      # 至少一方无极性，无所谓冲突
    elif v_pol == "mixed":
        verdict = "vision_ambiguous"
    elif v_pol.endswith("_weak") and s_pol != v_base:
        verdict = "no_polarity"      # 弱视觉证据不触发跨模态冲突翻转
    elif s_pol == v_base:
        verdict = "agree"
    else:
        verdict = "conflict"

    return {
        "verdict": verdict,           # agree / conflict / no_polarity / vision_ambiguous
        "speech_polarity": s_pol,
        "speech_evidence": s_hit,
        "vision_polarity": v_pol,
        "vision_evidence": v_hits,
        "vision_details": v_details,
    }
