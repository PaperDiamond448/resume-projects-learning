from __future__ import annotations

import os
import sys
import time
import unittest
from datetime import datetime, timezone
from pathlib import Path
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from lingxi.core.config import Config  # noqa: E402
from lingxi.core.deadline import HardDeadlineExceeded, run_with_deadline  # noqa: E402
from lingxi.core.schemas import MultimodalFrame, TextInput, VisionEvent, VisionKind  # noqa: E402
from lingxi.intent.context import DialogueContext  # noqa: E402
from lingxi.intent.engine import IntentEngine  # noqa: E402
from lingxi.intent.schema import IntentResult, validate_llm_json  # noqa: E402
from lingxi.llm import LLMError  # noqa: E402
from lingxi.persona.profile import UserProfile, apply_preference  # noqa: E402
from lingxi.response.actions import ActionExecutor, MockTerminal, RealTerminal  # noqa: E402
from lingxi.response.generator import ResponseGenerator  # noqa: E402


class RecordingLLM:
    def __init__(self, intent: str = "unclear") -> None:
        self.intent = intent
        self.json_calls = 0
        self.chat_calls = 0
        self.chat_messages = []
        self.chat_kwargs = []

    def chat_json(self, *_args, **_kwargs) -> dict:
        self.json_calls += 1
        return {
            "intent": self.intent,
            "slots": {},
            "confidence": 0.95,
            "conflict": False,
            "conflict_resolution": "",
            "need_clarification": self.intent == "unclear",
            "clarify_question": "请再说具体一点。" if self.intent == "unclear" else "",
            "domain": "other",
            "reasoning": "fixture",
            "direct_reply": "",
        }

    def chat(self, *args, **kwargs) -> str:
        self.chat_calls += 1
        self.chat_messages.append(args[0] if args else kwargs.get("messages", []))
        self.chat_kwargs.append(kwargs)
        return "兼容闲聊回复"


class ChatRouter:
    def predict(self, _text: str) -> dict:
        return {"domain": "chat", "is_oos": False, "p_oos": 0.2, "top_p": 0.5}


def _cfg(
    *, mobile: bool = True, repair_invalid_json: bool = True,
    allow_chitchat_reply_fallback: bool = True,
) -> Config:
    return Config({
        "intent": {
            "confidence_threshold": 0.6,
            "temperature": 0.1,
            "high_risk_actions": ["send_message", "make_call", "send_email"],
            "device_capabilities": ["navigation"] if mobile else [],
            "use_domain_router": False,
            "repair_invalid_json": repair_invalid_json,
        },
        "response": {
            "temperature": 0.7,
            "allow_chitchat_reply_fallback": allow_chitchat_reply_fallback,
        },
    })


def _frame(text: str, *, with_visual: bool = False) -> MultimodalFrame:
    vision = []
    if with_visual:
        vision = [VisionEvent(
            t_start=0.0,
            t_end=0.5,
            confidence=0.95,
            kind=VisionKind.HEAD_MOTION,
            label="nod",
        )]
    return MultimodalFrame(
        anchor=TextInput(t_start=0.0, t_end=0.0, text=text),
        vision_events=vision,
        # Intent fast-path只需看到原始事件便应退出；聚合摘要留空，避免fixture
        # 伪造不完整的kind/relation字段后误测到证据渲染器。
        vision_summary=[],
    )


class MobileDeterministicIntentTests(unittest.TestCase):
    def test_explicit_current_weather_uses_zero_llm_calls(self) -> None:
        llm = RecordingLLM()
        engine = IntentEngine(_cfg(), llm=llm)
        result = engine.understand(_frame("查询杭州天气"), DialogueContext())
        self.assertEqual("query_weather", result.intent)
        self.assertEqual({"location": "杭州", "date": "今天"}, result.slots)
        self.assertEqual(0, llm.json_calls)

    def test_time_query_uses_zero_llm_calls(self) -> None:
        llm = RecordingLLM()
        result = IntentEngine(_cfg(), llm=llm).understand(
            _frame("请问现在几点了？"), DialogueContext()
        )
        self.assertEqual("query_time", result.intent)
        self.assertEqual(0, llm.json_calls)

    def test_explicit_social_phrases_use_zero_llm_calls(self) -> None:
        cases = {
            "今天有点累，陪我聊两句": "辛苦了",
            "你好": "我在呢",
        }
        for text, reply_marker in cases.items():
            with self.subTest(text=text):
                llm = RecordingLLM()
                result = IntentEngine(_cfg(), llm=llm).understand(
                    _frame(text), DialogueContext()
                )
                self.assertEqual("chitchat", result.intent)
                self.assertIn(reply_marker, result.direct_reply)
                self.assertEqual(0, llm.json_calls)

    def test_safe_general_chat_uses_one_compact_text_call(self) -> None:
        llm = RecordingLLM()
        engine = IntentEngine(_cfg(), llm=llm)
        engine.router = ChatRouter()
        result = engine.understand(
            _frame("给我讲一个不超过两句话的冷笑话"), DialogueContext()
        )
        self.assertEqual("chitchat", result.intent)
        self.assertEqual("兼容闲聊回复", result.direct_reply)
        self.assertEqual(0, llm.json_calls)
        self.assertEqual(1, llm.chat_calls)

    def test_general_questions_do_not_require_fixed_chat_cue_words(self) -> None:
        for text in ("太阳系有几颗行星", "你喜欢小明还是小红"):
            with self.subTest(text=text):
                llm = RecordingLLM()
                engine = IntentEngine(_cfg(), llm=llm)
                engine.router = ChatRouter()
                result = engine.understand(_frame(text), DialogueContext())
                self.assertEqual("chitchat", result.intent)
                self.assertEqual("兼容闲聊回复", result.direct_reply)
                self.assertEqual(0, llm.json_calls)
                self.assertEqual(1, llm.chat_calls)

    def test_general_questions_receive_current_reply_length_profile(self) -> None:
        cases = [
            ("简短", "不超过20个汉字", 48),
            ("适中", "篇幅适中", 80),
            ("详细", "45至70个汉字", 112),
        ]
        for length, marker, expected_tokens in cases:
            with self.subTest(length=length):
                llm = RecordingLLM()
                engine = IntentEngine(_cfg(), llm=llm)
                engine.router = ChatRouter()
                context = DialogueContext(
                    profile_summary=f"[用户画像] 应答风格:{length}/自然/不用emoji"
                )
                result = engine.understand(_frame("天空为什么是蓝色的"), context)
                self.assertEqual("chitchat", result.intent)
                system = llm.chat_messages[0][0]["content"]
                self.assertIn(marker, system)
                self.assertIn(f"应答风格:{length}/自然/不用emoji", system)
                self.assertEqual(expected_tokens, llm.chat_kwargs[0]["max_tokens"])
                self.assertEqual(0.35, llm.chat_kwargs[0]["temperature"])

    def test_spoken_length_preference_is_persistable_before_next_question(self) -> None:
        cases = [
            ("以后回答简短一点", "简短", "不超过20个汉字", 48),
            ("回答详细一些", "详细", "45至70个汉字", 112),
            ("今后回复正常一点", "适中", "篇幅适中", 80),
        ]
        for command, expected_length, marker, expected_tokens in cases:
            with self.subTest(command=command):
                profile = UserProfile(user_id="test")
                llm = RecordingLLM()
                engine = IntentEngine(_cfg(), llm=llm)
                engine.router = ChatRouter()

                preference = engine.understand(_frame(command), DialogueContext())
                self.assertEqual("set_preference", preference.intent)
                self.assertEqual(expected_length, preference.slots["reply_length"])
                self.assertEqual(0, llm.chat_calls)
                self.assertEqual(0, llm.json_calls)

                changed = apply_preference(profile, preference.slots)
                self.assertIn(expected_length, changed)
                context = DialogueContext(profile_summary=profile.to_prompt_text())
                result = engine.understand(_frame("天空为什么是蓝色的"), context)
                self.assertEqual("chitchat", result.intent)
                system = llm.chat_messages[-1][0]["content"]
                self.assertIn(marker, system)
                self.assertEqual(expected_tokens, llm.chat_kwargs[-1]["max_tokens"])

    def test_one_turn_length_request_is_not_silently_persisted(self) -> None:
        llm = RecordingLLM()
        engine = IntentEngine(_cfg(), llm=llm)
        engine.router = ChatRouter()
        result = engine.understand(
            _frame("请简短回答太阳系有几颗行星"), DialogueContext()
        )
        self.assertEqual("chitchat", result.intent)
        self.assertNotEqual("set_preference", result.intent)

    def test_combined_length_and_nickname_preference_persists_both(self) -> None:
        profile = UserProfile(user_id="test")
        llm = RecordingLLM()
        engine = IntentEngine(_cfg(), llm=llm)
        engine.router = ChatRouter()
        command = "以后回答详细一些，并且回复我的时候必须叫大哥"

        preference = engine.understand(_frame(command), DialogueContext())
        self.assertEqual("set_preference", preference.intent)
        self.assertEqual(
            {"reply_length": "详细", "nickname": "大哥"}, preference.slots
        )
        self.assertEqual(0, llm.chat_calls)
        self.assertEqual(0, llm.json_calls)

        changed = apply_preference(profile, preference.slots)
        self.assertIn("回答详细", changed)
        self.assertIn("以后叫你「大哥」", changed)
        context = DialogueContext(profile_summary=profile.to_prompt_text())
        result = engine.understand(_frame("天空为什么是蓝色的"), context)
        self.assertEqual("chitchat", result.intent)
        system = llm.chat_messages[-1][0]["content"]
        self.assertIn("称呼用户为「大哥」", system)
        self.assertIn("每次回答开头必须先用该称呼", system)
        self.assertIn("45至70个汉字", system)

    def test_unhandled_persona_language_is_not_swallowed_as_chat(self) -> None:
        llm = RecordingLLM(intent="set_preference")
        engine = IntentEngine(_cfg(), llm=llm)
        engine.router = ChatRouter()
        result = engine.understand(
            _frame("以后回复时语气温柔一点"), DialogueContext()
        )
        self.assertEqual("set_preference", result.intent)
        self.assertEqual(1, llm.json_calls)
        self.assertEqual(0, llm.chat_calls)

    def test_compact_chat_timeout_reports_timeout_instead_of_misunderstanding(self) -> None:
        class TimeoutLLM(RecordingLLM):
            def chat(self, *_args, **_kwargs) -> str:
                raise LLMError("Hard deadline exceeded for llm after 4.5s")

        engine = IntentEngine(_cfg(), llm=TimeoutLLM())
        engine.router = ChatRouter()
        result = engine.understand(
            _frame("天空为什么是蓝色的"),
            DialogueContext(profile_summary="[用户画像] 应答风格:详细/自然/不用emoji"),
        )
        self.assertEqual("unclear", result.intent)
        self.assertTrue(result.need_clarification)
        self.assertIn("生成超时", result.clarify_question)
        self.assertIn("LLM_TIMEOUT", result.reasoning)

    def test_social_fast_path_does_not_steal_task_like_text(self) -> None:
        llm = RecordingLLM(intent="send_message")
        engine = IntentEngine(_cfg(), llm=llm)
        # 即使域路由错误声称是 chat，“发消息”也必须被确定性 task guard 拦住。
        engine.router = ChatRouter()
        result = engine.understand(
            _frame("给张三发消息说我好累，陪我聊聊"), DialogueContext()
        )
        self.assertEqual("send_message", result.intent)
        self.assertEqual(1, llm.json_calls)
        self.assertEqual(0, llm.chat_calls)

    def test_future_unknown_multi_city_visual_and_pending_fall_back(self) -> None:
        cases = [
            (_frame("明天杭州天气"), DialogueContext()),
            (_frame("查询保定天气"), DialogueContext()),
            (_frame("北京和上海天气"), DialogueContext()),
            (_frame("查询杭州天气", with_visual=True), DialogueContext()),
            (_frame("查询杭州天气"), DialogueContext(pending_action={"type": "send_message"})),
        ]
        for frame, context in cases:
            with self.subTest(text=frame.anchor_text):
                llm = RecordingLLM()
                IntentEngine(_cfg(), llm=llm).understand(frame, context)
                self.assertEqual(1, llm.json_calls)

    def test_pc_profile_does_not_activate_mobile_fast_path(self) -> None:
        llm = RecordingLLM()
        IntentEngine(_cfg(mobile=False), llm=llm).understand(
            _frame("查询杭州天气"), DialogueContext()
        )
        self.assertEqual(1, llm.json_calls)


class TimezoneContractTests(unittest.TestCase):
    def test_query_time_converts_utc_to_explicit_shanghai_time(self) -> None:
        executor = ActionExecutor(MockTerminal())
        frozen_utc = datetime(2026, 8, 22, 9, 3, tzinfo=timezone.utc)
        with patch.dict(os.environ, {"LINGXI_TIMEZONE": "Asia/Shanghai"}), patch(
            "lingxi.response.actions._utc_now", return_value=frozen_utc
        ):
            outcome = executor.execute(IntentResult(intent="query_time"))

        self.assertEqual("executed", outcome.status)
        self.assertEqual("现在是 17:03（2026年08月22日）", outcome.feedback)
        self.assertEqual(
            {"时间": "17:03", "日期": "2026年08月22日"},
            {item["label"]: item["value"] for item in outcome.card["fields"]},
        )
        self.assertIsNone(outcome.pending_action)


class SingleCallResponseTests(unittest.TestCase):
    def setUp(self) -> None:
        self.profile = UserProfile(user_id="test")
        self.context = DialogueContext()

    def test_task_feedback_never_calls_reply_llm(self) -> None:
        llm = RecordingLLM()
        generator = ResponseGenerator(_cfg(), llm=llm)
        reply = generator.generate(
            IntentResult(intent="query_weather", confidence=0.99),
            self.profile,
            self.context,
            action_feedback="天气查询结果：杭州晴，28℃",
        )
        self.assertEqual("天气查询结果：杭州晴，28℃", reply)
        self.assertEqual(0, llm.chat_calls)

    def test_clarification_uses_validated_question_without_reply_llm(self) -> None:
        llm = RecordingLLM()
        generator = ResponseGenerator(_cfg(), llm=llm)
        reply = generator.generate(
            IntentResult(
                intent="unclear",
                confidence=0.1,
                need_clarification=True,
                clarify_question="请告诉我具体城市。",
            ),
            self.profile,
            self.context,
            action_feedback="需要向用户澄清",
        )
        self.assertEqual("请告诉我具体城市。", reply)
        self.assertEqual(0, llm.chat_calls)

    def test_chitchat_reuses_same_call_direct_reply(self) -> None:
        llm = RecordingLLM()
        generator = ResponseGenerator(_cfg(), llm=llm)
        reply = generator.generate(
            IntentResult(intent="chitchat", confidence=0.95, direct_reply="今天也要照顾好自己。"),
            self.profile,
            self.context,
            user_text="今天有点累",
        )
        self.assertEqual("今天也要照顾好自己。", reply)
        self.assertEqual(0, llm.chat_calls)

    def test_old_chitchat_without_direct_reply_keeps_compatibility_fallback(self) -> None:
        llm = RecordingLLM()
        generator = ResponseGenerator(_cfg(), llm=llm)
        reply = generator.generate(
            IntentResult(intent="chitchat", confidence=0.95),
            self.profile,
            self.context,
            user_text="你好",
        )
        self.assertEqual("兼容闲聊回复", reply)
        self.assertEqual(1, llm.chat_calls)

    def test_cloud_profile_disables_second_chitchat_call(self) -> None:
        llm = RecordingLLM()
        generator = ResponseGenerator(
            _cfg(allow_chitchat_reply_fallback=False), llm=llm
        )
        reply = generator.generate(
            IntentResult(intent="chitchat", confidence=0.95),
            self.profile,
            self.context,
            user_text="你好",
        )
        self.assertTrue(reply)
        self.assertEqual(0, llm.chat_calls)

    def test_schema_discards_task_direct_reply_and_bounds_chat_reply(self) -> None:
        base = {
            "slots": {}, "confidence": 0.9, "conflict": False,
            "conflict_resolution": "", "need_clarification": False,
            "clarify_question": "", "domain": "time", "reasoning": "fixture",
        }
        task, errors = validate_llm_json({
            **base, "intent": "query_time", "direct_reply": "伪造的任务结果",
        })
        self.assertFalse(errors)
        self.assertEqual("", task.direct_reply)
        chat, errors = validate_llm_json({
            **base, "intent": "chitchat", "domain": "chitchat", "direct_reply": "好" * 200,
        })
        self.assertFalse(errors)
        self.assertEqual(120, len(chat.direct_reply))


class InvalidJsonBudgetTests(unittest.TestCase):
    def test_cloud_profile_disables_intent_repair_call(self) -> None:
        class InvalidLLM(RecordingLLM):
            def chat_json(self, *_args, **_kwargs) -> dict:
                self.json_calls += 1
                return {"intent": "not-a-real-intent"}

        llm = InvalidLLM()
        result = IntentEngine(
            _cfg(repair_invalid_json=False), llm=llm
        ).understand(_frame("随便聊聊"), DialogueContext())
        self.assertEqual("unclear", result.intent)
        self.assertTrue(result.need_clarification)
        self.assertEqual(1, llm.json_calls)


class HardDeadlineTests(unittest.TestCase):
    def test_blocking_call_returns_control_at_hard_deadline(self) -> None:
        started = time.perf_counter()
        with self.assertRaises(HardDeadlineExceeded):
            run_with_deadline(
                lambda: time.sleep(0.25),
                0.03,
                label="unit",
            )
        self.assertLess(time.perf_counter() - started, 0.15)


class WeatherProviderFallbackTests(unittest.TestCase):
    def test_amap_is_primary_without_dedicated_qweather_host(self) -> None:
        terminal = RealTerminal()
        env = {
            "QWEATHER_KEY": "qweather-fixture",
            "AMAP_KEY": "amap-fixture",
        }
        with patch.dict(os.environ, env, clear=False), \
                patch.object(terminal, "_amap_weather", return_value="杭州：晴，28℃") as amap, \
                patch.object(terminal, "_qweather", return_value="不应调用") as qweather:
            os.environ.pop("QWEATHER_HOST", None)
            self.assertEqual("杭州：晴，28℃", terminal.weather("杭州", "今天"))
        amap.assert_called_once_with("杭州", "amap-fixture")
        qweather.assert_not_called()

    def test_dedicated_qweather_failure_falls_back_to_amap(self) -> None:
        terminal = RealTerminal()
        env = {
            "QWEATHER_KEY": "qweather-fixture",
            "QWEATHER_HOST": "example.qweather.example",
            "AMAP_KEY": "amap-fixture",
        }
        with patch.dict(os.environ, env, clear=False), \
                patch.object(terminal, "_qweather", return_value=None) as qweather, \
                patch.object(terminal, "_amap_weather", return_value="杭州：晴，28℃") as amap:
            self.assertEqual("杭州：晴，28℃", terminal.weather("杭州", "今天"))
        qweather.assert_called_once_with("杭州", "qweather-fixture")
        amap.assert_called_once_with("杭州", "amap-fixture")


if __name__ == "__main__":
    unittest.main()
