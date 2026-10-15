"""话语锚定对齐器 —— 阶段4核心。

流程（D2 决策）：
  话语(或文本输入)完成 → 取窗口 [t_start−before, t_end+after] 内的视觉事件
  → 同类合并聚合(vision_summary) → 冲突预检(precheck) → MultimodalFrame

聚合规则：
  同 (kind, label) 的事件合并为一条证据：次数、总时长、置信度按时长加权、
  与话语的时间关系(overlap_ratio: 事件与话语区间的重叠比例)。
"""
from __future__ import annotations

from collections import defaultdict

from ..core.bus import EventBus
from ..core.config import Config
from ..core.log import get_logger
from ..core.schemas import (
    Modality, MultimodalFrame, SpeechUtterance, TextInput, VisionEvent,
)
from .conflict import precheck

logger = get_logger("fusion.aligner")


def _overlap_ratio(ev: VisionEvent, t_start: float, t_end: float) -> float:
    """事件与话语区间的重叠时长 / 事件自身时长（瞬时事件按是否落在区间内）。"""
    dur = max(ev.duration, 1e-6)
    overlap = max(0.0, min(ev.t_end, t_end) - max(ev.t_start, t_start))
    return round(min(1.0, overlap / dur), 2)


def _relation(ev: VisionEvent, t_start: float, t_end: float) -> str:
    if ev.t_end <= t_start:
        return "before"   # 完全在话语前（如先竖拇指再开口）
    if ev.t_start >= t_end:
        return "after"    # 完全在话语后（如说完才点头）
    return "during"


def aggregate_vision(events: list[VisionEvent], t_start: float, t_end: float) -> list[dict]:
    """同 (kind, label) 合并为一条聚合证据，按首次出现时间排序。"""
    groups: dict[tuple, list[VisionEvent]] = defaultdict(list)
    for ev in events:
        groups[(ev.kind.value, ev.label)].append(ev)

    summary = []
    for (kind, label), evs in groups.items():
        total_dur = sum(e.duration for e in evs)
        if total_dur > 0:
            conf = sum(e.confidence * e.duration for e in evs) / total_dur
        else:
            conf = max(e.confidence for e in evs)
        best_overlap = max(_overlap_ratio(e, t_start, t_end) for e in evs)
        relations = {_relation(e, t_start, t_end) for e in evs}
        summary.append({
            "kind": kind,
            "label": label,
            "count": len(evs),
            "t_first": round(min(e.t_start for e in evs), 2),
            "t_last": round(max(e.t_end for e in evs), 2),
            "total_dur": round(total_dur, 2),
            "confidence": round(conf, 2),
            "overlap_ratio": best_overlap,
            "relation": "during" if "during" in relations else relations.pop(),
        })
    summary.sort(key=lambda s: s["t_first"])
    return summary


class FrameAligner:
    """从事件总线拉取窗口证据，组装 MultimodalFrame。

    本类是纯查询逻辑（立即返回）；live 模式下"等待 after 窗口走完再对齐"
    的节奏控制由 runtime 层负责（batch 模式感知先行完成，无需等待）。
    """

    def __init__(self, bus: EventBus, cfg: Config) -> None:
        self.bus = bus
        self.before = float(cfg.get("alignment.window_before", 1.5))
        self.after = float(cfg.get("alignment.window_after", 1.5))

    def window_of(self, anchor: SpeechUtterance | TextInput) -> tuple[float, float]:
        return anchor.t_start - self.before, anchor.t_end + self.after

    def align(self, anchor: SpeechUtterance | TextInput, context: dict | None = None) -> MultimodalFrame:
        w_from, w_to = self.window_of(anchor)
        vision = [
            e for e in self.bus.query(w_from, w_to, modality=Modality.VISION)
            if isinstance(e, VisionEvent)
        ]
        summary = aggregate_vision(vision, anchor.t_start, anchor.t_end)
        frame = MultimodalFrame(
            anchor=anchor,
            vision_events=vision,
            vision_summary=summary,
            precheck=precheck(anchor, summary),
            context=context or {},
        )
        logger.info(
            "对齐帧 %s: 「%s」+ %d类视觉证据, 预检=%s",
            frame.frame_id, anchor.text, len(summary), frame.precheck["verdict"],
        )
        return frame
