"""LLM 统一客户端。

合规红线（赛题）：所选模型参数量必须明确 ≤14B。
默认 provider 见 configs/config.yaml 的 llm.provider：
- dashscope: 阿里云百炼 qwen2.5-14b-instruct（OpenAI 兼容模式）
- ollama:    本地 qwen2.5:7b-instruct（端侧部署可行性演示）
- custom:    任意 OpenAI 兼容服务

chat_json 用于意图理解等结构化输出场景：优先 response_format=json_object，
解析失败时做大括号截取兜底，再失败抛 LLMError 由上层降级处理。
"""
from __future__ import annotations

import json
import time

import httpx
from openai import APITimeoutError, OpenAI

from ..core.config import Config
from ..core.deadline import HardDeadlineExceeded, run_with_deadline
from ..core.log import get_logger

logger = get_logger("llm")


class LLMError(Exception):
    pass


class LLMClient:
    def __init__(self, cfg: Config, provider: str | None = None) -> None:
        self._cfg = cfg
        self.provider = provider or cfg.require("llm.provider")
        pcfg = cfg.get(f"llm.providers.{self.provider}")
        if not pcfg:
            raise LLMError(f"未知的 LLM provider: {self.provider}")
        api_key = pcfg.get("api_key", "")
        if not api_key:
            raise LLMError(
                f"provider '{self.provider}' 的 API Key 为空，"
                f"请设置对应环境变量（见 configs/config.yaml）"
            )
        self.model = pcfg["model"]
        self._extra_body: dict = pcfg.get("extra_body") or {}
        self.max_retries = int(cfg.get("llm.max_retries", 2))
        timeout = float(cfg.get("llm.request_timeout", 30))
        # 国内服务(DashScope/本地Ollama)直连最稳。Windows 上 httpx 会自动读取
        # 注册表里的系统代理(如 VPN 的 127.0.0.1:7890)导致连接失败，默认绕过。
        use_system_proxy = bool(cfg.get("llm.use_system_proxy", False))
        self._client = OpenAI(
            base_url=pcfg["base_url"],
            api_key=api_key,
            timeout=timeout,
            max_retries=0,  # 重试逻辑自管，便于记录与退避
            http_client=httpx.Client(trust_env=use_system_proxy, timeout=timeout),
        )
        self._timeout = timeout
        self._hard_deadline = float(cfg.get("llm.hard_deadline", 0) or 0)
        # P2-2 兜底 provider：主 provider 超时/失败后切它（如本地 ollama，端侧无网/尾延时也能应答）。
        # 防递归：兜底 client 自身不再设兜底（fb == 当前 provider 时关闭）。
        fb = cfg.get("llm.fallback_provider") or None
        self._fallback_name = None if fb == self.provider else fb
        self._fallback: "LLMClient | None" = None

    def _ensure_fallback(self) -> "LLMClient | None":
        """lazy 构造兜底 provider 客户端（首次需要时）。构造失败则永久关闭兜底。"""
        if not self._fallback_name:
            return None
        if self._fallback is None:
            try:
                self._fallback = LLMClient(self._cfg, provider=self._fallback_name)
                logger.info("兜底 provider 已就绪: %s", self._fallback_name)
            except Exception as e:  # noqa: BLE001 — 兜底不可用不应让主流程崩
                logger.warning("兜底 provider[%s] 不可用，关闭兜底: %s", self._fallback_name, e)
                self._fallback_name = None
        return self._fallback

    def chat(
        self,
        messages: list[dict],
        *,
        temperature: float = 0.3,
        max_tokens: int = 1024,
        json_mode: bool = False,
    ) -> str:
        kwargs: dict = dict(
            model=self.model,
            messages=messages,
            temperature=temperature,
            max_tokens=max_tokens,
        )
        if self._extra_body:
            kwargs["extra_body"] = self._extra_body
        if json_mode:
            kwargs["response_format"] = {"type": "json_object"}

        last_err: Exception | None = None
        for attempt in range(self.max_retries + 1):
            try:
                t0 = time.perf_counter()
                if self._hard_deadline > 0:
                    resp = run_with_deadline(
                        lambda: self._client.chat.completions.create(**kwargs),
                        self._hard_deadline,
                        label="llm",
                    )
                else:
                    resp = self._client.chat.completions.create(**kwargs)
                latency = time.perf_counter() - t0
                content = resp.choices[0].message.content or ""
                logger.debug("LLM[%s] %.2fs %d字", self.model, latency, len(content))
                return content
            except Exception as e:  # noqa: BLE001 —— 网络/服务端错误统一重试
                last_err = e
                # 部分服务不支持 response_format，降级为纯 prompt 约束
                if json_mode and "response_format" in str(e):
                    kwargs.pop("response_format", None)
                    logger.warning("provider 不支持 json_mode，降级为 prompt 约束")
                    continue
                # P2-2：超时类不再重试（重试只会叠加等待、拖垮 <5s）→ 直接走兜底/降级
                if isinstance(e, (APITimeoutError, HardDeadlineExceeded)):
                    logger.warning("LLM 超时(%.0fs)，跳过重试，走兜底/降级", self._timeout)
                    break
                if attempt < self.max_retries:
                    wait = 1.5 * (attempt + 1)
                    logger.warning("LLM 调用失败(%s)，%.1fs 后重试: %s", type(e).__name__, wait, e)
                    time.sleep(wait)
        # P2-2：主 provider 失败/超时 → 若配置了兜底 provider，切过去再试一次（如本地 ollama）
        fb = self._ensure_fallback()
        if fb is not None:
            logger.warning("主 provider[%s] 不可用，切兜底[%s]", self.provider, self._fallback_name)
            return fb.chat(messages, temperature=temperature, max_tokens=max_tokens, json_mode=json_mode)
        raise LLMError(f"LLM 调用失败（已重试 {self.max_retries} 次）: {last_err}") from last_err

    def chat_json(self, messages: list[dict], **kwargs) -> dict:
        """要求严格 JSON 输出；上层 Prompt 中也应明确要求 JSON。"""
        text = self.chat(messages, json_mode=True, **kwargs)
        try:
            return json.loads(text)
        except json.JSONDecodeError:
            # 兜底：截取首个 { 到最后一个 } 之间的内容
            start, end = text.find("{"), text.rfind("}")
            if start != -1 and end > start:
                try:
                    return json.loads(text[start : end + 1])
                except json.JSONDecodeError:
                    pass
        raise LLMError(f"LLM 返回内容无法解析为 JSON: {text[:200]}")
