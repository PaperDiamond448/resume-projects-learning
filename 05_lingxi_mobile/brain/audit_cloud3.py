"""Cloud-3 offline integration audit. No network, model, weather, or device action."""
from __future__ import annotations

import base64
import hashlib
import json
import sys
from pathlib import Path


HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(ROOT))

import server  # noqa: E402
import visual  # noqa: E402
from cloud_vision_policy import POLICY_VERSION  # noqa: E402
from lingxi.intent.schema import IntentResult  # noqa: E402
from lingxi.response.actions import ActionExecutor, MockTerminal  # noqa: E402


PNG = b"\x89PNG\r\n\x1a\ncloud3-offline-audit"
B64 = base64.b64encode(PNG).decode("ascii")
DIGEST = hashlib.sha256(PNG).hexdigest()


def evidence(purpose: str, **overrides):
    values = {
        "cloud_vision_consent": True,
        "cloud_vision_policy": POLICY_VERSION,
        "screenshot_purpose": purpose,
        "screen_sensitivity": "clear",
        "screenshot_sha256": DIGEST,
    }
    values.update(overrides)
    return values


def main() -> None:
    checks: dict[str, bool] = {}

    old_visual = server.agent_step_visual(server.VisualPickRequest(
        goal="点第一项", screenshot=B64, elements=[{"id": 1, "text": "第一项", "bounds": [0, 0, 20, 20]}]
    ))
    checks["old_visual_client_default_denied"] = old_visual.get("action") == "blocked"

    old_extract = server.agent_extract(server.AgentExtractRequest(
        goal="找最低价格", screenshot=B64,
        elements=[{"id": 1, "text": "100元", "bounds": [0, 0, 20, 20]}],
    ))
    checks["old_extract_client_default_denied"] = old_extract.get("action") == "blocked"

    old_reflect = server.agent_reflect(server.AgentReflectRequest(
        goal="打开视频", screenshot=B64, after_screen="视频详情",
    ))
    checks["old_reflect_client_default_denied"] = (
        old_reflect.get("status") == "UNSURE"
        and old_reflect.get("model") == "privacy-gate"
    )

    sensitive = server.agent_step_visual(server.VisualPickRequest(
        goal="继续", screenshot=B64,
        elements=[{"id": 1, "text": "继续", "bounds": [0, 0, 20, 20]}],
        **evidence("visual_pick", screen_sensitivity="sensitive"),
    ))
    checks["sensitive_attestation_denied"] = sensitive.get("reason") == "SENSITIVE_SCREEN_BLOCKED"

    wrong_purpose = server.agent_step_visual(server.VisualPickRequest(
        goal="继续", screenshot=B64,
        elements=[{"id": 1, "text": "继续", "bounds": [0, 0, 20, 20]}],
        **evidence("action_reflect"),
    ))
    checks["cross_endpoint_purpose_replay_denied"] = (
        wrong_purpose.get("reason") == "CLOUD_VISION_PURPOSE_MISMATCH"
    )

    wrong_digest = server.agent_step_visual(server.VisualPickRequest(
        goal="继续", screenshot=B64,
        elements=[{"id": 1, "text": "继续", "bounds": [0, 0, 20, 20]}],
        **evidence("visual_pick", screenshot_sha256="0" * 64),
    ))
    checks["screenshot_digest_mismatch_denied"] = (
        wrong_digest.get("reason") == "SCREENSHOT_DIGEST_MISMATCH"
    )

    seen: dict[str, object] = {}
    original_visual_pick = visual.visual_pick
    original_extract_pick = visual.extract_pick
    original_reflect_action = visual.reflect_action
    original_get_llm = server._get_llm
    original_get_agent_llm = server._get_agent_llm
    try:
        def fake_visual_pick(goal, history, png, elements, hints):
            seen["visual_png"] = png
            return {"action": "tap", "x": 10, "y": 10, "mark": 1, "reason": "fake", "model": "fake"}

        def fake_extract_pick(goal, png, elements, metric, higher, hints):
            seen["extract_png"] = png
            return {"action": "done", "items": [], "reason": "fake", "model": "fake"}

        def fake_reflect(goal, subgoal, action, target, before, after, png, hints, text_llm):
            seen["reflect_png"] = png
            return {"status": "SUCCESS", "reason": "fake", "model": "fake"}

        visual.visual_pick = fake_visual_pick
        visual.extract_pick = fake_extract_pick
        visual.reflect_action = fake_reflect
        server._get_llm = lambda: object()
        server._get_agent_llm = lambda: object()

        good_visual = server.agent_step_visual(server.VisualPickRequest(
            goal="点第一项", screenshot=B64,
            elements=[{"id": 1, "text": "第一项", "bounds": [0, 0, 20, 20]}],
            **evidence("visual_pick"),
        ))
        checks["valid_visual_reaches_vlm_once"] = good_visual.get("action") == "tap" and seen.get("visual_png") == PNG

        good_extract = server.agent_extract(server.AgentExtractRequest(
            goal="找最低价格", screenshot=B64,
            elements=[{"id": 1, "text": "100元", "bounds": [0, 0, 20, 20]}],
            **evidence("structured_extract"),
        ))
        checks["valid_extract_reaches_vlm_once"] = good_extract.get("model") == "fake" and seen.get("extract_png") == PNG

        good_reflect = server.agent_reflect(server.AgentReflectRequest(
            goal="打开视频", screenshot=B64, after_screen="视频详情",
            **evidence("action_reflect"),
        ))
        checks["valid_reflect_reaches_vlm_once"] = good_reflect.get("status") == "SUCCESS" and seen.get("reflect_png") == PNG
    finally:
        visual.visual_pick = original_visual_pick
        visual.extract_pick = original_extract_pick
        visual.reflect_action = original_reflect_action
        server._get_llm = original_get_llm
        server._get_agent_llm = original_get_agent_llm

    terminal = MockTerminal()
    missing_city = ActionExecutor(terminal).execute(IntentResult("query_weather", {"date": "明天"}))
    checks["missing_city_clarifies"] = missing_city.status == "clarify" and "城市" in missing_city.feedback
    explicit_city = ActionExecutor(terminal).execute(IntentResult(
        "query_weather", {"location": "杭州", "date": "明天"}
    ))
    checks["explicit_city_not_rewritten"] = explicit_city.status == "executed" and "杭州" in explicit_city.feedback

    visual_source = (HERE / "visual.py").read_text(encoding="utf-8")
    checks["exact_three_b_only"] = (
        '_DEF_MODEL = "qwen2.5-vl-3b-instruct"' in visual_source
        and "_ALLOWED_MODELS = {_DEF_MODEL}" in visual_source
        and 'VLMConfigurationError("cloud_vlm_disabled")' in visual_source
        and 'key = _cfg("LINGXI_VLM_API_KEY")' in visual_source
    )
    checks["no_third_party_vlm_fallback"] = all(
        token not in visual_source for token in ("openrouter.ai", "OPENROUTER_API_KEY", "FIREWORKS_API_KEY")
    )

    android = ROOT / "android" / "app" / "src" / "main" / "java" / "com" / "lingxi" / "mobile"
    automator = (android / "action" / "AppAutomator.kt").read_text(encoding="utf-8")
    main_activity = (android / "MainActivity.kt").read_text(encoding="utf-8")
    checks["all_android_upload_paths_gated"] = all(
        token in automator
        for token in (
            "PURPOSE_VISUAL_PICK",
            "PURPOSE_STRUCTURED_EXTRACT",
            "PURPOSE_ACTION_REFLECT",
            "CloudVisionConsentStore.isEnabled",
        )
    )
    checks["explicit_separate_android_consent_ui"] = (
        "cloudVisionConsentButton" in main_activity
        and "我同意开启" in main_activity
        and "这不会自动上传截图" in main_activity
    )

    failed = [name for name, ok in checks.items() if not ok]
    result = {
        "status": "PASS" if not failed else "FAIL",
        "passed": len(checks) - len(failed),
        "total": len(checks),
        "checks": checks,
    }
    print(json.dumps(result, ensure_ascii=False, indent=2))
    if failed:
        raise SystemExit("Cloud-3 audit failed: " + ", ".join(failed))


if __name__ == "__main__":
    main()
