"""Cloud container entrypoint with explicit local, secure-local and Railway stages."""
from __future__ import annotations

import base64
import binascii
import json
import os
import re
import sys
from ipaddress import ip_address
from pathlib import Path
from urllib.parse import urlparse


SECRET_NAMES = (
    "DASHSCOPE_API_KEY",
    "LINGXI_VLM_API_KEY",
    "QWEATHER_KEY",
    "AMAP_KEY",
)
SECURE_STAGES = {"secure-local", "railway"}
RAILWAY_DATA_ROOT_TEXT = "/app/brain/data"
RAILWAY_AUTH_FILE_TEXT = "/app/brain/data/security/device_auth_registry.json"
RAILWAY_IDEMPOTENCY_DB_TEXT = "/app/brain/data/security/idempotency.sqlite3"
RAILWAY_PROFILE_DIR_TEXT = "/app/brain/data/profiles"
RAILWAY_DATA_ROOT = Path(RAILWAY_DATA_ROOT_TEXT)
RAILWAY_AUTH_FILE = Path(RAILWAY_AUTH_FILE_TEXT)
RAILWAY_PROFILE_DIR = Path(RAILWAY_PROFILE_DIR_TEXT)
RAILWAY_APPROVAL = "railway-cloud5-vlm-disabled"
RAILWAY_LLM_TIMEOUT_SECONDS = "4.5"
RAILWAY_AGENT_LLM_TIMEOUT_SECONDS = "12.0"
RAILWAY_TIMEZONE = "Asia/Shanghai"
_SUBJECT_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.:-]{0,63}$")
_DEVICE_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.:-]{0,95}$")
_HEX_SHA256_RE = re.compile(r"^[0-9a-f]{64}$")


def _load_file_secret(name: str) -> None:
    if os.environ.get(name, "").strip():
        return
    secret_file = os.environ.get(f"{name}_FILE", "").strip()
    if not secret_file:
        return
    path = Path(secret_file)
    if not path.is_file():
        raise SystemExit(f"secret file missing for {name}: {path}")
    value = path.read_text(encoding="utf-8").strip()
    if not value:
        raise SystemExit(f"secret file empty for {name}: {path}")
    os.environ[name] = value


def _self_hosted_vlm_url_is_safe(raw_url: str) -> bool:
    """Allow HTTPS anywhere; allow HTTP only on a loopback/private development hop."""
    try:
        parsed = urlparse(raw_url)
        host = (parsed.hostname or "").lower()
        if parsed.scheme == "https" and host:
            return True
        if parsed.scheme != "http" or not host:
            return False
        if host in {"127.0.0.1", "localhost", "host.docker.internal"}:
            return True
        return ip_address(host).is_private
    except ValueError:
        return False


def _parse_port(raw: str) -> int:
    try:
        port = int(raw)
    except ValueError as exc:
        raise SystemExit("PORT must be an integer") from exc
    if not 1 <= port <= 65535:
        raise SystemExit("PORT must be between 1 and 65535")
    return port


def _decode_device_registry(raw_b64: str) -> bytes:
    """Decode and validate the hash-only registry before writing it to the volume."""
    try:
        decoded = base64.b64decode(raw_b64, validate=True)
    except (binascii.Error, ValueError) as exc:
        raise SystemExit("Railway device auth registry is not valid base64") from exc
    if not decoded or len(decoded) > 64 * 1024:
        raise SystemExit("Railway device auth registry size is invalid")
    try:
        data = json.loads(decoded.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise SystemExit("Railway device auth registry is not valid UTF-8 JSON") from exc
    devices = data.get("devices") if isinstance(data, dict) else None
    if (
        not isinstance(data, dict)
        or data.get("schema_version") != "lingxi-device-auth.1"
        or not isinstance(devices, list)
    ):
        raise SystemExit("Railway device auth registry schema is invalid")
    seen_hashes: set[str] = set()
    active = 0
    for item in devices:
        if not isinstance(item, dict):
            raise SystemExit("Railway device auth registry contains a non-object entry")
        subject = item.get("subject")
        device_id = item.get("device_id")
        token_hash = item.get("token_sha256")
        enabled = item.get("enabled")
        if (
            not isinstance(subject, str)
            or not _SUBJECT_RE.fullmatch(subject)
            or not isinstance(device_id, str)
            or not _DEVICE_RE.fullmatch(device_id)
            or not isinstance(token_hash, str)
            or not _HEX_SHA256_RE.fullmatch(token_hash)
            or not isinstance(enabled, bool)
        ):
            raise SystemExit("Railway device auth registry contains an invalid entry")
        if token_hash in seen_hashes:
            raise SystemExit("Railway device auth registry contains a duplicate token hash")
        seen_hashes.add(token_hash)
        active += int(enabled)
    if active == 0:
        raise SystemExit("Railway device auth registry has no active credential")
    return json.dumps(data, ensure_ascii=False, indent=2).encode("utf-8") + b"\n"


def _validate_railway_environment() -> int:
    for name in (
        "RAILWAY_PROJECT_ID",
        "RAILWAY_ENVIRONMENT_ID",
        "RAILWAY_SERVICE_ID",
        "RAILWAY_DEPLOYMENT_ID",
    ):
        if not os.environ.get(name, "").strip():
            raise SystemExit(f"Railway platform marker missing: {name}")
    if os.environ.get("LINGXI_PUBLIC_DEPLOYMENT_APPROVED", "").strip() != RAILWAY_APPROVAL:
        raise SystemExit("Railway public deployment approval is missing or invalid")
    if os.environ.get("RAILWAY_VOLUME_MOUNT_PATH", "").strip() != RAILWAY_DATA_ROOT_TEXT:
        raise SystemExit("Railway volume must be mounted at /app/brain/data")
    if os.environ.get("RAILWAY_RUN_UID", "").strip() != "0":
        raise SystemExit("Railway must start entrypoint as root so it can repair volume ownership")
    exact_paths = {
        "LINGXI_DEVICE_AUTH_FILE": RAILWAY_AUTH_FILE_TEXT,
        "LINGXI_IDEMPOTENCY_DB": RAILWAY_IDEMPOTENCY_DB_TEXT,
        "LINGXI_MOBILE_PROFILE_DIR": RAILWAY_PROFILE_DIR_TEXT,
    }
    for name, expected in exact_paths.items():
        if os.environ.get(name, "").strip() != expected:
            raise SystemExit(f"Railway requires {name}={expected}")
    if os.environ.get("LINGXI_CLOUD_VLM_ENABLED", "").strip().lower() != "false":
        raise SystemExit("Railway Cloud-5 pilot is VLM-disabled and cannot enable vision")
    if os.environ.get("LINGXI_LLM_REQUEST_TIMEOUT_SECONDS", "").strip() != RAILWAY_LLM_TIMEOUT_SECONDS:
        raise SystemExit(
            "Railway requires LINGXI_LLM_REQUEST_TIMEOUT_SECONDS=4.5 to preserve the response deadline"
        )
    if (
        os.environ.get("LINGXI_AGENT_LLM_REQUEST_TIMEOUT_SECONDS", "").strip()
        != RAILWAY_AGENT_LLM_TIMEOUT_SECONDS
    ):
        raise SystemExit(
            "Railway requires LINGXI_AGENT_LLM_REQUEST_TIMEOUT_SECONDS=12.0 for Agent-only calls"
        )
    if os.environ.get("LINGXI_TIMEZONE", "").strip() != RAILWAY_TIMEZONE:
        raise SystemExit("Railway requires LINGXI_TIMEZONE=Asia/Shanghai")
    for name in ("DASHSCOPE_API_KEY", "QWEATHER_KEY", "AMAP_KEY"):
        if not os.environ.get(name, "").strip():
            raise SystemExit(f"Railway required secret missing: {name}")
    return _parse_port(os.environ.get("PORT", ""))


def _prepare_railway_volume() -> None:
    if os.name != "posix" or not hasattr(os, "geteuid") or os.geteuid() != 0:
        raise SystemExit("Railway entrypoint must begin as root and drop privileges before serving")
    RAILWAY_DATA_ROOT.mkdir(parents=True, exist_ok=True)
    if RAILWAY_DATA_ROOT.is_symlink() or RAILWAY_DATA_ROOT.resolve() != RAILWAY_DATA_ROOT:
        raise SystemExit("Railway data root must be a real /app/brain/data directory")
    RAILWAY_AUTH_FILE.parent.mkdir(parents=True, exist_ok=True)
    RAILWAY_PROFILE_DIR.mkdir(parents=True, exist_ok=True)

    raw_registry = os.environ.pop("LINGXI_DEVICE_AUTH_REGISTRY_B64", "").strip()
    if raw_registry:
        payload = _decode_device_registry(raw_registry)
        temp_path = RAILWAY_AUTH_FILE.with_name(
            f".{RAILWAY_AUTH_FILE.name}.{os.getpid()}.tmp"
        )
        try:
            with temp_path.open("xb") as handle:
                handle.write(payload)
                handle.flush()
                os.fsync(handle.fileno())
            os.chmod(temp_path, 0o600)
            os.replace(temp_path, RAILWAY_AUTH_FILE)
        finally:
            if temp_path.exists():
                temp_path.unlink()
    if not RAILWAY_AUTH_FILE.is_file():
        raise SystemExit(
            "Railway device auth registry is missing; provide the hash-only base64 seed"
        )

    import pwd

    account = pwd.getpwnam("lingxi")
    if account.pw_uid != 999:
        raise SystemExit("container lingxi UID changed; refusing Railway startup")
    for path in (RAILWAY_DATA_ROOT, *RAILWAY_DATA_ROOT.rglob("*")):
        if path.is_symlink():
            raise SystemExit(f"Railway data volume contains a forbidden symlink: {path}")
        os.chown(path, account.pw_uid, account.pw_gid)
    os.initgroups(account.pw_name, account.pw_gid)
    os.setgid(account.pw_gid)
    os.setuid(account.pw_uid)
    if os.geteuid() != 999:
        raise SystemExit("Railway privilege drop failed")


def main() -> None:
    stage = os.environ.get("LINGXI_DEPLOYMENT_STAGE", "local-container").strip()
    security_mode = os.environ.get("LINGXI_SECURITY_MODE", "local-open").strip()
    if stage not in {"local-container", "secure-local", "railway"}:
        raise SystemExit(
            "deployment stage is not approved by the Lingxi cloud contract"
        )
    if stage == "local-container" and security_mode != "local-open":
        raise SystemExit("local-container requires LINGXI_SECURITY_MODE=local-open")
    if stage in SECURE_STAGES and security_mode != "required":
        raise SystemExit(f"{stage} requires LINGXI_SECURITY_MODE=required")

    workers = os.environ.get("LINGXI_WORKERS", "1").strip()
    if workers != "1":
        raise SystemExit(
            "pending state is process-local; Cloud-1 requires exactly one worker"
        )

    for name in SECRET_NAMES:
        _load_file_secret(name)

    port = 8000
    if stage == "railway":
        port = _validate_railway_environment()
        _prepare_railway_volume()

    if stage in SECURE_STAGES:
        auth_file = Path(os.environ.get("LINGXI_DEVICE_AUTH_FILE", ""))
        if not auth_file.is_file():
            raise SystemExit(f"{stage} device auth registry is missing")
        vlm_flag = os.environ.get("LINGXI_CLOUD_VLM_ENABLED", "false").strip().lower()
        if vlm_flag not in {"true", "false"}:
            raise SystemExit("LINGXI_CLOUD_VLM_ENABLED must be true or false")
        if vlm_flag == "true":
            if os.environ.get("LINGXI_VLM_MODEL", "").strip() != "qwen2.5-vl-3b-instruct":
                raise SystemExit(f"{stage} requires exact qwen2.5-vl-3b-instruct")
            vlm_url = os.environ.get("LINGXI_VLM_BASE_URL", "").strip()
            if not _self_hosted_vlm_url_is_safe(vlm_url):
                raise SystemExit(
                    f"{stage} VLM requires HTTPS, or HTTP on a loopback/private self-hosted endpoint"
                )
            if not os.environ.get("LINGXI_VLM_API_KEY", "").strip():
                raise SystemExit(f"{stage} enabled VLM requires LINGXI_VLM_API_KEY")
        for required_secret in ("DASHSCOPE_API_KEY", "QWEATHER_KEY"):
            if not os.environ.get(required_secret, "").strip():
                raise SystemExit(f"{stage} required secret missing: {required_secret}")

    args = [
        sys.executable,
        "-m",
        "uvicorn",
        "server:app",
        "--app-dir",
        "/app/brain",
        "--host",
        "0.0.0.0",
        "--port",
        str(port),
        "--workers",
        "1",
        "--no-server-header",
    ]
    if stage in SECURE_STAGES:
        args.append("--no-access-log")
    os.execv(sys.executable, args)


if __name__ == "__main__":
    main()
