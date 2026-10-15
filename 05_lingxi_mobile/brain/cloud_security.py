"""Cloud-2 HTTP security boundary for the Lingxi Brain.

The mobile client owns device actions, so a duplicated HTTP response containing a
``device_action`` is itself dangerous.  This module therefore authenticates every
business request, binds credentials to a fixed subject, and permanently consumes
each subject/path/request_id before the response is released to the client.

Only token hashes are stored server-side.  Request bodies and responses are never
persisted by the idempotency store.
"""
from __future__ import annotations

import hashlib
import hmac
import json
import logging
import os
import re
import sqlite3
import threading
import time
import uuid
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Awaitable, Callable


LOGGER = logging.getLogger("lingxi.cloud_security")
AUTH_SCHEMA = "lingxi-device-auth.1"
SECURITY_MODES = {"local-open", "required"}
PUBLIC_LIVENESS_PATH = "/live"
JSON_BODY_METHODS = {"POST", "PUT", "PATCH"}
_SUBJECT_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.:-]{0,63}$")
_DEVICE_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.:-]{0,95}$")
_HEX_SHA256_RE = re.compile(r"^[0-9a-f]{64}$")


@dataclass(frozen=True)
class SecurityConfig:
    mode: str
    auth_file: Path | None
    idempotency_db: Path
    max_request_bytes: int = 8 * 1024 * 1024

    @classmethod
    def from_env(cls, project_root: Path) -> "SecurityConfig":
        mode = os.environ.get("LINGXI_SECURITY_MODE", "local-open").strip().lower()
        if mode not in SECURITY_MODES:
            raise RuntimeError(
                f"invalid LINGXI_SECURITY_MODE={mode!r}; expected one of {sorted(SECURITY_MODES)}"
            )
        raw_auth = os.environ.get("LINGXI_DEVICE_AUTH_FILE", "").strip()
        raw_db = os.environ.get("LINGXI_IDEMPOTENCY_DB", "").strip()
        raw_limit = os.environ.get("LINGXI_MAX_REQUEST_BYTES", str(8 * 1024 * 1024)).strip()
        try:
            max_bytes = int(raw_limit)
        except ValueError as exc:
            raise RuntimeError("LINGXI_MAX_REQUEST_BYTES must be an integer") from exc
        if not 64 * 1024 <= max_bytes <= 16 * 1024 * 1024:
            raise RuntimeError("LINGXI_MAX_REQUEST_BYTES must be between 64 KiB and 16 MiB")
        return cls(
            mode=mode,
            auth_file=Path(raw_auth).expanduser().resolve() if raw_auth else None,
            idempotency_db=(
                Path(raw_db).expanduser().resolve()
                if raw_db
                else (project_root / "brain" / "data" / "security" / "idempotency.sqlite3").resolve()
            ),
            max_request_bytes=max_bytes,
        )


@dataclass(frozen=True)
class DevicePrincipal:
    subject: str
    device_id: str


class AuthConfigurationError(RuntimeError):
    pass


class AuthenticationRegistry:
    """Small, reload-on-request registry so revocation does not require a restart."""

    def __init__(self, path: Path | None):
        self.path = path

    def _active_entries(self) -> list[dict[str, str]]:
        if self.path is None or not self.path.is_file():
            raise AuthConfigurationError("device auth registry is missing")
        try:
            raw = json.loads(self.path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            raise AuthConfigurationError("device auth registry cannot be read") from exc
        if raw.get("schema_version") != AUTH_SCHEMA or not isinstance(raw.get("devices"), list):
            raise AuthConfigurationError("device auth registry schema is invalid")

        active: list[dict[str, str]] = []
        seen_hashes: set[str] = set()
        for item in raw["devices"]:
            if not isinstance(item, dict):
                raise AuthConfigurationError("device auth registry contains a non-object entry")
            subject = str(item.get("subject", ""))
            device_id = str(item.get("device_id", ""))
            token_hash = str(item.get("token_sha256", "")).lower()
            enabled = item.get("enabled")
            if (
                not _SUBJECT_RE.fullmatch(subject)
                or not _DEVICE_RE.fullmatch(device_id)
                or not _HEX_SHA256_RE.fullmatch(token_hash)
                or not isinstance(enabled, bool)
            ):
                raise AuthConfigurationError("device auth registry contains an invalid entry")
            if token_hash in seen_hashes:
                raise AuthConfigurationError("device auth registry contains a duplicate token hash")
            seen_hashes.add(token_hash)
            if enabled:
                active.append(
                    {"subject": subject, "device_id": device_id, "token_sha256": token_hash}
                )
        if not active:
            raise AuthConfigurationError("device auth registry has no active credentials")
        return active

    def validate(self) -> None:
        self._active_entries()

    def authenticate(self, token: str) -> DevicePrincipal | None:
        if len(token) < 32 or len(token) > 256 or not token.isascii():
            return None
        candidate = hashlib.sha256(token.encode("ascii")).hexdigest()
        match: DevicePrincipal | None = None
        # Compare every active digest so the first matching entry is not observable.
        for item in self._active_entries():
            if hmac.compare_digest(candidate, item["token_sha256"]):
                match = DevicePrincipal(item["subject"], item["device_id"])
        return match


class IdempotencyStore:
    """Persistent consume-once ledger; stores only opaque metadata and body hashes."""

    def __init__(self, path: Path):
        self.path = path
        self._init_lock = threading.Lock()
        self._initialized = False

    def _connect(self) -> sqlite3.Connection:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        conn = sqlite3.connect(str(self.path), timeout=5.0)
        conn.execute("PRAGMA journal_mode=WAL")
        conn.execute("PRAGMA synchronous=FULL")
        return conn

    def _ensure_schema(self) -> None:
        if self._initialized:
            return
        with self._init_lock:
            if self._initialized:
                return
            with self._connect() as conn:
                conn.execute(
                    """
                    CREATE TABLE IF NOT EXISTS consumed_requests (
                        subject TEXT NOT NULL,
                        path TEXT NOT NULL,
                        request_id TEXT NOT NULL,
                        request_hash TEXT NOT NULL,
                        state TEXT NOT NULL,
                        response_status INTEGER,
                        created_at REAL NOT NULL,
                        completed_at REAL,
                        PRIMARY KEY(subject, request_id)
                    )
                    """
                )
            self._initialized = True

    def probe(self) -> None:
        self._ensure_schema()
        with self._connect() as conn:
            conn.execute("SELECT 1").fetchone()

    def claim(self, subject: str, path: str, request_id: str, request_hash: str) -> str:
        """Return NEW, REPLAY, or REUSE.  Existing rows are never re-executed."""
        self._ensure_schema()
        conn = self._connect()
        try:
            conn.execute("BEGIN IMMEDIATE")
            row = conn.execute(
                "SELECT path,request_hash FROM consumed_requests "
                "WHERE subject=? AND request_id=?",
                (subject, request_id),
            ).fetchone()
            if row is None:
                conn.execute(
                    "INSERT INTO consumed_requests "
                    "(subject,path,request_id,request_hash,state,created_at) "
                    "VALUES (?,?,?,?,?,?)",
                    (subject, path, request_id, request_hash, "IN_FLIGHT", time.time()),
                )
                conn.commit()
                return "NEW"
            conn.commit()
            same_path = hmac.compare_digest(str(row[0]), path)
            same_body = hmac.compare_digest(str(row[1]), request_hash)
            return "REPLAY" if same_path and same_body else "REUSE"
        finally:
            conn.close()

    def complete(self, subject: str, path: str, request_id: str, status: int) -> None:
        self._ensure_schema()
        with self._connect() as conn:
            conn.execute(
                "UPDATE consumed_requests SET state=?,response_status=?,completed_at=? "
                "WHERE subject=? AND path=? AND request_id=?",
                ("COMPLETE", status, time.time(), subject, path, request_id),
            )


def _canonical_uuid(value: Any) -> str | None:
    if not isinstance(value, str):
        return None
    try:
        parsed = uuid.UUID(value)
    except (ValueError, AttributeError):
        return None
    canonical = str(parsed)
    return canonical if value.lower() == canonical else None


def _headers(scope: dict[str, Any], name: bytes) -> list[str]:
    return [
        value.decode("latin-1")
        for key, value in scope.get("headers", [])
        if key.lower() == name
    ]


def _json_bytes(payload: dict[str, Any]) -> bytes:
    return json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8")


async def _send_json(
    send: Callable[[dict[str, Any]], Awaitable[None]],
    status: int,
    reason: str,
    message: str,
    *,
    request_id: str | None = None,
    authenticate: bool = False,
) -> None:
    payload = {
        "status": "refused",
        "reason": reason,
        "message": message,
        "device_action": None,
    }
    if request_id:
        payload["request_id"] = request_id
    body = _json_bytes(payload)
    headers = [
        (b"content-type", b"application/json; charset=utf-8"),
        (b"content-length", str(len(body)).encode("ascii")),
        (b"cache-control", b"no-store"),
        (b"x-content-type-options", b"nosniff"),
    ]
    if authenticate:
        headers.append((b"www-authenticate", b"Bearer"))
    await send({"type": "http.response.start", "status": status, "headers": headers})
    await send({"type": "http.response.body", "body": body, "more_body": False})


async def _read_limited_body(
    receive: Callable[[], Awaitable[dict[str, Any]]], max_bytes: int
) -> tuple[bytes | None, str | None]:
    chunks: list[bytes] = []
    total = 0
    while True:
        message = await receive()
        if message["type"] == "http.disconnect":
            return None, "CLIENT_DISCONNECTED"
        if message["type"] != "http.request":
            continue
        chunk = message.get("body", b"")
        total += len(chunk)
        if total > max_bytes:
            return None, "REQUEST_TOO_LARGE"
        if chunk:
            chunks.append(chunk)
        if not message.get("more_body", False):
            return b"".join(chunks), None


def _replay_receive(body: bytes) -> Callable[[], Awaitable[dict[str, Any]]]:
    delivered = False

    async def receive() -> dict[str, Any]:
        nonlocal delivered
        if not delivered:
            delivered = True
            return {"type": "http.request", "body": body, "more_body": False}
        return {"type": "http.request", "body": b"", "more_body": False}

    return receive


class CloudSecurityMiddleware:
    """Pure ASGI middleware so body limits apply before FastAPI parses JSON."""

    def __init__(self, app: Any, config: SecurityConfig):
        self.app = app
        self.config = config
        self.registry = AuthenticationRegistry(config.auth_file)
        self.idempotency = IdempotencyStore(config.idempotency_db)

    async def __call__(
        self,
        scope: dict[str, Any],
        receive: Callable[[], Awaitable[dict[str, Any]]],
        send: Callable[[dict[str, Any]], Awaitable[None]],
    ) -> None:
        if scope.get("type") != "http" or self.config.mode != "required":
            await self.app(scope, receive, send)
            return

        method = str(scope.get("method", "GET")).upper()
        path = str(scope.get("path", ""))
        if method == "GET" and path == PUBLIC_LIVENESS_PATH:
            async def liveness_send(message: dict[str, Any]) -> None:
                if message.get("type") == "http.response.start":
                    headers = list(message.get("headers", []))
                    headers.extend(
                        [(b"cache-control", b"no-store"), (b"x-content-type-options", b"nosniff")]
                    )
                    message["headers"] = headers
                await send(message)

            await self.app(scope, receive, liveness_send)
            return

        auth_values = _headers(scope, b"authorization")
        auth_parts = auth_values[0].split() if len(auth_values) == 1 else []
        if len(auth_parts) != 2 or auth_parts[0].lower() != "bearer":
            await _send_json(send, 401, "AUTH_REQUIRED", "需要设备Bearer身份。", authenticate=True)
            return
        token = auth_parts[1]
        try:
            principal = self.registry.authenticate(token)
        except AuthConfigurationError:
            await _send_json(
                send, 503, "AUTH_CONFIGURATION_UNAVAILABLE", "设备认证配置不可用。"
            )
            return
        if principal is None:
            await _send_json(send, 401, "INVALID_TOKEN", "设备身份无效或已吊销。", authenticate=True)
            return

        scope.setdefault("state", {})["lingxi_subject"] = principal.subject
        scope["state"]["lingxi_device_id"] = principal.device_id

        if method not in JSON_BODY_METHODS:
            response_status = 500
            started = time.perf_counter()

            async def authenticated_send(message: dict[str, Any]) -> None:
                nonlocal response_status
                if message.get("type") == "http.response.start":
                    response_status = int(message.get("status", 500))
                    headers = list(message.get("headers", []))
                    headers.extend(
                        [(b"cache-control", b"no-store"), (b"x-content-type-options", b"nosniff")]
                    )
                    message["headers"] = headers
                await send(message)

            await self.app(scope, receive, authenticated_send)
            LOGGER.info(
                json.dumps(
                    {
                        "event": "cloud_request",
                        "method": method,
                        "path": path,
                        "status": response_status,
                        "subject_ref": hashlib.sha256(principal.subject.encode("utf-8")).hexdigest()[:12],
                        "device_ref": hashlib.sha256(principal.device_id.encode("utf-8")).hexdigest()[:12],
                        "latency_ms": round((time.perf_counter() - started) * 1000, 2),
                    },
                    ensure_ascii=False,
                    separators=(",", ":"),
                )
            )
            return

        content_types = _headers(scope, b"content-type")
        media_type = content_types[0].split(";", 1)[0].strip().lower() if len(content_types) == 1 else ""
        if media_type != "application/json":
            await _send_json(send, 415, "UNSUPPORTED_MEDIA_TYPE", "业务请求只接受application/json。")
            return
        content_lengths = _headers(scope, b"content-length")
        if len(content_lengths) > 1:
            await _send_json(send, 400, "AMBIGUOUS_CONTENT_LENGTH", "请求长度头不合法。")
            return
        if content_lengths:
            try:
                if int(content_lengths[0]) > self.config.max_request_bytes:
                    await _send_json(send, 413, "REQUEST_TOO_LARGE", "请求体超过服务端限制。")
                    return
            except ValueError:
                await _send_json(send, 400, "INVALID_CONTENT_LENGTH", "请求长度头不合法。")
                return

        body, body_error = await _read_limited_body(receive, self.config.max_request_bytes)
        if body_error:
            status = 413 if body_error == "REQUEST_TOO_LARGE" else 400
            await _send_json(send, status, body_error, "请求体不可用或超过服务端限制。")
            return
        try:
            payload = json.loads((body or b"").decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            await _send_json(send, 400, "INVALID_JSON", "请求体必须是UTF-8 JSON对象。")
            return
        if not isinstance(payload, dict):
            await _send_json(send, 400, "INVALID_JSON_OBJECT", "请求体必须是JSON对象。")
            return

        request_id = _canonical_uuid(payload.get("request_id"))
        if request_id is None:
            await _send_json(send, 400, "INVALID_REQUEST_ID", "业务POST必须携带规范UUID request_id。")
            return
        supplied_subject = payload.get("user_id")
        if path == "/interact" and supplied_subject is None:
            await _send_json(
                send, 403, "SUBJECT_REQUIRED", "交互请求必须显式携带令牌绑定的user_id。", request_id=request_id
            )
            return
        if supplied_subject is not None and supplied_subject != principal.subject:
            await _send_json(
                send, 403, "SUBJECT_MISMATCH", "请求主体与设备令牌不一致。", request_id=request_id
            )
            return

        canonical = json.dumps(payload, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        request_hash = hashlib.sha256(canonical.encode("utf-8")).hexdigest()
        try:
            claim = self.idempotency.claim(principal.subject, path, request_id, request_hash)
        except (OSError, sqlite3.Error):
            await _send_json(
                send, 503, "IDEMPOTENCY_UNAVAILABLE", "幂等存储不可用，已拒绝执行。", request_id=request_id
            )
            return
        if claim != "NEW":
            reason = "IDEMPOTENT_REPLAY" if claim == "REPLAY" else "REQUEST_ID_REUSE"
            message = "该请求已被消费，不会再次执行。" if claim == "REPLAY" else "request_id已绑定其他请求。"
            await _send_json(send, 409, reason, message, request_id=request_id)
            return

        captured: list[dict[str, Any]] = []

        async def capture_send(message: dict[str, Any]) -> None:
            captured.append(message)

        started = time.perf_counter()
        response_status = 500
        try:
            await self.app(scope, _replay_receive(body or b""), capture_send)
            for message in captured:
                if message.get("type") == "http.response.start":
                    response_status = int(message.get("status", 500))
                    headers = list(message.get("headers", []))
                    headers.extend(
                        [
                            (b"cache-control", b"no-store"),
                            (b"x-content-type-options", b"nosniff"),
                            (b"x-lingxi-request-id", request_id.encode("ascii")),
                        ]
                    )
                    message["headers"] = headers
                    break
        finally:
            try:
                self.idempotency.complete(
                    principal.subject, path, request_id, response_status
                )
            except (OSError, sqlite3.Error):
                # Do not release a response that may contain a device action unless
                # its request ID was durably finalized first.
                captured = []
                await _send_json(
                    send,
                    503,
                    "IDEMPOTENCY_COMMIT_FAILED",
                    "请求结果未能安全落盘，已拒绝下发。",
                    request_id=request_id,
                )
                return

        LOGGER.info(
            json.dumps(
                {
                    "event": "cloud_request",
                    "method": method,
                    "path": path,
                    "status": response_status,
                    "request_id": request_id,
                    "subject_ref": hashlib.sha256(principal.subject.encode("utf-8")).hexdigest()[:12],
                    "device_ref": hashlib.sha256(principal.device_id.encode("utf-8")).hexdigest()[:12],
                    "latency_ms": round((time.perf_counter() - started) * 1000, 2),
                },
                ensure_ascii=False,
                separators=(",", ":"),
            )
        )
        for message in captured:
            await send(message)


def readiness(config: SecurityConfig) -> dict[str, Any]:
    """Authenticated readiness detail; public callers only get /live."""
    if config.mode == "required":
        AuthenticationRegistry(config.auth_file).validate()
        IdempotencyStore(config.idempotency_db).probe()
    return {
        "status": "ready",
        "security_mode": config.mode,
        "idempotency": "persistent" if config.mode == "required" else "not_enforced_local",
    }
