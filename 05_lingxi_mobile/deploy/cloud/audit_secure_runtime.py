"""Black-box secure-local HTTP audit.  Never prints the bearer token."""
from __future__ import annotations

import argparse
import json
import os
import urllib.error
import urllib.request
import uuid
from typing import Any


def _call(
    base_url: str,
    method: str,
    path: str,
    *,
    token: str | None = None,
    payload: dict[str, Any] | None = None,
    raw_body: bytes | None = None,
    content_type: str = "application/json",
) -> tuple[int, dict[str, Any]]:
    headers = {"Accept": "application/json"}
    if token:
        headers["Authorization"] = f"Bearer {token}"
    body = raw_body
    if payload is not None:
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    if body is not None:
        headers["Content-Type"] = content_type
    request = urllib.request.Request(base_url + path, data=body, headers=headers, method=method)
    try:
        response = urllib.request.urlopen(request, timeout=15)
        status = response.status
        raw = response.read()
    except urllib.error.HTTPError as exc:
        status = exc.code
        raw = exc.read()
    return status, json.loads(raw.decode("utf-8"))


def _assert(name: str, condition: bool, detail: object, passed: list[str]) -> None:
    if not condition:
        raise AssertionError(f"{name}: {detail}")
    passed.append(name)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", default="http://127.0.0.1:8000")
    parser.add_argument("--mode", choices=("full", "prime-restart", "verify-restart"), default="full")
    args = parser.parse_args()
    token = os.environ.get("LINGXI_AUDIT_DEVICE_TOKEN", "")
    replay_id = os.environ.get("LINGXI_AUDIT_REPLAY_ID", "")
    if not token:
        raise SystemExit("LINGXI_AUDIT_DEVICE_TOKEN is required")
    if args.mode != "full":
        try:
            replay_id = str(uuid.UUID(replay_id))
        except (ValueError, AttributeError) as exc:
            raise SystemExit("LINGXI_AUDIT_REPLAY_ID must be a canonical UUID") from exc

    passed: list[str] = []
    if args.mode == "full":
        status, data = _call(args.base_url, "GET", "/live")
        _assert("public_minimal_liveness", status == 200 and data == {"status": "alive"}, data, passed)
        status, data = _call(args.base_url, "GET", "/health")
        _assert("health_requires_auth", status == 401 and data.get("device_action") is None, data, passed)
        status, data = _call(args.base_url, "GET", "/health", token="x" * 48)
        _assert("wrong_token", status == 401 and data.get("reason") == "INVALID_TOKEN", data, passed)
        status, data = _call(args.base_url, "GET", "/ready", token=token)
        _assert("authenticated_readiness", status == 200 and data.get("status") == "ready", data, passed)
        status, data = _call(args.base_url, "GET", "/docs", token=token)
        _assert("production_docs_closed", status == 404, data, passed)
        status, data = _call(
            args.base_url,
            "POST",
            "/interact",
            token=token,
            payload={"request_id": str(uuid.uuid4()), "user_id": "other", "text": "blocked"},
        )
        _assert("subject_mismatch", status == 403 and data.get("device_action") is None, data, passed)
        status, data = _call(
            args.base_url,
            "POST",
            "/interact",
            token=token,
            payload={"user_id": "android", "text": "blocked"},
        )
        _assert("request_id_required", status == 400 and data.get("device_action") is None, data, passed)
        status, data = _call(
            args.base_url,
            "POST",
            "/interact",
            token=token,
            raw_body=b"{}",
            content_type="text/plain",
        )
        _assert("json_media_required", status == 415 and data.get("device_action") is None, data, passed)
        status, data = _call(
            args.base_url,
            "POST",
            "/interact",
            token=token,
            payload={
                "request_id": str(uuid.uuid4()),
                "user_id": "android",
                "text": "x" * (8 * 1024 * 1024),
            },
        )
        _assert("body_limit", status == 413 and data.get("device_action") is None, data, passed)
        request_id = str(uuid.uuid4())
        action = {"request_id": request_id, "user_id": "android", "text": "\u6253\u5f00\u76f8\u673a"}
        status, data = _call(args.base_url, "POST", "/interact", token=token, payload=action)
        _assert(
            "first_action_once",
            status == 200 and (data.get("device_action") or {}).get("type") == "open_camera",
            data,
            passed,
        )
        status, data = _call(args.base_url, "POST", "/interact", token=token, payload=action)
        _assert(
            "immediate_replay_zero_action",
            status == 409
            and data.get("reason") == "IDEMPOTENT_REPLAY"
            and data.get("device_action") is None,
            data,
            passed,
        )
    else:
        action = {"request_id": replay_id, "user_id": "android", "text": "\u6253\u5f00\u76f8\u673a"}
        status, data = _call(args.base_url, "POST", "/interact", token=token, payload=action)
        if args.mode == "prime-restart":
            _assert(
                "restart_probe_primed",
                status == 200 and (data.get("device_action") or {}).get("type") == "open_camera",
                data,
                passed,
            )
        else:
            _assert(
                "restart_replay_zero_action",
                status == 409
                and data.get("reason") == "IDEMPOTENT_REPLAY"
                and data.get("device_action") is None,
                data,
                passed,
            )

    print(json.dumps({"status": "PASS", "mode": args.mode, "passed": len(passed), "checks": passed}, indent=2))


if __name__ == "__main__":
    main()
