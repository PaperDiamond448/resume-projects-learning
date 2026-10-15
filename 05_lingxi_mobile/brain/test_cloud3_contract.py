from __future__ import annotations

import base64
import hashlib
import os
import sys
import unittest
from pathlib import Path
from unittest.mock import patch


HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(ROOT))

import cloud_vision_policy as policy  # noqa: E402
import visual  # noqa: E402
from lingxi.intent.schema import IntentResult  # noqa: E402
from lingxi.response.actions import ActionExecutor, MockTerminal, RealTerminal  # noqa: E402


PNG = b"\x89PNG\r\n\x1a\ncloud3-contract-probe"
B64 = base64.b64encode(PNG).decode("ascii")
DIGEST = hashlib.sha256(PNG).hexdigest()


def valid_gate(**overrides):
    values = {
        "screenshot_b64": B64,
        "consent": True,
        "policy_version": policy.POLICY_VERSION,
        "purpose": "visual_pick",
        "expected_purpose": "visual_pick",
        "screen_sensitivity": "clear",
        "screenshot_sha256": DIGEST,
    }
    values.update(overrides)
    return policy.validate_cloud_screenshot(**values)


class CloudVisionGateTest(unittest.TestCase):
    def test_valid_evidence_binds_exact_png(self):
        decision = valid_gate()
        self.assertTrue(decision.allowed)
        self.assertEqual(PNG, decision.png)

    def test_consent_is_default_deny(self):
        self.assertEqual(
            "CLOUD_VISION_CONSENT_REQUIRED",
            valid_gate(consent=False).code,
        )

    def test_sensitive_and_unknown_screens_fail_closed(self):
        for sensitivity in ("sensitive", "unknown", ""):
            with self.subTest(sensitivity=sensitivity):
                decision = valid_gate(screen_sensitivity=sensitivity)
                self.assertFalse(decision.allowed)
                self.assertEqual("SENSITIVE_SCREEN_BLOCKED", decision.code)

    def test_purpose_cannot_be_replayed_across_endpoints(self):
        decision = valid_gate(expected_purpose="structured_extract")
        self.assertFalse(decision.allowed)
        self.assertEqual("CLOUD_VISION_PURPOSE_MISMATCH", decision.code)

    def test_digest_mismatch_and_non_png_are_blocked(self):
        self.assertEqual(
            "SCREENSHOT_DIGEST_MISMATCH",
            valid_gate(screenshot_sha256="0" * 64).code,
        )
        raw = b"not-a-png"
        self.assertEqual(
            "SCREENSHOT_NOT_PNG",
            valid_gate(
                screenshot_b64=base64.b64encode(raw).decode(),
                screenshot_sha256=hashlib.sha256(raw).hexdigest(),
            ).code,
        )

    def test_only_frozen_three_b_model_is_accepted(self):
        env = {
            "LINGXI_VLM_API_KEY": "unit-test-only",
            "LINGXI_VLM_MODEL": "qwen2.5-vl-7b-instruct",
        }
        with patch.dict(os.environ, env, clear=False):
            with self.assertRaises(visual.VLMConfigurationError):
                visual._get_client()

    def test_missing_key_has_no_dummy_or_third_party_fallback(self):
        keys = {
            "LINGXI_VLM_API_KEY": "",
            "DASHSCOPE_API_KEY": "china-region-key-must-not-be-reused",
            "OPENROUTER_API_KEY": "should-not-be-used",
            "FIREWORKS_API_KEY": "should-not-be-used",
            "LINGXI_VLM_MODEL": "qwen2.5-vl-3b-instruct",
        }
        with patch.dict(os.environ, keys, clear=False):
            with self.assertRaises(visual.VLMConfigurationError):
                visual._get_client()
        disabled = {
            "LINGXI_CLOUD_VLM_ENABLED": "false",
            "LINGXI_VLM_API_KEY": "even-a-present-key-must-not-bypass-disable",
        }
        with patch.dict(os.environ, disabled, clear=False):
            with self.assertRaisesRegex(visual.VLMConfigurationError, "cloud_vlm_disabled"):
                visual._get_client()


class _WeatherTerminal(MockTerminal):
    def __init__(self):
        super().__init__()
        self.weather_calls = []

    def weather(self, location: str, date: str) -> str:
        self.weather_calls.append((location, date))
        return f"REAL:{location}:{date}"


class CloudLocationContractTest(unittest.TestCase):
    def test_missing_or_current_city_clarifies_without_calling_backend(self):
        server_source = (HERE / "server.py").read_text(encoding="utf-8")
        self.assertNotIn("def _amap_ip_city", server_source)
        self.assertNotIn("_amap_ip_city()", server_source)
        for location in ("", "本地", "当前", "未知", "明天"):
            terminal = _WeatherTerminal()
            outcome = ActionExecutor(terminal).execute(
                IntentResult("query_weather", {"location": location, "date": "明天"})
            )
            with self.subTest(location=location):
                self.assertEqual("clarify", outcome.status)
                self.assertIn("城市", outcome.feedback)
                self.assertEqual([], terminal.weather_calls)

    def test_explicit_city_reaches_backend_unchanged(self):
        terminal = _WeatherTerminal()
        outcome = ActionExecutor(terminal).execute(
            IntentResult("query_weather", {"location": "杭州", "date": "明天"})
        )
        self.assertEqual("executed", outcome.status)
        self.assertEqual([("杭州", "明天")], terminal.weather_calls)
        self.assertIn("REAL:杭州:明天", outcome.feedback)

    def test_real_terminal_never_fabricates_weather_without_key(self):
        with patch.dict(
            os.environ,
            {"QWEATHER_KEY": "", "QWEATHER_HOST": "", "AMAP_KEY": ""},
            clear=False,
        ):
            text = RealTerminal().weather("杭州", "今天")
        self.assertIn("暂不可用", text)
        self.assertNotIn("晴转多云", text)


if __name__ == "__main__":
    unittest.main()
