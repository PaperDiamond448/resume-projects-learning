"""Cloud-2 server integration audit.  Uses no external model or business API."""
from __future__ import annotations

import hashlib
import json
import logging
import os
import sqlite3
import sys
import tempfile
import uuid
from pathlib import Path


HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(ROOT))


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="lingxi-cloud2-audit-") as temp_name:
        temp = Path(temp_name)
        token = "lx1_" + "C" * 43
        auth_file = temp / "devices.json"
        db_file = temp / "idempotency.sqlite3"
        auth_file.write_text(
            json.dumps(
                {
                    "schema_version": "lingxi-device-auth.1",
                    "devices": [
                        {
                            "subject": "android",
                            "device_id": "mi8lite-audit",
                            "token_sha256": hashlib.sha256(token.encode("ascii")).hexdigest(),
                            "enabled": True,
                        }
                    ],
                }
            ),
            encoding="utf-8",
        )
        os.environ["LINGXI_SECURITY_MODE"] = "required"
        os.environ["LINGXI_DEVICE_AUTH_FILE"] = str(auth_file)
        os.environ["LINGXI_IDEMPOTENCY_DB"] = str(db_file)
        os.environ["LINGXI_MAX_REQUEST_BYTES"] = str(64 * 1024)
        os.environ.setdefault("DASHSCOPE_API_KEY", "cloud2-audit-no-network")

        from fastapi.testclient import TestClient
        import server

        headers = {"Authorization": f"Bearer {token}"}
        client = TestClient(server.app)
        passed: list[str] = []

        def check(name: str, condition: bool, detail: object = None) -> None:
            if not condition:
                raise AssertionError(f"{name}: {detail}")
            passed.append(name)

        check(
            "legacy_content_logs_suppressed",
            server._secure_api
            and logging.getLogger("lingxi").level == logging.CRITICAL
            and logging.getLogger("lingxi.cloud_security").level == logging.INFO,
        )

        api_kt = (ROOT / "android/app/src/main/java/com/lingxi/mobile/model/Api.kt").read_text(encoding="utf-8")
        client_kt = (ROOT / "android/app/src/main/java/com/lingxi/mobile/net/BrainClient.kt").read_text(encoding="utf-8")
        store_kt = (ROOT / "android/app/src/main/java/com/lingxi/mobile/net/BrainConnectionStore.kt").read_text(encoding="utf-8")
        policy_kt = (ROOT / "android/app/src/main/java/com/lingxi/mobile/net/BrainConnectionPolicy.kt").read_text(encoding="utf-8")
        manifest = (ROOT / "android/app/src/main/AndroidManifest.xml").read_text(encoding="utf-8")
        check(
            "android_all_post_models_have_request_id",
            api_kt.count("val request_id: String = UUID.randomUUID().toString()") == 9,
        )
        check(
            "android_bearer_is_runtime_only",
            'request.header("Authorization", "Bearer $it")' in client_kt
            and "tokenProvider" in client_kt
            and "X-Lingxi-Client-Version" in client_kt,
        )
        check(
            "android_keystore_and_endpoint_policy",
            all(
                token in store_kt
                for token in ("AndroidKeyStore", "AES/GCM/NoPadding", "token_cipher")
            )
            and "正式版只允许HTTPS" in policy_kt
            and "DEBUG_HTTP_HOSTS" in policy_kt
            and 'android:name=".LingxiApplication"' in manifest,
        )

        live = client.get("/live")
        check("public_minimal_liveness", live.status_code == 200 and live.json() == {"status": "alive"}, live.text)

        unauth = client.get("/health")
        check("health_requires_auth", unauth.status_code == 401 and unauth.json().get("device_action") is None, unauth.text)

        wrong = client.get("/health", headers={"Authorization": "Bearer " + "x" * 48})
        check("wrong_token", wrong.status_code == 401 and wrong.json().get("reason") == "INVALID_TOKEN", wrong.text)

        ready = client.get("/ready", headers=headers)
        check("authenticated_readiness", ready.status_code == 200 and ready.json().get("status") == "ready", ready.text)

        docs = client.get("/docs", headers=headers)
        check("production_docs_closed", docs.status_code == 404, docs.text)

        before_sessions = set(server._sessions)
        mismatch = client.post(
            "/interact",
            json={"request_id": str(uuid.uuid4()), "user_id": "other", "text": "打开相机"},
            headers=headers,
        )
        check(
            "subject_mismatch_zero_state",
            mismatch.status_code == 403
            and mismatch.json().get("device_action") is None
            and set(server._sessions) == before_sessions,
            mismatch.text,
        )

        no_id = client.post("/interact", json={"user_id": "android", "text": "打开相机"}, headers=headers)
        check("request_id_required", no_id.status_code == 400 and no_id.json().get("device_action") is None, no_id.text)

        wrong_media = client.post(
            "/interact",
            content=b"{}",
            headers={**headers, "Content-Type": "text/plain"},
        )
        check("json_media_required", wrong_media.status_code == 415, wrong_media.text)

        too_large = client.post(
            "/interact",
            json={
                "request_id": str(uuid.uuid4()),
                "user_id": "android",
                "text": "x" * (70 * 1024),
            },
            headers=headers,
        )
        check("body_limit", too_large.status_code == 413 and too_large.json().get("device_action") is None, too_large.text)

        action_id = str(uuid.uuid4())
        action_payload = {"request_id": action_id, "user_id": "android", "text": "打开相机"}
        first = client.post("/interact", json=action_payload, headers=headers)
        check(
            "first_action_once",
            first.status_code == 200
            and (first.json().get("device_action") or {}).get("type") == "open_camera",
            first.text,
        )
        replay = client.post("/interact", json=action_payload, headers=headers)
        check(
            "replay_zero_action",
            replay.status_code == 409
            and replay.json().get("reason") == "IDEMPOTENT_REPLAY"
            and replay.json().get("device_action") is None,
            replay.text,
        )

        pending = client.post(
            "/interact",
            json={
                "request_id": str(uuid.uuid4()),
                "user_id": "android",
                "text": "给测试联系人发短信说晚点到",
            },
            headers=headers,
        )
        pdata = pending.json()
        check(
            "pending_created",
            pending.status_code == 200
            and pdata.get("status") == "pending_confirmation"
            and pdata.get("device_action") is None,
            pending.text,
        )
        old_pending = pdata.get("pending_id")
        old_window = pdata.get("confirmation_window_id")
        server._sessions.clear()
        server._session_locks.clear()
        orphan = client.post(
            "/interact",
            json={
                "request_id": str(uuid.uuid4()),
                "user_id": "android",
                "text": "确认发送",
                "pending_id": old_pending,
                "confirmation_window_id": old_window,
                "confirmation_source": "typed",
            },
            headers=headers,
        )
        check(
            "restart_orphan_zero_action",
            orphan.status_code == 200
            and orphan.json().get("decision_reason") == "ORPHAN"
            and orphan.json().get("device_action") is None,
            orphan.text,
        )

        db_bytes = db_file.read_bytes()
        conn = sqlite3.connect(str(db_file))
        try:
            columns = [row[1] for row in conn.execute("PRAGMA table_info(consumed_requests)")]
            rows = conn.execute("SELECT request_id,request_hash,state FROM consumed_requests").fetchall()
        finally:
            conn.close()
        check(
            "ledger_metadata_only",
            "request_hash" in columns
            and "request_body" not in columns
            and rows
            and token.encode("utf-8") not in db_bytes
            and "打开相机".encode("utf-8") not in db_bytes,
            columns,
        )

        print(
            json.dumps(
                {
                    "status": "PASS",
                    "passed": len(passed),
                    "checks": passed,
                    "external_model_calls": 0,
                    "device_actions_observed": 1,
                    "device_actions_executed": 0,
                },
                ensure_ascii=False,
                indent=2,
            )
        )


if __name__ == "__main__":
    main()
