"""Issue, rotate, revoke, or list Lingxi device credentials.

The raw bearer token is printed exactly when issued and is never written to the
registry.  The registry contains only SHA-256 digests and non-sensitive metadata.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import secrets
import tempfile
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


AUTH_SCHEMA = "lingxi-device-auth.1"


def _now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def _load(path: Path) -> dict[str, Any]:
    if not path.exists():
        return {"schema_version": AUTH_SCHEMA, "devices": []}
    data = json.loads(path.read_text(encoding="utf-8"))
    if data.get("schema_version") != AUTH_SCHEMA or not isinstance(data.get("devices"), list):
        raise SystemExit("设备凭据文件格式错误，已停止且未改写。")
    return data


def _save(path: Path, data: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    handle = tempfile.NamedTemporaryFile(
        mode="w", encoding="utf-8", dir=path.parent, prefix=path.name + ".", delete=False
    )
    temp_path = Path(handle.name)
    try:
        with handle:
            json.dump(data, handle, ensure_ascii=False, indent=2)
            handle.write("\n")
            handle.flush()
            os.fsync(handle.fileno())
        os.chmod(temp_path, 0o600)
        os.replace(temp_path, path)
    finally:
        if temp_path.exists():
            temp_path.unlink()


def _issue(data: dict[str, Any], subject: str, device_id: str) -> str:
    token = "lx1_" + secrets.token_urlsafe(32)
    data["devices"].append(
        {
            "credential_id": str(uuid.uuid4()),
            "subject": subject,
            "device_id": device_id,
            "token_sha256": hashlib.sha256(token.encode("ascii")).hexdigest(),
            "enabled": True,
            "created_at": _now(),
        }
    )
    return token


def main() -> None:
    parser = argparse.ArgumentParser(description="管理灵犀设备Bearer凭据（服务端只保存哈希）")
    parser.add_argument("--file", required=True, type=Path, help="设备凭据JSON路径")
    sub = parser.add_subparsers(dest="command", required=True)
    for name in ("issue", "rotate", "revoke"):
        cmd = sub.add_parser(name)
        cmd.add_argument("--subject", required=True)
        cmd.add_argument("--device-id", required=True)
    sub.add_parser("list")
    args = parser.parse_args()

    path = args.file.expanduser().resolve()
    data = _load(path)
    if args.command == "list":
        rows = [
            {
                "credential_id": item.get("credential_id"),
                "subject": item.get("subject"),
                "device_id": item.get("device_id"),
                "enabled": item.get("enabled"),
                "created_at": item.get("created_at"),
                "revoked_at": item.get("revoked_at"),
                "token_hash_prefix": str(item.get("token_sha256", ""))[:12],
            }
            for item in data["devices"]
        ]
        print(json.dumps({"schema_version": AUTH_SCHEMA, "devices": rows}, ensure_ascii=False, indent=2))
        return

    subject = args.subject.strip()
    device_id = args.device_id.strip()
    if not subject or not device_id:
        raise SystemExit("subject和device-id不能为空。")
    matching = [
        item
        for item in data["devices"]
        if item.get("subject") == subject and item.get("device_id") == device_id
    ]

    if args.command == "issue":
        if any(item.get("enabled") is True for item in matching):
            raise SystemExit("该subject/device已有启用凭据；如需更换请使用rotate。")
        token = _issue(data, subject, device_id)
        _save(path, data)
        print(json.dumps({"status": "issued", "subject": subject, "device_id": device_id, "token": token}, ensure_ascii=False))
        return

    changed = 0
    for item in matching:
        if item.get("enabled") is True:
            item["enabled"] = False
            item["revoked_at"] = _now()
            changed += 1
    if args.command == "revoke":
        if not changed:
            raise SystemExit("没有找到启用中的匹配凭据，未改写。")
        _save(path, data)
        print(json.dumps({"status": "revoked", "subject": subject, "device_id": device_id, "count": changed}, ensure_ascii=False))
        return

    token = _issue(data, subject, device_id)
    _save(path, data)
    print(
        json.dumps(
            {
                "status": "rotated",
                "subject": subject,
                "device_id": device_id,
                "revoked": changed,
                "token": token,
            },
            ensure_ascii=False,
        )
    )


if __name__ == "__main__":
    main()
