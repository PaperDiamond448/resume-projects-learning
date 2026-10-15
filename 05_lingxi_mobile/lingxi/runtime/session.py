"""交互会话编排器 —— 单轮处理的唯一入口（live/batch 运行时共用）。

一轮流程:
  MultimodalFrame ──IntentEngine──> IntentResult
                  ──ActionExecutor──> ActionOutcome(回执+卡片+待确认动作)
                  ──ResponseGenerator──> 个性化应答文本 (──TTS──> 语音)
                  ──ProfileUpdater──> 画像增量学习（本地）
                  ──DialogueContext──> 历史/待确认动作滚动更新
"""
from __future__ import annotations

import threading
import time
from dataclasses import dataclass, field

from ..core.config import Config
from ..core.log import get_logger
from ..core.schemas import MultimodalFrame
from ..intent import DialogueContext, IntentEngine
from ..intent.schema import IntentResult
from ..llm import LLMClient
from ..persona import ProfileStore, ProfileUpdater, apply_preference
from ..response import ResponseGenerator
from ..response.actions import ActionExecutor, ActionOutcome, MockTerminal, make_terminal
from ..response.tts import TTSEngine

logger = get_logger("runtime.session")


@dataclass
class TurnResult:
    user_text: str
    intent: IntentResult
    outcome: ActionOutcome
    reply: str
    card: dict = field(default_factory=dict)
    latency: dict = field(default_factory=dict)  # 分解耗时(秒)

    def to_dict(self) -> dict:
        return {
            "user_text": self.user_text,
            "intent": self.intent.to_dict(),
            "status": self.outcome.status,
            "reply": self.reply,
            "card": self.card,
            "latency": self.latency,
        }


class InteractionSession:
    def __init__(self, cfg: Config, user_id: str | None = None,
                 speak: bool = False, screen: dict | None = None,
                 terminal_mode: str | None = None) -> None:
        llm = LLMClient(cfg)  # 意图与应答共用一个客户端连接
        self.engine = IntentEngine(cfg, llm=llm)
        self.generator = ResponseGenerator(cfg, llm=llm)
        self.executor = ActionExecutor(make_terminal(cfg, terminal_mode))
        self.store = ProfileStore(cfg)
        self.updater = ProfileUpdater()
        self.profile = self.store.load(user_id or cfg.get("persona.default_user", "default"))
        self.context = DialogueContext(screen=screen or {})
        self.context.profile_summary = self.profile.to_prompt_text()
        self.tts = TTSEngine(cfg) if speak else None
        self._tts_lock = threading.Lock()  # 串行播放，但不阻塞轮次处理
        self._speak_seq = 0                # 积压时只播最新一条应答
        # 待确认动作生命周期：用户岔开话题聊几轮后，悬而未决的确认应自动作废
        self.max_pending_turns = int(cfg.get("intent.max_pending_turns", 2))
        self._pending_age = 0
        # 回声防护：记录最近播报文本与结束时刻，仅拦截"内容高度重合"的真回声
        self._spoken_text = ""
        self._spoken_until = 0.0
        self.echo_window_s = float(cfg.get("response.tts.echo_window_s", 1.5))
        self.echo_sim_threshold = float(cfg.get("response.tts.echo_sim_threshold", 0.6))

    @property
    def terminal(self) -> MockTerminal:
        return self.executor.terminal

    @property
    def tts_busy(self) -> bool:
        """TTS 合成/播放中。"""
        return self.tts is not None and self._tts_lock.locked()

    def is_likely_echo(self, asr_text: str) -> bool:
        """判断 asr_text 是否是刚播报语音的回声（扬声器声音被麦克风录回）。

        仅当"播报中或刚结束 echo_window_s 内" 且 "内容与播报文本高度相似" 时才判回声——
        避免把用户在播报期间/刚结束时说的真实话语（如对"您想做什么"答"没什么"）误丢。
        """
        if self.tts is None or not asr_text:
            return False
        busy = self._tts_lock.locked()
        recent = (time.monotonic() - self._spoken_until) < self.echo_window_s
        if not (busy or recent):
            return False
        from difflib import SequenceMatcher

        sim = SequenceMatcher(None, asr_text, self._spoken_text).ratio()
        return sim >= self.echo_sim_threshold

    def _handle_persona(self, intent: IntentResult) -> ActionOutcome:
        """set_preference/query_profile → 直接读写本地画像（隐私全端侧），即时落盘。"""
        if intent.intent == "set_preference":
            changed = apply_preference(self.profile, intent.slots)
            self.store.save(self.profile)
            self.context.profile_summary = self.profile.to_prompt_text()
            return ActionOutcome(
                status="executed",
                feedback=f"已更新个性化设置：{changed}" if changed else "好的，记下了",
            )
        # query_profile
        op = str(intent.slots.get("op", "")).strip()
        if any(k in op for k in ("清", "忘", "删", "重置", "clear", "reset")):
            self.profile.clear()
            self.store.save(self.profile)
            self.context.profile_summary = self.profile.to_prompt_text()
            return ActionOutcome(status="executed", feedback="已清除我对你记住的所有个性化偏好")
        return ActionOutcome(
            status="executed",
            feedback=f"向用户复述我记住的个性化偏好：{self.profile.to_prompt_text()}",
        )

    def process(self, frame: MultimodalFrame) -> TurnResult:
        t_all = time.perf_counter()
        # 待确认动作老化：超过 max_pending_turns 轮仍未响应则过期作废，
        # 避免后续闲聊里偶然的"嗯/好"被误当成对它的确认/取消
        prev_pending = self.context.pending_action
        if prev_pending is not None:
            self._pending_age += 1
            if self._pending_age > self.max_pending_turns:
                logger.info("待确认动作超时作废(%d轮未响应): %s",
                            self._pending_age, prev_pending.get("summary"))
                self.context.pending_action = None
                self._pending_age = 0
        pending_before = self.context.pending_action

        t0 = time.perf_counter()
        intent = self.engine.understand(frame, self.context)
        t_intent = time.perf_counter() - t0

        # 个性化记忆意图（set_preference/query_profile）直接作用于本地画像，不走终端执行器
        is_persona = intent.intent in ("set_preference", "query_profile") and not intent.need_clarification
        outcome = self._handle_persona(intent) if is_persona \
            else self.executor.execute(intent, pending_before)

        t0 = time.perf_counter()
        reply = self.generator.generate(intent, self.profile, self.context,
                                        action_feedback=outcome.feedback,
                                        user_text=frame.anchor_text)
        t_reply = time.perf_counter() - t0

        # 画像学习（执行成功才学；confirm 计入被确认动作）。个性化意图已在 _handle_persona 里
        # 直接落盘，不再走统计式 updater（避免把 set_preference 自己也统计成"习惯"）。
        if outcome.status == "executed" and not is_persona:
            self.updater.update(self.profile, intent, pending_action=pending_before)
            self.store.save(self.profile)
            self.context.profile_summary = self.profile.to_prompt_text()

        # 上下文滚动
        self.context.pending_action = outcome.pending_action
        if outcome.pending_action is not None and outcome.pending_action is not prev_pending:
            self._pending_age = 0  # 本轮新建的待确认动作，重新计龄
        self.context.add_turn("user", frame.anchor_text)
        self.context.add_turn("assistant", reply)

        if self.tts is not None:
            # 后台线程播放：实测同步播放(开代理时edge-tts可达20s)会堵死后续轮次
            self._speak_seq += 1
            seq = self._speak_seq

            def _speak_bg(text: str) -> None:
                with self._tts_lock:
                    if seq != self._speak_seq:
                        return  # 已有更新的应答排队，过期的不播
                    self._spoken_text = text
                    self.tts.speak(text)              # 阻塞至播放结束
                    self._spoken_until = time.monotonic()

            threading.Thread(target=_speak_bg, args=(reply,), daemon=True).start()

        result = TurnResult(
            user_text=frame.anchor_text, intent=intent, outcome=outcome,
            reply=reply, card=outcome.card,
            latency={"intent": round(t_intent, 2), "reply": round(t_reply, 2),
                     "total": round(time.perf_counter() - t_all, 2)},
        )
        logger.info("轮次完成 [%s] %s → %s (%.2fs)", outcome.status,
                    frame.anchor_text, reply, result.latency["total"])
        return result
