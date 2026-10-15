"""融合层：话语锚定对齐、视觉证据聚合、冲突预检、证据文本化。"""

from .aligner import FrameAligner, aggregate_vision
from .conflict import precheck
from .evidence import frame_to_evidence_text, render_timeline

__all__ = [
    "FrameAligner",
    "aggregate_vision",
    "precheck",
    "frame_to_evidence_text",
    "render_timeline",
]
