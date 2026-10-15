from __future__ import annotations

import hashlib
import json
import sys
import tempfile
import unittest
import uuid
from pathlib import Path

from fastapi import FastAPI
from fastapi.testclient import TestClient


HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

from cloud_security import AUTH_SCHEMA, CloudSecurityMiddleware, SecurityConfig  # noqa: E402


class CloudSecurityMiddlewareTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory(prefix="lingxi-cloud2-test-")
        root = Path(self.temp.name)
        self.auth_file = root / "devices.json"
        self.db_file = root / "idempotency.sqlite3"
        self.token = "lx1_" + "A" * 43
        self.other_token = "lx1_" + "B" * 43
        self._write_registry(first_enabled=True)
        self.config = SecurityConfig(
            mode="required",
            auth_file=self.auth_file,
            idempotency_db=self.db_file,
            max_request_bytes=64 * 1024,
        )

    def tearDown(self) -> None:
        self.temp.cleanup()

    def _write_registry(self, *, first_enabled: bool) -> None:
        data = {
            "schema_version": AUTH_SCHEMA,
            "devices": [
                {
                    "subject": "android",
                    "device_id": "mi8lite",
                    "token_sha256": hashlib.sha256(self.token.encode("ascii")).hexdigest(),
                    "enabled": first_enabled,
                },
                {
                    "subject": "backup",
                    "device_id": "test-backup",
                    "token_sha256": hashlib.sha256(self.other_token.encode("ascii")).hexdigest(),
                    "enabled": True,
                },
            ],
        }
        self.auth_file.write_text(json.dumps(data), encoding="utf-8")

    def _headers(self, token: str | None = None) -> dict[str, str]:
        return {"Authorization": f"Bearer {token or self.token}"}

    def _app(self, calls: list[dict]) -> FastAPI:
        app = FastAPI(docs_url=None, redoc_url=None, openapi_url=None)
        app.add_middleware(CloudSecurityMiddleware, config=self.config)

        @app.get("/live")
        def live() -> dict:
            return {"status": "alive"}

        @app.get("/health")
        def health() -> dict:
            return {"status": "ok", "detail": "private"}

        @app.post("/interact")
        def interact(payload: dict) -> dict:
            calls.append(payload)
            return {"status": "executed", "device_action": {"type": "open_camera"}}

        @app.post("/other")
        def other(payload: dict) -> dict:
            calls.append(payload)
            return {"status": "executed", "device_action": {"type": "open_setting"}}

        return app

    @staticmethod
    def _payload(**updates: object) -> dict:
        payload: dict[str, object] = {
            "request_id": str(uuid.uuid4()),
            "user_id": "android",
            "text": "打开相机",
        }
        payload.update(updates)
        return payload

    def test_public_liveness_is_minimal_but_health_requires_auth(self) -> None:
        calls: list[dict] = []
        client = TestClient(self._app(calls))
        self.assertEqual(client.get("/live").json(), {"status": "alive"})
        denied = client.get("/health")
        self.assertEqual(denied.status_code, 401)
        self.assertEqual(denied.json()["reason"], "AUTH_REQUIRED")
        self.assertEqual(denied.json()["device_action"], None)
        self.assertEqual(client.get("/health", headers=self._headers()).status_code, 200)

    def test_wrong_and_revoked_tokens_fail_closed(self) -> None:
        client = TestClient(self._app([]))
        wrong = client.get("/health", headers=self._headers("lx1_" + "Z" * 43))
        self.assertEqual((wrong.status_code, wrong.json()["reason"]), (401, "INVALID_TOKEN"))
        self._write_registry(first_enabled=False)
        revoked = client.get("/health", headers=self._headers())
        self.assertEqual((revoked.status_code, revoked.json()["reason"]), (401, "INVALID_TOKEN"))

    def test_subject_mismatch_does_not_reach_handler(self) -> None:
        calls: list[dict] = []
        client = TestClient(self._app(calls))
        result = client.post(
            "/interact", json=self._payload(user_id="someone_else"), headers=self._headers()
        )
        self.assertEqual((result.status_code, result.json()["reason"]), (403, "SUBJECT_MISMATCH"))
        self.assertEqual(result.json()["device_action"], None)
        self.assertEqual(calls, [])

    def test_request_id_is_required_and_must_be_canonical_uuid(self) -> None:
        client = TestClient(self._app([]))
        missing = self._payload()
        missing.pop("request_id")
        result = client.post("/interact", json=missing, headers=self._headers())
        self.assertEqual((result.status_code, result.json()["reason"]), (400, "INVALID_REQUEST_ID"))
        invalid = client.post(
            "/interact", json=self._payload(request_id="pending-123"), headers=self._headers()
        )
        self.assertEqual((invalid.status_code, invalid.json()["reason"]), (400, "INVALID_REQUEST_ID"))

    def test_media_type_invalid_json_and_body_limit_fail_closed(self) -> None:
        calls: list[dict] = []
        client = TestClient(self._app(calls))
        media = client.post(
            "/interact", content=b"{}", headers={**self._headers(), "Content-Type": "text/plain"}
        )
        self.assertEqual((media.status_code, media.json()["reason"]), (415, "UNSUPPORTED_MEDIA_TYPE"))
        malformed = client.post(
            "/interact", content=b"{", headers={**self._headers(), "Content-Type": "application/json"}
        )
        self.assertEqual((malformed.status_code, malformed.json()["reason"]), (400, "INVALID_JSON"))
        oversized = client.post(
            "/interact",
            json=self._payload(text="x" * (70 * 1024)),
            headers=self._headers(),
        )
        self.assertEqual((oversized.status_code, oversized.json()["reason"]), (413, "REQUEST_TOO_LARGE"))
        self.assertEqual(calls, [])

    def test_replay_and_request_id_reuse_are_never_reexecuted(self) -> None:
        calls: list[dict] = []
        client = TestClient(self._app(calls))
        payload = self._payload()
        first = client.post("/interact", json=payload, headers=self._headers())
        self.assertEqual(first.status_code, 200)
        self.assertIsNotNone(first.json()["device_action"])
        replay = client.post("/interact", json=payload, headers=self._headers())
        self.assertEqual((replay.status_code, replay.json()["reason"]), (409, "IDEMPOTENT_REPLAY"))
        self.assertIsNone(replay.json()["device_action"])
        changed = client.post(
            "/interact", json={**payload, "text": "打开设置"}, headers=self._headers()
        )
        self.assertEqual((changed.status_code, changed.json()["reason"]), (409, "REQUEST_ID_REUSE"))
        self.assertIsNone(changed.json()["device_action"])
        cross_path = client.post("/other", json=payload, headers=self._headers())
        self.assertEqual((cross_path.status_code, cross_path.json()["reason"]), (409, "REQUEST_ID_REUSE"))
        self.assertIsNone(cross_path.json()["device_action"])
        self.assertEqual(len(calls), 1)

    def test_persistent_ledger_blocks_replay_after_middleware_reconstruction(self) -> None:
        payload = self._payload()
        first_calls: list[dict] = []
        with TestClient(self._app(first_calls)) as first_client:
            self.assertEqual(
                first_client.post("/interact", json=payload, headers=self._headers()).status_code,
                200,
            )
        second_calls: list[dict] = []
        with TestClient(self._app(second_calls)) as second_client:
            replay = second_client.post("/interact", json=payload, headers=self._headers())
        self.assertEqual((replay.status_code, replay.json()["reason"]), (409, "IDEMPOTENT_REPLAY"))
        self.assertIsNone(replay.json()["device_action"])
        self.assertEqual(first_calls and len(first_calls), 1)
        self.assertEqual(second_calls, [])


if __name__ == "__main__":
    unittest.main(verbosity=2)
