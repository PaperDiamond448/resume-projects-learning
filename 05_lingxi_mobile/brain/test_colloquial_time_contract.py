from __future__ import annotations

import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from lingxi.core.config import Config  # noqa: E402
from lingxi.core.schemas import MultimodalFrame, TextInput, VisionEvent, VisionKind  # noqa: E402
from lingxi.intent.context import DialogueContext  # noqa: E402
from lingxi.intent.engine import IntentEngine, _mobile_current_time_request  # noqa: E402
from lingxi.llm import LLMError  # noqa: E402


TIME_POSITIVES = (
    "我是问你几点啦",
    "我问你几点钟啦",
    "我问的是现在几点",
    "我是想问现在到底几点钟了",
    "你能告诉我现在几点吗",
    "你告诉我现在几点了呗",
    "告诉我一下当前几点",
    "你跟我说下现在几点呀",
    "麻烦你告诉我当前几点了",
    "请问，这会儿几点啦？",
    "几点啦",
    "现在是什么时间",
    "当前时间是多少",
    "给我报个时",
)

TIME_HARD_NEGATIVES = (
    "老狼老狼几点啦",
    "今天几点开会",
    "会议几点钟开始",
    "电影几点开始",
    "明天几点提醒我出门",
    "几点给妈妈打电话",
    "几点播放音乐",
    "你几点睡觉",
    "我问你电影几点开始",
    "老狼老狼几点了这个游戏怎么玩",
    "伦敦现在几点",
    "杭州现在几点下雨",
    "还有几点到杭州",
)


class _RecordingLlm:
    def __init__(self) -> None:
        self.json_calls = 0

    def chat_json(self, *_args, **_kwargs) -> dict:
        self.json_calls += 1
        return {
            "intent": "chitchat",
            "slots": {},
            "confidence": 0.9,
            "conflict": False,
            "conflict_resolution": "",
            "need_clarification": False,
            "clarify_question": "",
            "domain": "chitchat",
            "reasoning": "delegate fixture",
            "direct_reply": "已交给原意图链。",
        }


class _InvalidSchemaLlm:
    def chat_json(self, *_args, **_kwargs) -> dict:
        return {"intent": "invented_intent", "slots": {}}


class _ErrorLlm:
    def __init__(self, message: str) -> None:
        self.message = message

    def chat_json(self, *_args, **_kwargs) -> dict:
        raise LLMError(self.message)


class _ModelClarifyLlm:
    def chat_json(self, *_args, **_kwargs) -> dict:
        return {
            "intent": "unclear",
            "slots": {},
            "confidence": 0.85,
            "conflict": False,
            "conflict_resolution": "",
            "need_clarification": True,
            "clarify_question": "你想问哪个时间？",
            "domain": "time",
            "reasoning": "模型认为时间对象不明确",
            "direct_reply": "",
        }


def _cfg(*, mobile: bool = True) -> Config:
    return Config({
        "intent": {
            "confidence_threshold": 0.6,
            "temperature": 0.1,
            "high_risk_actions": ["send_message", "make_call", "send_email"],
            "device_capabilities": ["navigation"] if mobile else [],
            "use_domain_router": False,
            "repair_invalid_json": False,
        },
        "response": {"temperature": 0.7},
    })


def _frame(text: str, *, visual: bool = False) -> MultimodalFrame:
    events = []
    if visual:
        events.append(VisionEvent(
            t_start=0.0,
            t_end=0.5,
            confidence=0.95,
            kind=VisionKind.HEAD_MOTION,
            label="nod",
        ))
    return MultimodalFrame(
        anchor=TextInput(t_start=0.0, t_end=0.0, text=text),
        vision_events=events,
        vision_summary=[],
    )


class ColloquialCurrentTimeTests(unittest.TestCase):
    def test_bounded_colloquial_variants_use_zero_llm_calls(self) -> None:
        for text in TIME_POSITIVES:
            with self.subTest(text=text):
                self.assertTrue(_mobile_current_time_request(text))
                llm = _RecordingLlm()
                result = IntentEngine(_cfg(), llm=llm).understand(
                    _frame(text), DialogueContext()
                )
                self.assertEqual("query_time", result.intent)
                self.assertEqual({}, result.slots)
                self.assertEqual(0, llm.json_calls)

    def test_explicit_restatement_recovers_after_game_ambiguity(self) -> None:
        context = DialogueContext(history=[
            {"role": "user", "text": "老狼老狼几点啦"},
            {"role": "assistant", "text": "这是在玩小游戏吗？"},
        ])
        for text in ("我是问你几点啦", "我问你几点钟啦"):
            with self.subTest(text=text):
                llm = _RecordingLlm()
                result = IntentEngine(_cfg(), llm=llm).understand(_frame(text), context)
                self.assertEqual("query_time", result.intent)
                self.assertEqual(0, llm.json_calls)

    def test_event_schedule_reminder_game_and_world_time_phrases_delegate(self) -> None:
        for text in TIME_HARD_NEGATIVES:
            with self.subTest(text=text):
                self.assertFalse(_mobile_current_time_request(text))
                llm = _RecordingLlm()
                result = IntentEngine(_cfg(), llm=llm).understand(
                    _frame(text), DialogueContext()
                )
                self.assertEqual("chitchat", result.intent)
                self.assertEqual(1, llm.json_calls)

    def test_pending_visual_and_pc_profiles_never_use_time_fast_path(self) -> None:
        cases = (
            (_cfg(), _frame("我问你几点钟啦"), DialogueContext(
                pending_action={"type": "send_message"}
            )),
            (_cfg(), _frame("我问你几点钟啦", visual=True), DialogueContext()),
            (_cfg(mobile=False), _frame("我问你几点钟啦"), DialogueContext()),
        )
        for cfg, frame, context in cases:
            with self.subTest(text=frame.anchor_text, mobile=bool(cfg.get("intent.device_capabilities"))):
                llm = _RecordingLlm()
                IntentEngine(cfg, llm=llm).understand(frame, context)
                self.assertEqual(1, llm.json_calls)


class IntentFallbackObservabilityTests(unittest.TestCase):
    def test_invalid_schema_has_stable_machine_reason_and_log_code(self) -> None:
        with self.assertLogs("lingxi.intent.engine", level="ERROR") as captured:
            result = IntentEngine(_cfg(), llm=_InvalidSchemaLlm()).understand(
                _frame("帮我做一件事"), DialogueContext()
            )
        self.assertEqual("LLM_SCHEMA_INVALID", result.reasoning)
        self.assertTrue(any("code=LLM_SCHEMA_INVALID" in line for line in captured.output))

    def test_llm_failures_have_distinct_machine_reasons(self) -> None:
        cases = {
            "Hard deadline exceeded for llm after 4.5s": "LLM_TIMEOUT",
            "LLM 返回内容无法解析为 JSON: broken": "LLM_JSON_PARSE_FAILED",
            "provider unavailable": "LLM_CALL_FAILED",
        }
        for message, expected in cases.items():
            with self.subTest(expected=expected):
                with self.assertLogs("lingxi.intent.engine", level="ERROR") as captured:
                    result = IntentEngine(_cfg(), llm=_ErrorLlm(message)).understand(
                        _frame("帮我做一件事"), DialogueContext()
                    )
                self.assertEqual(expected, result.reasoning)
                self.assertTrue(any(f"code={expected}" in line for line in captured.output))

    def test_model_requested_clarification_is_logged_separately(self) -> None:
        with self.assertLogs("lingxi.intent.engine", level="INFO") as captured:
            result = IntentEngine(_cfg(), llm=_ModelClarifyLlm()).understand(
                _frame("帮我做一件事"), DialogueContext()
            )
        self.assertTrue(result.need_clarification)
        self.assertTrue(any("code=MODEL_CLARIFY" in line for line in captured.output))


if __name__ == "__main__":
    unittest.main()
