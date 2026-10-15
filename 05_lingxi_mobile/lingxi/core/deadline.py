"""Small synchronous hard-deadline guard for cloud-only outbound calls.

The OpenAI/httpx timeout is a per-network-phase timeout, not a total wall-clock
deadline.  A provider that slowly produces bytes can therefore exceed the
product response budget.  This helper returns control to the caller at a hard
deadline while allowing the underlying blocking SDK call to finish only in a
bounded daemon worker.  Saturation fails closed instead of creating unbounded
threads.
"""
from __future__ import annotations

import queue
import threading
from collections.abc import Callable
from typing import TypeVar


T = TypeVar("T")
_DEADLINE_SLOTS = threading.BoundedSemaphore(value=4)


class HardDeadlineExceeded(TimeoutError):
    """The caller's total wall-clock budget was exhausted or saturated."""


def run_with_deadline(call: Callable[[], T], timeout: float, *, label: str) -> T:
    """Run a blocking call with a bounded wall-clock wait.

    A timed-out worker is deliberately not force-killed because CPython cannot
    safely terminate a thread.  It is daemonized and still occupies one of four
    global slots until it exits; new calls fail immediately when all slots are
    occupied.  This keeps the public request path bounded and prevents a slow
    provider from creating unlimited background work.
    """
    budget = float(timeout)
    if budget <= 0:
        return call()
    if not _DEADLINE_SLOTS.acquire(blocking=False):
        raise HardDeadlineExceeded(f"{label} hard-deadline workers are saturated")

    outcome: queue.Queue[tuple[bool, object]] = queue.Queue(maxsize=1)

    def worker() -> None:
        try:
            value: object = call()
            item = (True, value)
        except BaseException as exc:  # propagate the original SDK exception
            item = (False, exc)
        try:
            outcome.put_nowait(item)
        except queue.Full:
            pass
        finally:
            _DEADLINE_SLOTS.release()

    threading.Thread(
        target=worker,
        name=f"lingxi-deadline-{label}",
        daemon=True,
    ).start()
    try:
        ok, value = outcome.get(timeout=budget)
    except queue.Empty as exc:
        raise HardDeadlineExceeded(f"{label} exceeded {budget:.2f}s hard deadline") from exc
    if ok:
        return value  # type: ignore[return-value]
    raise value  # type: ignore[misc]
