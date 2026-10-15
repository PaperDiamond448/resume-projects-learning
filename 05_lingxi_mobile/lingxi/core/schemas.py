"""多模态事件 Schema —— 全系统数据流的统一契约。

时间戳约定：浮点秒，同一会话内所有模态共用同一时钟（见 clock.py）。
live 运行时用单调钟，batch 运行时用媒体文件内的时间轴。
模块之间只允许传这里定义的类型，不允许传裸 dict。
"""
from __future__ import annotations

import uuid
from dataclasses import asdict, dataclass, field
from enum import Enum


class Modality(str, Enum):
    SPEECH = "speech"
    VISION = "vision"
    TEXT = "text"


class VisionKind(str, Enum):
    GESTURE = "gesture"          # thumbs_up / thumbs_down / ok / wave / point / fist ...
    EXPRESSION = "expression"    # positive / negative / neutral
    HEAD_MOTION = "head_motion"  # nod / shake
    GAZE = "gaze"                # at_screen / away


def _new_id() -> str:
    return uuid.uuid4().hex[:10]


@dataclass(kw_only=True)
class ModalityEvent:
    """所有模态事件的基类。t_start/t_end 为该事件在统一时间轴上的起止。"""

    modality: Modality
    t_start: float
    t_end: float
    confidence: float = 1.0
    event_id: str = field(default_factory=_new_id)

    @property
    def duration(self) -> float:
        return self.t_end - self.t_start

    def overlaps(self, t_from: float, t_to: float) -> bool:
        return self.t_end >= t_from and self.t_start <= t_to

    def to_dict(self) -> dict:
        d = asdict(self)
        d["type"] = type(self).__name__
        return d


@dataclass
class WordTiming:
    """词级时间戳，对齐层据此做更细粒度的锚定。"""

    word: str
    t_start: float
    t_end: float


@dataclass(kw_only=True)
class SpeechUtterance(ModalityEvent):
    """一段完整话语（VAD 断句 + ASR 的产物）。"""

    modality: Modality = Modality.SPEECH
    text: str
    words: list[WordTiming] = field(default_factory=list)
    language: str = "zh"
    # 副语言特征：volume(0~1) / speech_rate(字每秒) / emotion(可选) 等
    paralinguistic: dict = field(default_factory=dict)


@dataclass(kw_only=True)
class VisionEvent(ModalityEvent):
    """一次视觉感知结果（手势 / 表情 / 头部动作 / 注视）。"""

    modality: Modality = Modality.VISION
    kind: VisionKind
    label: str
    extra: dict = field(default_factory=dict)


@dataclass(kw_only=True)
class TextInput(ModalityEvent):
    """用户键入的文本（点击瞬间 t_start == t_end）。"""

    modality: Modality = Modality.TEXT
    text: str
    source: str = "keyboard"


@dataclass
class MultimodalFrame:
    """对齐层的输出：以一条话语（或文本输入）为锚，聚合窗口内的视觉证据与上下文。

    意图理解层的唯一输入类型。context 携带模拟界面状态、对话历史摘要等。
    """

    anchor: SpeechUtterance | TextInput
    vision_events: list[VisionEvent] = field(default_factory=list)
    context: dict = field(default_factory=dict)
    # 以下两项由 fusion 层填充：
    vision_summary: list[dict] = field(default_factory=list)  # 聚合后的视觉证据
    precheck: dict = field(default_factory=dict)              # 冲突预检结果
    frame_id: str = field(default_factory=_new_id)

    @property
    def anchor_text(self) -> str:
        return self.anchor.text

    def to_dict(self) -> dict:
        return {
            "frame_id": self.frame_id,
            "anchor": self.anchor.to_dict(),
            "vision_events": [e.to_dict() for e in self.vision_events],
            "vision_summary": self.vision_summary,
            "precheck": self.precheck,
            "context": self.context,
        }
