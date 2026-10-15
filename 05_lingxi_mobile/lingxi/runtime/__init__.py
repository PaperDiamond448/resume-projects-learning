"""运行时层：会话编排（live/batch 共用），运行时入口见 apps/。"""

from .session import InteractionSession, TurnResult

__all__ = ["InteractionSession", "TurnResult"]
