"""LLM 统一适配层：所有 provider 走 OpenAI 兼容协议。"""

from .client import LLMClient, LLMError

__all__ = ["LLMClient", "LLMError"]
