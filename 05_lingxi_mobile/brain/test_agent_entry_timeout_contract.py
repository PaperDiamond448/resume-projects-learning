"""Agent入口与独立LLM时间预算的离线契约测试；不调用真实模型或手机。"""
from __future__ import annotations

import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
if str(ROOT / "brain") not in sys.path:
    sys.path.insert(0, str(ROOT / "brain"))
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

_profiles = tempfile.TemporaryDirectory(prefix="lingxi_agent_contract_")
os.environ["LINGXI_MOBILE_PROFILE_DIR"] = _profiles.name
os.environ["DASHSCOPE_API_KEY"] = "agent-contract-no-network"
os.environ["LINGXI_SECURITY_MODE"] = "local-open"
os.environ.pop("LINGXI_DEPLOYMENT_STAGE", None)

import server  # noqa: E402


class FakeLLM:
    def __init__(self, intent: str = "unclear", slots: dict | None = None) -> None:
        self.intent = intent
        self.slots = slots or {}
        self.json_calls = 0

    def chat_json(self, *_args, **_kwargs) -> dict:
        self.json_calls += 1
        return {
            "intent": self.intent,
            "slots": self.slots,
            "confidence": 0.95,
            "conflict": False,
            "conflict_resolution": "",
            "need_clarification": self.intent == "unclear",
            "clarify_question": "请再说具体一点。" if self.intent == "unclear" else "",
            "domain": "app",
            "reasoning": "fixture",
            "direct_reply": "",
        }

    def chat(self, *_args, **_kwargs) -> str:
        raise AssertionError("本契约不应调用自由文本模型")


def _reset(uid: str, fake: FakeLLM | None = None):
    server._sessions.pop(uid, None)
    server._pending_app_task.pop(uid, None)
    sess = server._get_session(uid)
    if fake is not None:
        sess.engine.llm = fake
        sess.generator.llm = fake
    return sess


class DeterministicAgentEntryTests(unittest.TestCase):
    def test_requested_agent_examples_enter_without_llm(self) -> None:
        cases = {
            "打开酷狗音乐并播放每日推荐第一首歌": "酷狗音乐",
            "去京东搜索卷纸": "京东",
            "去淘宝搜索我的世界": "淘宝",
            "去淘宝和京东搜索ThinkPad e14，看看哪个平台更便宜": "淘宝+京东",
        }
        for index, (text, app) in enumerate(cases.items()):
            with self.subTest(text=text):
                uid = f"agent_direct_{index}"
                fake = FakeLLM()
                _reset(uid, fake)
                data = server.interact(server.InteractRequest(user_id=uid, text=text))
                self.assertEqual("run_agent", data["intent"])
                self.assertEqual("executed", data["status"])
                self.assertEqual(app, data["slots"]["app"])
                self.assertEqual(text, data["slots"]["goal"])
                self.assertEqual("run_agent", data["device_action"]["type"])
                self.assertEqual("DETERMINISTIC_AGENT_ENTRY", data["decision_reason"])
                self.assertEqual(0, fake.json_calls)

    def test_multi_app_order_follows_user_text(self) -> None:
        self.assertEqual(
            ["京东", "淘宝"],
            server._safe_direct_agent_apps("先去京东再去淘宝搜索同一款耳机比价"),
        )

    def test_plain_open_is_not_promoted_to_agent(self) -> None:
        uid = "agent_plain_open"
        fake = FakeLLM(intent="open_app", slots={"app_name": "酷狗音乐"})
        _reset(uid, fake)
        data = server.interact(server.InteractRequest(user_id=uid, text="打开酷狗音乐"))
        self.assertEqual("open_app", data["intent"])
        self.assertEqual("open_app", data["device_action"]["type"])
        self.assertEqual(1, fake.json_calls)

    def test_purchase_and_payment_words_never_use_direct_entry(self) -> None:
        cases = [
            "去淘宝购买这件商品并下单",
            "打开京东搜索手机然后立即购买",
            "在淘宝加入购物车并结算付款",
            "去京东搜索这款耳机然后买下它",
        ]
        for index, text in enumerate(cases):
            with self.subTest(text=text):
                uid = f"agent_risk_{index}"
                fake = FakeLLM()
                _reset(uid, fake)
                data = server.interact(server.InteractRequest(user_id=uid, text=text))
                self.assertNotEqual("run_agent", data["intent"])
                self.assertIsNone(data["device_action"])
                self.assertEqual(1, fake.json_calls)

    def test_visual_evidence_keeps_full_fusion_path(self) -> None:
        uid = "agent_visual_guard"
        fake = FakeLLM()
        _reset(uid, fake)
        mark = server.VisionMark(
            kind="gesture",
            label="nod",
            confidence=0.95,
            t_start=1.0,
            t_end=1.5,
            event_id="agent_visual_event",
            window_id="not-a-confirmation-window",
            stable_count=8,
            localized=True,
            quality="ok",
        )
        data = server.interact(server.InteractRequest(
            user_id=uid,
            text="打开酷狗音乐并播放每日推荐第一首歌",
            vision=[mark],
        ))
        self.assertNotEqual("run_agent", data["intent"])
        self.assertEqual(1, fake.json_calls)


class AgentBudgetTests(unittest.TestCase):
    def tearDown(self) -> None:
        server._cfg = None
        server._llm = None
        server._agent_llm = None

    def test_railway_keeps_general_four_point_five_seconds_and_agent_twelve(self) -> None:
        env = {
            "LINGXI_DEPLOYMENT_STAGE": "railway",
            "LINGXI_LLM_REQUEST_TIMEOUT_SECONDS": "4.5",
            "LINGXI_AGENT_LLM_REQUEST_TIMEOUT_SECONDS": "12.0",
            "DASHSCOPE_API_KEY": "agent-contract-no-network",
        }
        with patch.dict(os.environ, env, clear=False):
            server._cfg = None
            server._llm = None
            server._agent_llm = None
            general = server._get_llm()
            agent = server._get_agent_llm()
            self.assertEqual(4.5, general._timeout)
            self.assertEqual(4.5, general._hard_deadline)
            self.assertEqual(0, general.max_retries)
            self.assertEqual(12.0, agent._timeout)
            self.assertEqual(12.0, agent._hard_deadline)
            self.assertEqual(0, agent.max_retries)

    def test_wrong_railway_agent_budget_fails_closed(self) -> None:
        env = {
            "LINGXI_DEPLOYMENT_STAGE": "railway",
            "LINGXI_LLM_REQUEST_TIMEOUT_SECONDS": "4.5",
            "LINGXI_AGENT_LLM_REQUEST_TIMEOUT_SECONDS": "10.0",
            "DASHSCOPE_API_KEY": "agent-contract-no-network",
        }
        with patch.dict(os.environ, env, clear=False):
            server._cfg = None
            server._agent_llm = None
            with self.assertRaises(RuntimeError):
                server._get_agent_llm()


if __name__ == "__main__":
    unittest.main()
