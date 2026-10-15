"""事件总线 —— 感知层发布、对齐层按时间窗查询的线程安全缓冲。

设计要点：
- 感知模块（音频线程、视觉线程）publish；对齐器 query 时间窗内事件。
- subscribe 用于"话语完成即触发对齐"这类推送场景，回调在发布线程执行，须轻量。
- 超过 retention 的事件自动清理，保证长时间运行内存有界。
"""
from __future__ import annotations

import threading
from enum import Enum
from typing import Callable, Iterable

from .schemas import Modality, ModalityEvent


def _kind_value(k: object) -> str:
    # Python 3.10 中 str(StrEnum成员) 是 "VisionKind.X" 而非值本身，统一取 .value
    return k.value if isinstance(k, Enum) else str(k)


class EventBus:
    def __init__(self, retention: float = 60.0) -> None:
        self._retention = retention
        self._events: list[ModalityEvent] = []
        self._lock = threading.Lock()
        self._subscribers: list[Callable[[ModalityEvent], None]] = []

    def publish(self, event: ModalityEvent) -> None:
        with self._lock:
            self._events.append(event)
            self._prune_locked(event.t_end)
        for fn in list(self._subscribers):
            fn(event)

    def subscribe(self, fn: Callable[[ModalityEvent], None]) -> None:
        self._subscribers.append(fn)

    def query(
        self,
        t_from: float,
        t_to: float,
        modality: Modality | None = None,
        kinds: Iterable[str] | None = None,
    ) -> list[ModalityEvent]:
        """返回与 [t_from, t_to] 有时间重叠的事件，按 t_start 排序。

        kinds 仅对 VisionEvent 生效（按 kind 的值过滤）。
        """
        kind_set = {_kind_value(k) for k in kinds} if kinds else None
        with self._lock:
            found = [e for e in self._events if e.overlaps(t_from, t_to)]
        if modality is not None:
            found = [e for e in found if e.modality == modality]
        if kind_set is not None:
            found = [
                e for e in found
                if getattr(e, "kind", None) is not None
                and _kind_value(e.kind) in kind_set
            ]
        return sorted(found, key=lambda e: e.t_start)

    def _prune_locked(self, now: float) -> None:
        horizon = now - self._retention
        if self._events and self._events[0].t_end < horizon:
            self._events = [e for e in self._events if e.t_end >= horizon]

    def size(self) -> int:
        with self._lock:
            return len(self._events)
