"""Offline tests for self-hosted VLM and Railway startup safety boundaries."""
from __future__ import annotations

import base64
import importlib.util
import json
import os
import unittest
from pathlib import Path
from unittest.mock import patch


ENTRYPOINT = Path(__file__).with_name("entrypoint.py")
SPEC = importlib.util.spec_from_file_location("lingxi_cloud_entrypoint", ENTRYPOINT)
assert SPEC is not None and SPEC.loader is not None
entrypoint = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(entrypoint)


class SelfHostedVlmUrlPolicyTest(unittest.TestCase):
    def test_https_endpoint_is_allowed(self) -> None:
        self.assertTrue(entrypoint._self_hosted_vlm_url_is_safe("https://vlm.example/v1"))

    def test_docker_host_http_is_allowed_for_local_development(self) -> None:
        self.assertTrue(
            entrypoint._self_hosted_vlm_url_is_safe(
                "http://host.docker.internal:1234/v1"
            )
        )

    def test_loopback_http_is_allowed_for_local_development(self) -> None:
        self.assertTrue(entrypoint._self_hosted_vlm_url_is_safe("http://127.0.0.1:1234/v1"))

    def test_private_lan_http_is_allowed_for_local_development(self) -> None:
        self.assertTrue(entrypoint._self_hosted_vlm_url_is_safe("http://192.168.3.2:1234/v1"))

    def test_public_http_is_rejected(self) -> None:
        self.assertFalse(entrypoint._self_hosted_vlm_url_is_safe("http://vlm.example/v1"))

    def test_invalid_or_non_http_url_is_rejected(self) -> None:
        self.assertFalse(entrypoint._self_hosted_vlm_url_is_safe("file:///tmp/model"))


class RailwayPolicyTest(unittest.TestCase):
    def _env(self) -> dict[str, str]:
        return {
            "RAILWAY_PROJECT_ID": "project",
            "RAILWAY_ENVIRONMENT_ID": "environment",
            "RAILWAY_SERVICE_ID": "service",
            "RAILWAY_DEPLOYMENT_ID": "deployment",
            "RAILWAY_VOLUME_MOUNT_PATH": "/app/brain/data",
            "RAILWAY_RUN_UID": "0",
            "LINGXI_PUBLIC_DEPLOYMENT_APPROVED": "railway-cloud5-vlm-disabled",
            "LINGXI_DEVICE_AUTH_FILE": "/app/brain/data/security/device_auth_registry.json",
            "LINGXI_IDEMPOTENCY_DB": "/app/brain/data/security/idempotency.sqlite3",
            "LINGXI_MOBILE_PROFILE_DIR": "/app/brain/data/profiles",
            "LINGXI_CLOUD_VLM_ENABLED": "false",
            "LINGXI_LLM_REQUEST_TIMEOUT_SECONDS": "4.5",
            "LINGXI_AGENT_LLM_REQUEST_TIMEOUT_SECONDS": "12.0",
            "LINGXI_TIMEZONE": "Asia/Shanghai",
            "DASHSCOPE_API_KEY": "dashscope-placeholder",
            "QWEATHER_KEY": "qweather-placeholder",
            "AMAP_KEY": "amap-placeholder",
            "PORT": "4321",
        }

    def test_complete_railway_environment_is_allowed(self) -> None:
        with patch.dict(os.environ, self._env(), clear=True):
            self.assertEqual(entrypoint._validate_railway_environment(), 4321)

    def test_missing_platform_marker_is_rejected(self) -> None:
        env = self._env()
        del env["RAILWAY_SERVICE_ID"]
        with patch.dict(os.environ, env, clear=True):
            with self.assertRaises(SystemExit):
                entrypoint._validate_railway_environment()

    def test_missing_explicit_public_approval_is_rejected(self) -> None:
        env = self._env()
        env["LINGXI_PUBLIC_DEPLOYMENT_APPROVED"] = "true"
        with patch.dict(os.environ, env, clear=True):
            with self.assertRaises(SystemExit):
                entrypoint._validate_railway_environment()

    def test_wrong_volume_or_root_uid_is_rejected(self) -> None:
        for name, value in (
            ("RAILWAY_VOLUME_MOUNT_PATH", "/data"),
            ("RAILWAY_RUN_UID", "999"),
        ):
            env = self._env()
            env[name] = value
            with self.subTest(name=name), patch.dict(os.environ, env, clear=True):
                with self.assertRaises(SystemExit):
                    entrypoint._validate_railway_environment()

    def test_vlm_enablement_is_rejected_for_cloud5_pilot(self) -> None:
        env = self._env()
        env["LINGXI_CLOUD_VLM_ENABLED"] = "true"
        with patch.dict(os.environ, env, clear=True):
            with self.assertRaises(SystemExit):
                entrypoint._validate_railway_environment()

    def test_wrong_llm_deadline_is_rejected(self) -> None:
        for value in ("", "3.0", "5.0", "12"):
            env = self._env()
            env["LINGXI_LLM_REQUEST_TIMEOUT_SECONDS"] = value
            with self.subTest(value=value), patch.dict(os.environ, env, clear=True):
                with self.assertRaises(SystemExit):
                    entrypoint._validate_railway_environment()

    def test_wrong_agent_llm_deadline_is_rejected(self) -> None:
        for value in ("", "4.0", "10.0", "15.0"):
            env = self._env()
            env["LINGXI_AGENT_LLM_REQUEST_TIMEOUT_SECONDS"] = value
            with self.subTest(value=value), patch.dict(os.environ, env, clear=True):
                with self.assertRaises(SystemExit):
                    entrypoint._validate_railway_environment()

    def test_missing_or_wrong_timezone_is_rejected(self) -> None:
        for value in ("", "UTC", "Etc/GMT-8"):
            env = self._env()
            env["LINGXI_TIMEZONE"] = value
            with self.subTest(value=value), patch.dict(os.environ, env, clear=True):
                with self.assertRaises(SystemExit):
                    entrypoint._validate_railway_environment()

    def test_missing_business_secret_is_rejected(self) -> None:
        env = self._env()
        del env["AMAP_KEY"]
        with patch.dict(os.environ, env, clear=True):
            with self.assertRaises(SystemExit):
                entrypoint._validate_railway_environment()

    def test_invalid_port_is_rejected(self) -> None:
        env = self._env()
        env["PORT"] = "0"
        with patch.dict(os.environ, env, clear=True):
            with self.assertRaises(SystemExit):
                entrypoint._validate_railway_environment()

    def test_hash_only_registry_is_accepted(self) -> None:
        registry = {
            "schema_version": "lingxi-device-auth.1",
            "devices": [
                {
                    "credential_id": "test-id",
                    "subject": "android",
                    "device_id": "mi8lite-2687da22",
                    "token_sha256": "a" * 64,
                    "enabled": True,
                }
            ],
        }
        encoded = base64.b64encode(json.dumps(registry).encode("utf-8")).decode("ascii")
        decoded = json.loads(entrypoint._decode_device_registry(encoded).decode("utf-8"))
        self.assertEqual(decoded, registry)

    def test_registry_without_active_credential_is_rejected(self) -> None:
        registry = {
            "schema_version": "lingxi-device-auth.1",
            "devices": [
                {
                    "subject": "android",
                    "device_id": "mi8lite-2687da22",
                    "token_sha256": "b" * 64,
                    "enabled": False,
                }
            ],
        }
        encoded = base64.b64encode(json.dumps(registry).encode("utf-8")).decode("ascii")
        with self.assertRaises(SystemExit):
            entrypoint._decode_device_registry(encoded)


if __name__ == "__main__":
    unittest.main()
