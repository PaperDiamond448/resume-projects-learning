"""`pending-action.3` 高风险待确认契约审计。

本脚本不调用真实 14B/VLM，不写项目源码；画像写入临时目录。它同时覆盖当前
已成立的安全约束和待修复的契约缺口。只要存在不符合项，进程就返回 1。
"""
from __future__ import annotations

import json
import os
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from typing import Callable


sys.stdout.reconfigure(encoding="utf-8", errors="replace")
_profiles = tempfile.TemporaryDirectory(prefix="lingxi_pending_contract_")
os.environ["LINGXI_MOBILE_PROFILE_DIR"] = _profiles.name
# 仅让 LLMClient 完成构造；所有会触及意图/应答 LLM 的路径都会换成下方 FakeLLM。
os.environ["DASHSCOPE_API_KEY"] = "contract-audit-no-network"
sys.path.insert(0, str(Path(__file__).resolve().parent))

import server  # noqa: E402


PENDING_TEXT = "给妈妈发短信说我晚点回家"


class FakeLLM:
    def __init__(
        self,
        intent: str = "confirm",
        *,
        slots: dict | None = None,
        need_clarification: bool = False,
    ) -> None:
        self.intent = intent
        self.slots = slots or {}
        self.need_clarification = need_clarification

    def chat_json(self, *_args, **_kwargs) -> dict:
        return {
            "intent": self.intent,
            "slots": self.slots,
            "confidence": 0.99,
            "conflict": False,
            "conflict_resolution": "",
            "need_clarification": self.need_clarification,
            "clarify_question": "请补充应用" if self.need_clarification else "",
            "domain": "",
        }

    def chat(self, *_args, **_kwargs) -> str:
        return "请重新确认。"


def _reset(uid: str):
    server._sessions.pop(uid, None)
    server._pending_app_task.pop(uid, None)
    sess = server._get_session(uid)
    sess.engine.llm = FakeLLM()
    sess.generator.llm = sess.engine.llm
    return sess


def _establish(uid: str):
    sess = _reset(uid)
    first = server.interact(server.InteractRequest(user_id=uid, text=PENDING_TEXT))
    assert first["status"] == "pending_confirmation", first
    assert first.get("device_action") is None, first
    assert sess.context.pending_action is not None, first
    return sess


def _ids(sess) -> tuple[str, str]:
    pending = sess.context.pending_action
    assert pending is not None
    return pending["pending_id"], pending["confirmation_window_id"]


def _mark(
    sess,
    *,
    kind: str,
    label: str,
    confidence: float = 0.95,
    event_id: str = "event_1",
    stable_count: int | None = None,
) -> server.VisionMark:
    _, window_id = _ids(sess)
    return server.VisionMark(
        kind=kind,
        label=label,
        confidence=confidence,
        t_start=1.2,
        t_end=1.8,
        event_id=event_id,
        window_id=window_id,
        stable_count=stable_count if stable_count is not None else 5,
        localized=True,
        quality="ok",
    )


def _confirmation_request(
    uid: str,
    sess,
    *,
    text: str,
    source: str,
    vision: list[server.VisionMark] | None = None,
    pending_id: str | None = None,
    window_id: str | None = None,
) -> server.InteractRequest:
    current_pending_id, current_window_id = _ids(sess)
    return server.InteractRequest(
        user_id=uid,
        text=text,
        confirmation_source=source,
        pending_id=pending_id or current_pending_id,
        confirmation_window_id=window_id or current_window_id,
        speech_t_start=1.0,
        speech_t_end=2.0,
        vision=vision or [],
    )


def _assert_no_action(data: dict) -> None:
    assert data.get("device_action") is None, data


def explicit_confirmation_is_one_shot() -> None:
    uid = "contract_explicit_confirm"
    sess = _establish(uid)
    request = _confirmation_request(uid, sess, text="确认发送", source="typed")
    data = server.interact(request)
    action = data.get("device_action")
    assert data["intent"] == "confirm" and data["status"] == "executed", data
    assert action and action["type"] == "send_message", data
    assert action["slots"]["recipient"] == "妈妈", data
    assert sess.context.pending_action is None, data
    repeated = server.interact(request)
    _assert_no_action(repeated)
    assert repeated["status"] == "refused" and repeated["decision_reason"] == "REPLAY", repeated


def verbal_refusal_dominates_visual_affirmation() -> None:
    uid = "contract_refuse_vs_affirm"
    sess = _establish(uid)
    data = server.interact(_confirmation_request(
        uid,
        sess,
        text="不要",
        source="spoken_visual",
        vision=[_mark(sess, kind="head_motion", label="nod")],
    ))
    _assert_no_action(data)
    assert data["intent"] == "cancel" and data["status"] == "cancelled", data
    assert data["conflict"] is True and data["decision_reason"] == "EXPLICIT_TEXT_REJECTION", data
    assert sess.context.pending_action is None, data


def verbal_confirmation_with_visual_refusal_cancels_honestly() -> None:
    uid = "contract_affirm_vs_refuse"
    sess = _establish(uid)
    data = server.interact(_confirmation_request(
        uid,
        sess,
        text="确认发送",
        source="spoken_visual",
        vision=[_mark(sess, kind="head_motion", label="shake")],
    ))
    _assert_no_action(data)
    assert data["intent"] == "cancel" and data["status"] == "cancelled", data
    assert data["conflict"] is True and data["decision_reason"] == "EXPLICIT_VISUAL_REJECTION", data
    assert "超时" not in data["reply"], data
    assert sess.context.pending_action is None, data


def ambiguous_affirmation_never_executes() -> None:
    uid = "contract_ambiguous"
    sess = _establish(uid)
    data = server.interact(_confirmation_request(
        uid,
        sess,
        text="要",
        source="spoken_visual",
        vision=[_mark(sess, kind="gesture", label="thumb_up", confidence=0.92)],
    ))
    _assert_no_action(data)
    assert data["status"] == "pending_confirmation", data
    assert sess.context.pending_action is not None, data


def recipient_change_invalidates_old_pending() -> None:
    uid = "contract_recipient_change"
    sess = _establish(uid)
    data = server.interact(server.InteractRequest(user_id=uid, text="不对，是发给李娜"))
    _assert_no_action(data)
    assert sess.context.pending_action is None, {
        "response": data,
        "pending_after": sess.context.pending_action,
    }


def content_change_invalidates_old_pending() -> None:
    uid = "contract_content_change"
    sess = _establish(uid)
    data = server.interact(server.InteractRequest(user_id=uid, text="内容改成堵车了要晚点"))
    _assert_no_action(data)
    assert sess.context.pending_action is None, {
        "response": data,
        "pending_after": sess.context.pending_action,
    }


def new_task_invalidates_old_pending() -> None:
    uid = "contract_new_task"
    sess = _establish(uid)
    data = server.interact(server.InteractRequest(user_id=uid, text="打开相机"))
    action = data.get("device_action")
    assert action and action["type"] == "open_camera", data
    assert sess.context.pending_action is None, {
        "response": data,
        "pending_after": sess.context.pending_action,
    }


def spoken_complete_new_task_invalidates_old_pending() -> None:
    """KWS confirmation capture must not turn an explicit new task into confirmation text."""
    uid = "contract_spoken_new_weather_task"
    sess = _establish(uid)
    sess.engine.llm = FakeLLM(
        intent="query_weather",
        slots={"location": "杭州", "date": "明天"},
    )
    sess.generator.llm = sess.engine.llm
    sess.terminal.weather = lambda location, date: f"{location}{date}天气测试结果"
    data = server.interact(_confirmation_request(
        uid,
        sess,
        text="查询明天杭州天气",
        source="spoken",
    ))
    _assert_no_action(data)
    assert data["intent"] == "query_weather" and data["status"] == "executed", data
    assert data["decision_reason"] == "NEW_TASK", data
    assert data.get("pending_id") is None, data
    assert data.get("confirmation_window_id") is None, data
    assert sess.context.pending_action is None, data


def spoken_new_task_with_wrong_binding_fails_closed() -> None:
    uid = "contract_spoken_new_task_wrong_binding"
    sess = _establish(uid)
    data = server.interact(_confirmation_request(
        uid,
        sess,
        text="查询明天杭州天气",
        source="spoken",
        pending_id="pending_stale",
    ))
    _assert_no_action(data)
    assert data["status"] == "pending_confirmation", data
    assert data["decision_reason"] == "PENDING_ID_MISMATCH", data
    assert sess.context.pending_action is not None, data


def explicit_retry_preserves_without_execution() -> None:
    uid = "contract_retry"
    sess = _establish(uid)
    old_window = sess.context.pending_action["confirmation_window_id"]
    data = server.interact(server.InteractRequest(user_id=uid, text="再来一次"))
    _assert_no_action(data)
    assert data["status"] == "pending_confirmation", data
    assert sess.context.pending_action is not None, data
    assert data["confirmation_window_id"] != old_window, data


def expired_confirmation_never_executes() -> None:
    uid = "contract_expired"
    sess = _establish(uid)
    sess._pending_age = sess.max_pending_turns
    data = server.interact(server.InteractRequest(user_id=uid, text="确认发送"))
    _assert_no_action(data)
    assert data["status"] == "expired" and data["decision_reason"] == "TIMEOUT", data
    assert "超时" in data["reply"], data
    assert sess.context.pending_action is None, data


def upgraded_run_agent_does_not_rehang_old_app_pending() -> None:
    uid = "contract_run_agent_pending"
    sess = _reset(uid)
    fake = FakeLLM(
        intent="open_app",
        slots={"app": "酷狗音乐"},
        need_clarification=True,
    )
    sess.engine.llm = fake
    sess.generator.llm = fake
    server._pending_app_task[uid] = {
        "text": "打开酷狗音乐日推的第三首歌",
        "age": 0,
    }
    data = server.interact(server.InteractRequest(user_id=uid, text="酷狗音乐"))
    action = data.get("device_action")
    assert action and action["type"] == "run_agent", data
    assert uid not in server._pending_app_task, server._pending_app_task.get(uid)


def brain_and_core_agree_on_high_risk_types() -> None:
    uid = "contract_risk_sets"
    sess = _reset(uid)
    expected = {"send_message", "make_call"}
    assert expected.issubset(server.HIGH_RISK_PENDING_TYPES)
    assert expected.issubset(sess.engine.high_risk_actions), {
        "brain_adapter": sorted(server.HIGH_RISK_PENDING_TYPES),
        "core_engine": sorted(sess.engine.high_risk_actions),
        "missing": sorted(expected - sess.engine.high_risk_actions),
    }


def missing_source_fails_closed() -> None:
    uid = "contract_missing_source"
    sess = _establish(uid)
    pending_id, window_id = _ids(sess)
    data = server.interact(server.InteractRequest(
        user_id=uid,
        text="确认发送",
        pending_id=pending_id,
        confirmation_window_id=window_id,
    ))
    _assert_no_action(data)
    assert data["decision_reason"] == "SOURCE_MISMATCH", data
    assert sess.context.pending_action is not None, data


def wrong_pending_id_fails_closed() -> None:
    uid = "contract_wrong_pending"
    sess = _establish(uid)
    data = server.interact(_confirmation_request(
        uid, sess, text="确认发送", source="spoken", pending_id="pending_stale",
    ))
    _assert_no_action(data)
    assert data["decision_reason"] == "PENDING_ID_MISMATCH", data


def wrong_window_id_fails_closed() -> None:
    uid = "contract_wrong_window"
    sess = _establish(uid)
    data = server.interact(_confirmation_request(
        uid, sess, text="确认发送", source="spoken", window_id="window_stale",
    ))
    _assert_no_action(data)
    assert data["decision_reason"] == "WINDOW_ID_MISMATCH", data


def vision_only_stays_shadow_until_threshold_is_frozen() -> None:
    uid = "contract_vision_shadow"
    sess = _establish(uid)
    data = server.interact(_confirmation_request(
        uid,
        sess,
        text="确认",
        source="vision_only",
        vision=[_mark(sess, kind="gesture", label="thumb_up", confidence=0.99)],
    ))
    _assert_no_action(data)
    assert data["status"] == "pending_confirmation", data
    assert data["decision_reason"] == "VISION_ONLY_DISABLED", data
    assert sess.context.pending_action is not None, data


def visual_rejection_can_cancel_without_spoken_text() -> None:
    uid = "contract_visual_reject"
    sess = _establish(uid)
    data = server.interact(_confirmation_request(
        uid,
        sess,
        text="取消",
        source="vision_only",
        vision=[_mark(sess, kind="gesture", label="thumb_down", confidence=0.98)],
    ))
    _assert_no_action(data)
    assert data["status"] == "cancelled", data
    assert data["decision_reason"] == "EXPLICIT_VISUAL_REJECTION", data
    assert sess.context.pending_action is None, data


def visual_event_replay_is_rejected() -> None:
    uid = "contract_visual_replay"
    sess = _establish(uid)
    mark = _mark(sess, kind="gesture", label="thumb_up", event_id="event_replay")
    first = server.interact(_confirmation_request(
        uid, sess, text="确认", source="vision_only", vision=[mark],
    ))
    _assert_no_action(first)
    second = server.interact(_confirmation_request(
        uid, sess, text="确认", source="vision_only", vision=[mark],
    ))
    _assert_no_action(second)
    assert second["decision_reason"] == "REPLAY", second


def unsupported_visual_event_never_authorizes() -> None:
    uid = "contract_unsupported_visual"
    sess = _establish(uid)
    data = server.interact(_confirmation_request(
        uid,
        sess,
        text="确认",
        source="vision_only",
        vision=[_mark(sess, kind="gesture", label="victory", confidence=0.99)],
    ))
    _assert_no_action(data)
    assert data["decision_reason"] == "UNSUPPORTED_VISUAL_EVENT", data


def low_confidence_visual_event_never_authorizes() -> None:
    uid = "contract_low_visual"
    sess = _establish(uid)
    data = server.interact(_confirmation_request(
        uid,
        sess,
        text="确认",
        source="vision_only",
        vision=[_mark(sess, kind="gesture", label="thumb_up", confidence=0.60)],
    ))
    _assert_no_action(data)
    assert data["decision_reason"] == "LOW_VISUAL_CONFIDENCE", data


def transient_visual_event_never_becomes_strong_evidence() -> None:
    uid = "contract_transient_visual"
    sess = _establish(uid)
    data = server.interact(_confirmation_request(
        uid,
        sess,
        text="确认",
        source="vision_only",
        vision=[_mark(
            sess, kind="gesture", label="thumb_up", confidence=0.99, stable_count=3,
        )],
    ))
    _assert_no_action(data)
    assert data["decision_reason"] == "LOW_VISUAL_CONFIDENCE", data
    assert "至少需要 5 帧" in data["reply"], data


def concurrent_confirmation_consumes_pending_once() -> None:
    uid = "contract_atomic_consume"
    sess = _establish(uid)
    request = _confirmation_request(uid, sess, text="确认发送", source="typed")
    with ThreadPoolExecutor(max_workers=2) as pool:
        results = list(pool.map(lambda _i: server.interact(request), range(2)))
    actions = [item.get("device_action") for item in results if item.get("device_action")]
    assert len(actions) == 1, results
    assert sum(item.get("decision_reason") == "REPLAY" for item in results) == 1, results


def cancelled_pending_rejects_late_unknown_envelope() -> None:
    """A late empty camera callback must not resurrect a consumed high-risk action."""
    uid = "contract_late_unknown_after_cancel"
    sess = _establish(uid)
    old_pending_id, old_window_id = _ids(sess)
    cancelled = server.interact(_confirmation_request(
        uid,
        sess,
        text="不要发送",
        source="typed",
    ))
    _assert_no_action(cancelled)
    assert cancelled["status"] == "cancelled", cancelled
    assert sess.context.pending_action is None, cancelled

    # If the orphan envelope leaks into the ordinary LLM path, this fake route recreates
    # a call pending exactly like the true-device regression found on 2026-08-07.
    sess.engine.llm = FakeLLM(intent="make_call", slots={"contact": "测试联系人"})
    sess.generator.llm = sess.engine.llm
    late = server.interact(server.InteractRequest(
        user_id=uid,
        text="",
        confirmation_source="unknown",
        pending_id=old_pending_id,
        confirmation_window_id=old_window_id,
    ))
    _assert_no_action(late)
    assert late["status"] == "refused", late
    assert late["decision_reason"] == "REPLAY", late
    assert late.get("pending_id") is None, late
    assert late.get("confirmation_window_id") is None, late
    assert sess.context.pending_action is None, late


def pending_context_question_does_not_invalidate() -> None:
    uid = "contract_pending_context"
    sess = _establish(uid)
    pending_id, _ = _ids(sess)
    data = server.interact(server.InteractRequest(user_id=uid, text="你刚才说发给谁？"))
    _assert_no_action(data)
    assert data["status"] == "pending_confirmation", data
    assert data["pending_id"] == pending_id, data
    assert sess.context.pending_action is not None, data


def complete_replacement_creates_fresh_pending() -> None:
    uid = "contract_full_replacement"
    sess = _establish(uid)
    old_pending_id, _ = _ids(sess)
    data = server.interact(server.InteractRequest(
        user_id=uid, text="给李娜发短信说会议改到三点",
    ))
    _assert_no_action(data)
    assert data["status"] == "pending_confirmation", data
    assert data["pending_id"] != old_pending_id, data
    assert sess.context.pending_action["slots"]["recipient"] == "李娜", data
    assert sess.context.pending_action["slots"]["content"] == "会议改到三点", data


def android_has_no_affirmative_fallback() -> None:
    root = Path(__file__).resolve().parents[1]
    main = (root / "android/app/src/main/java/com/lingxi/mobile/MainActivity.kt").read_text(encoding="utf-8")
    kws = (root / "android/app/src/main/java/com/lingxi/mobile/voice/KwsForegroundService.kt").read_text(encoding="utf-8")
    policy = (root / "android/app/src/main/java/com/lingxi/mobile/confirmation/ConfirmationEvidencePolicy.kt").read_text(encoding="utf-8")
    assert 'else -> "确认"' not in main, "MainActivity 仍存在无证据肯定兜底"
    assert 'else -> "确认"' not in kws, "KWS 仍存在无证据肯定兜底"
    assert 'visualAccept = setOf("nod", "thumb_up")' in policy, "高风险肯定白名单必须只含 nod/thumb_up"
    assert "else -> null" in policy, "空/非授权视觉必须返回 null"
    assert 'val text = evidence?.normalizedText.orEmpty()' not in kws, "KWS 不得把 null evidence 降成空文本继续发送"
    assert 'val source = evidence?.source ?: "unknown"' not in kws, "KWS 不得把 null evidence 降成 unknown 继续发送"


def android_consumes_only_non_null_device_action() -> None:
    root = Path(__file__).resolve().parents[1]
    main = (root / "android/app/src/main/java/com/lingxi/mobile/MainActivity.kt").read_text(encoding="utf-8")
    kws = (root / "android/app/src/main/java/com/lingxi/mobile/voice/KwsForegroundService.kt").read_text(encoding="utf-8")
    executor = (root / "android/app/src/main/java/com/lingxi/mobile/action/DeviceActionExecutor.kt").read_text(encoding="utf-8")
    assert "deviceAction?.let" in main, "MainActivity 未以 nullable device_action 为执行闸"
    assert "resp.device_action?.let" in kws, "KWS 服务未以 nullable device_action 为执行闸"
    assert '"send_message" -> sendMessage(slots)' in executor, "Android 动作消费映射缺失"


def android_kws_typed_handoff_is_owned_and_single_path() -> None:
    root = Path(__file__).resolve().parents[1]
    main = (root / "android/app/src/main/java/com/lingxi/mobile/MainActivity.kt").read_text(encoding="utf-8")
    kws = (root / "android/app/src/main/java/com/lingxi/mobile/voice/KwsForegroundService.kt").read_text(encoding="utf-8")
    store = (root / "android/app/src/main/java/com/lingxi/mobile/confirmation/PendingConfirmationStore.kt").read_text(encoding="utf-8")
    policy = (root / "android/app/src/main/java/com/lingxi/mobile/confirmation/TypedConfirmationHandoffPolicy.kt").read_text(encoding="utf-8")
    assert "PendingConfirmationOwner" in store and '.putString(OWNER, owner.wireValue)' in store, (
        "共享 pending binding 必须记录 KWS/Activity 所有者"
    )
    handoff_call = main.index("if (handleKwsTypedConfirmation(text)) return")
    activity_capture = main.index("if (handleAutoConfirmText(text)) return")
    direct_submit = main.index("submitText(text)", handoff_call)
    assert handoff_call < activity_capture < direct_submit, (
        "必须先按 pending owner 分流 KWS handoff，再尝试 Activity 窗口，最后才可走普通 typed"
    )
    debug_receiver = main[
        main.index("private val debugSubmitReceiver"):
        main.index("override fun onCreate")
    ]
    debug_handoff = debug_receiver.index("handleKwsTypedConfirmation(text)")
    debug_activity_capture = debug_receiver.index("handleAutoConfirmText(text)")
    debug_direct_submit = debug_receiver.index("submitText(text)")
    assert debug_handoff < debug_activity_capture < debug_direct_submit, (
        "真机回归文本入口也必须复用 KWS owner handoff，不能绕回分离请求"
    )
    assert "pendingTypedConfirmText = text.trim()" in kws, "KWS 未接收 typed evidence"
    assert "TypedConfirmationHandoffPolicy.resolveText(typed, spoken)" in kws, (
        "KWS 未在同一窗口合并 typed/spoken evidence"
    )
    assert "PendingConfirmationOwner.KWS" in kws, "KWS 创建的 pending 未声明所有权"
    assert "BINDING_MISMATCH" in policy and "WINDOW_CLOSED" in policy, (
        "typed handoff 必须对错 ID 和关闭窗口 fail closed"
    )


CASES: list[tuple[str, Callable[[], None]]] = [
    ("explicit_confirmation_is_one_shot", explicit_confirmation_is_one_shot),
    ("verbal_refusal_dominates_visual_affirmation", verbal_refusal_dominates_visual_affirmation),
    ("verbal_confirmation_with_visual_refusal_cancels_honestly", verbal_confirmation_with_visual_refusal_cancels_honestly),
    ("ambiguous_affirmation_never_executes", ambiguous_affirmation_never_executes),
    ("recipient_change_invalidates_old_pending", recipient_change_invalidates_old_pending),
    ("content_change_invalidates_old_pending", content_change_invalidates_old_pending),
    ("new_task_invalidates_old_pending", new_task_invalidates_old_pending),
    ("spoken_complete_new_task_invalidates_old_pending", spoken_complete_new_task_invalidates_old_pending),
    ("spoken_new_task_with_wrong_binding_fails_closed", spoken_new_task_with_wrong_binding_fails_closed),
    ("explicit_retry_preserves_without_execution", explicit_retry_preserves_without_execution),
    ("expired_confirmation_never_executes", expired_confirmation_never_executes),
    ("upgraded_run_agent_does_not_rehang_old_app_pending", upgraded_run_agent_does_not_rehang_old_app_pending),
    ("brain_and_core_agree_on_high_risk_types", brain_and_core_agree_on_high_risk_types),
    ("missing_source_fails_closed", missing_source_fails_closed),
    ("wrong_pending_id_fails_closed", wrong_pending_id_fails_closed),
    ("wrong_window_id_fails_closed", wrong_window_id_fails_closed),
    ("vision_only_stays_shadow_until_threshold_is_frozen", vision_only_stays_shadow_until_threshold_is_frozen),
    ("visual_rejection_can_cancel_without_spoken_text", visual_rejection_can_cancel_without_spoken_text),
    ("visual_event_replay_is_rejected", visual_event_replay_is_rejected),
    ("unsupported_visual_event_never_authorizes", unsupported_visual_event_never_authorizes),
    ("low_confidence_visual_event_never_authorizes", low_confidence_visual_event_never_authorizes),
    ("transient_visual_event_never_becomes_strong_evidence", transient_visual_event_never_becomes_strong_evidence),
    ("concurrent_confirmation_consumes_pending_once", concurrent_confirmation_consumes_pending_once),
    ("cancelled_pending_rejects_late_unknown_envelope", cancelled_pending_rejects_late_unknown_envelope),
    ("pending_context_question_does_not_invalidate", pending_context_question_does_not_invalidate),
    ("complete_replacement_creates_fresh_pending", complete_replacement_creates_fresh_pending),
    ("android_has_no_affirmative_fallback", android_has_no_affirmative_fallback),
    ("android_consumes_only_non_null_device_action", android_consumes_only_non_null_device_action),
    ("android_kws_typed_handoff_is_owned_and_single_path", android_kws_typed_handoff_is_owned_and_single_path),
]


def main() -> int:
    results = []
    try:
        for name, case in CASES:
            try:
                case()
                results.append({"case": name, "status": "PASS"})
            except Exception as exc:  # noqa: BLE001 - 审计器必须继续收集其余失败
                results.append({
                    "case": name,
                    "status": "FAIL",
                    "error": f"{type(exc).__name__}: {exc}",
                })
        failed = sum(item["status"] == "FAIL" for item in results)
        print(json.dumps({
            "contract": "pending-action.3",
            "summary": {
                "total": len(results),
                "passed": len(results) - failed,
                "failed": failed,
            },
            "results": results,
        }, ensure_ascii=False, indent=2))
        return 1 if failed else 0
    finally:
        _profiles.cleanup()


if __name__ == "__main__":
    raise SystemExit(main())
