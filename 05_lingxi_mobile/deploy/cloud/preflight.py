"""Static Cloud-1 checks that do not require Docker."""
from __future__ import annotations

import json
import re
from pathlib import Path

import yaml


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def main() -> None:
    required = [
        "Dockerfile",
        "Dockerfile.dockerignore",
        "requirements.lock.txt",
        "entrypoint.py",
        "compose.local.yml",
        "compose.secure-local.yml",
        "manage_device_auth.py",
        "audit_secure_runtime.py",
        "Caddyfile.example",
        ".env.example",
        "README.md",
        "RAILWAY.md",
    ]
    missing = [name for name in required if not (HERE / name).is_file()]

    compose_text = (HERE / "compose.local.yml").read_text(encoding="utf-8")
    secure_compose_text = (HERE / "compose.secure-local.yml").read_text(encoding="utf-8")
    dockerfile_text = (HERE / "Dockerfile").read_text(encoding="utf-8")
    dockerignore_text = (HERE / "Dockerfile.dockerignore").read_text(encoding="utf-8")
    entrypoint_text = (HERE / "entrypoint.py").read_text(encoding="utf-8")
    project_scope_text = (ROOT / "PROJECT_SCOPE.md").read_text(encoding="utf-8")
    railway_text = (ROOT / ".railway" / "railway.ts").read_text(encoding="utf-8")
    railway_ignore_text = (ROOT / ".railwayignore").read_text(encoding="utf-8")
    railway_readme_text = (HERE / "RAILWAY.md").read_text(encoding="utf-8")
    server_text = (ROOT / "brain" / "server.py").read_text(encoding="utf-8")
    visual_text = (ROOT / "brain" / "visual.py").read_text(encoding="utf-8")
    automator_text = (ROOT / "android" / "app" / "src" / "main" / "java" / "com" / "lingxi" / "mobile" / "action" / "AppAutomator.kt").read_text(encoding="utf-8")
    compose = yaml.safe_load(compose_text)
    secure_compose = yaml.safe_load(secure_compose_text)
    brain = compose["services"]["brain"]
    secure_brain = secure_compose["services"]["brain"]
    ports = [str(item) for item in brain.get("ports", [])]
    secure_ports = [str(item) for item in secure_brain.get("ports", [])]

    checks = {
        "required_files": not missing,
        "localhost_only": ports == ["127.0.0.1:8000:8000"],
        "read_only_root": brain.get("read_only") is True,
        "single_worker": brain.get("environment", {}).get("LINGXI_WORKERS") == "1",
        "local_stage": brain.get("environment", {}).get("LINGXI_DEPLOYMENT_STAGE") == "local-container",
        "secure_localhost_only": secure_ports == ["127.0.0.1:8000:8000"],
        "secure_mode_required": (
            secure_brain.get("environment", {}).get("LINGXI_DEPLOYMENT_STAGE") == "secure-local"
            and secure_brain.get("environment", {}).get("LINGXI_SECURITY_MODE") == "required"
        ),
        "secure_vlm_deferred_fail_closed": (
            secure_brain.get("environment", {}).get("LINGXI_CLOUD_VLM_ENABLED") == "false"
            and
            secure_brain.get("environment", {}).get("LINGXI_VLM_MODEL")
            == "qwen2.5-vl-3b-instruct"
            and secure_brain.get("environment", {}).get("LINGXI_VLM_BASE_URL")
            == "http://host.docker.internal:1234/v1"
            and "host.docker.internal:host-gateway" in secure_brain.get("extra_hosts", [])
        ),
        "text_key_is_not_reused_as_vlm_key": (
            secure_brain.get("environment", {}).get("DASHSCOPE_API_KEY_FILE")
            == "/run/secrets/dashscope_api_key"
            and "dashscope_api_key" in secure_brain.get("secrets", [])
            and "LINGXI_VLM_API_KEY_FILE" not in secure_brain.get("environment", {})
            and "dashscope_vlm_api_key" not in secure_brain.get("secrets", [])
        ),
        "secure_registry_secret": (
            secure_brain.get("environment", {}).get("LINGXI_DEVICE_AUTH_FILE")
            == "/run/secrets/device_auth_registry"
            and "device_auth_registry" in secure_brain.get("secrets", [])
        ),
        "persistent_idempotency": (
            secure_brain.get("environment", {}).get("LINGXI_IDEMPOTENCY_DB")
            == "/app/brain/data/security/idempotency.sqlite3"
            and any(str(item).endswith(":/app/brain/data") for item in secure_brain.get("volumes", []))
        ),
        "secure_health_is_minimal_live": "/live" in secure_compose_text and "/health" not in secure_compose_text,
        "no_public_caddy_service": "caddy" not in compose.get("services", {}),
        "persistent_brain_data": any(
            str(item).endswith(":/app/brain/data") for item in brain.get("volumes", [])
        ),
        "docker_context_is_clean_root": brain.get("build", {}).get("context") == "../..",
        "original_source_not_in_context": (
            ROOT.name == "lingxi-project-clean"
            or (
                ROOT.as_posix() == "/app"
                and "# 纯项目边界审计" in project_scope_text
                and "原 Android/Brain 与外部 PC 大脑源码不删不改" in project_scope_text
            )
        ),
        "dockerfile_specific_ignore_active": (
            brain.get("build", {}).get("dockerfile") == "deploy/cloud/Dockerfile"
            and (HERE / "Dockerfile.dockerignore").is_file()
            and not (HERE / ".dockerignore").exists()
        ),
        "contract_verification_stage": all(
            token in dockerfile_text
            for token in (
                "FROM runtime-base AS contract-verifier",
                "python -B /app/brain/audit_pending_contract.py",
                "python -B /app/brain/smoke_standalone.py",
                "smoke_server.test_health()",
                "test_cloud_security.py",
                "audit_cloud_security.py",
                "cloud2-unit=7/7",
                "cloud2-audit=18/18",
                "test_cloud3_contract.py",
                "audit_cloud3.py",
                "cloud3-unit=10/10",
                "cloud3-audit=15/15",
                "test_cloud5_latency_contract.py",
                "cloud5-latency-unit=18/18",
                "test_agent_entry_timeout_contract.py",
                "agent-entry-timeout=7/7",
                "test_entrypoint.py",
                "entrypoint-policy=18/18",
                "python -B /app/android/audit_release_network_security.py",
                "cloud4-release-network=11/11",
                "python -B /app/deploy/cloud/preflight.py",
                "cloud5-railway-static=PASS",
                "FROM runtime-base AS final",
                "COPY --from=contract-verifier /tmp/cloud1-contracts.ok",
            )
        ),
        "runtime_excludes_android_source": all(
            f"!{path}" in dockerignore_text
            for path in (
                "android/app/src/main/java/com/lingxi/mobile/MainActivity.kt",
                "android/app/src/main/java/com/lingxi/mobile/voice/KwsForegroundService.kt",
                "android/app/src/main/java/com/lingxi/mobile/action/DeviceActionExecutor.kt",
                "android/app/src/main/java/com/lingxi/mobile/action/AppAutomator.kt",
                "android/app/src/main/java/com/lingxi/mobile/confirmation/ConfirmationEvidencePolicy.kt",
                "android/app/src/main/java/com/lingxi/mobile/confirmation/PendingConfirmationStore.kt",
                "android/app/src/main/java/com/lingxi/mobile/confirmation/TypedConfirmationHandoffPolicy.kt",
                "android/app/src/main/java/com/lingxi/mobile/model/Api.kt",
                "android/app/src/main/java/com/lingxi/mobile/net/BrainClient.kt",
                "android/app/src/main/java/com/lingxi/mobile/net/BrainConnectionPolicy.kt",
                "android/app/src/main/java/com/lingxi/mobile/net/BrainConnectionStore.kt",
                "android/app/src/main/java/com/lingxi/mobile/LingxiApplication.kt",
                "android/app/src/main/java/com/lingxi/mobile/vision/CloudVisionPolicy.kt",
                "android/app/src/main/java/com/lingxi/mobile/vision/CloudVisionConsentStore.kt",
                "android/app/src/main/AndroidManifest.xml",
                "android/app/src/debug/AndroidManifest.xml",
                "android/app/src/release/AndroidManifest.xml",
                "android/app/src/release/res/xml/network_security_config.xml",
                "android/audit_release_network_security.py",
            )
        ),
        "cloud4_release_network_contract": all(
            token in dockerfile_text
            for token in (
                "COPY android/app/src/release/AndroidManifest.xml",
                "COPY android/app/src/release/res/xml/network_security_config.xml",
                "COPY android/audit_release_network_security.py",
                "python -B /app/android/audit_release_network_security.py",
            )
        ),
        "secrets_directory_ignored": "deploy/cloud/secrets/" in (ROOT / ".gitignore").read_text(encoding="utf-8"),
        "secure_entrypoint_gate": all(
            token in entrypoint_text
            for token in (
                'stage not in {"local-container", "secure-local", "railway"}',
                "stage in SECURE_STAGES and security_mode != \"required\"",
                "deployment stage is not approved by the Lingxi cloud contract",
                "requires exact qwen2.5-vl-3b-instruct",
                "VLM requires HTTPS, or HTTP on a loopback/private self-hosted endpoint",
                "enabled VLM requires LINGXI_VLM_API_KEY",
            )
        ),
        "railway_config_contract": (
            'service("lingxi-brain-staging"' in railway_text
            and 'volume("lingxi-brain-staging-volume"' in railway_text
            and 'healthcheck: "/live"' in railway_text
            and "healthcheckTimeout: 300" in railway_text
            and '"asia-southeast1-eqsg3a": 1' in railway_text
            and 'region: "asia-southeast1-eqsg3a"' in railway_text
            and '"/app/brain/data": data' in railway_text
            and 'RAILWAY_DOCKERFILE_PATH: "/deploy/cloud/Dockerfile"' in railway_text
            and 'LINGXI_CLOUD_VLM_ENABLED: "false"' in railway_text
            and 'LINGXI_LLM_REQUEST_TIMEOUT_SECONDS: "4.5"' in railway_text
            and 'LINGXI_AGENT_LLM_REQUEST_TIMEOUT_SECONDS: "12.0"' in railway_text
            and 'LINGXI_TIMEZONE: "Asia/Shanghai"' in railway_text
        ),
        "railway_explicit_public_gate": all(
            token in entrypoint_text
            for token in (
                'RAILWAY_APPROVAL = "railway-cloud5-vlm-disabled"',
                'RAILWAY_LLM_TIMEOUT_SECONDS = "4.5"',
                'RAILWAY_AGENT_LLM_TIMEOUT_SECONDS = "12.0"',
                'RAILWAY_TIMEZONE = "Asia/Shanghai"',
                "Railway public deployment approval is missing or invalid",
                "Railway platform marker missing",
                "Railway requires LINGXI_LLM_REQUEST_TIMEOUT_SECONDS=4.5",
                "Railway requires LINGXI_AGENT_LLM_REQUEST_TIMEOUT_SECONDS=12.0",
                "Railway requires LINGXI_TIMEZONE=Asia/Shanghai",
            )
        ),
        "railway_response_deadline_contract": all(
            token in server_text
            for token in (
                'os.environ.get("LINGXI_DEPLOYMENT_STAGE", "").strip() == "railway"',
                'raw_timeout != "4.5"',
                'llm_cfg["request_timeout"] = 4.5',
                'llm_cfg["max_retries"] = 0',
                'llm_cfg["hard_deadline"] = 4.5',
                '["repair_invalid_json"] = False',
                '["allow_chitchat_reply_fallback"] = False',
                '"LINGXI_AGENT_LLM_REQUEST_TIMEOUT_SECONDS"',
                '_AGENT_LLM_TIMEOUT_SECONDS = 12.0',
                'llm_cfg["hard_deadline"] = timeout',
                'def _get_agent_llm()',
                '_railway_weather_deadline_seconds = 3.5',
            )
        ),
        "railway_dynamic_port": (
            'return _parse_port(os.environ.get("PORT", ""))' in entrypoint_text
            and '"--port",\n        str(port)' in entrypoint_text
        ),
        "railway_persistent_state_paths": all(
            token in entrypoint_text
            for token in (
                'RAILWAY_DATA_ROOT_TEXT = "/app/brain/data"',
                'RAILWAY_AUTH_FILE_TEXT = "/app/brain/data/security/device_auth_registry.json"',
                'RAILWAY_IDEMPOTENCY_DB_TEXT = "/app/brain/data/security/idempotency.sqlite3"',
                "Railway volume must be mounted at /app/brain/data",
            )
        ),
        "railway_root_then_drop_uid999": all(
            token in entrypoint_text
            for token in (
                'os.environ.get("RAILWAY_RUN_UID", "").strip() != "0"',
                'pwd.getpwnam("lingxi")',
                "account.pw_uid != 999",
                "os.setuid(account.pw_uid)",
                "os.geteuid() != 999",
            )
        ),
        "railway_vlm_disabled_pilot": (
            'LINGXI_CLOUD_VLM_ENABLED", "").strip().lower() != "false"' in entrypoint_text
            and "railway-cloud5-vlm-disabled" in railway_readme_text
            and "qwen2.5-vl-3b-instruct" in railway_readme_text
        ),
        "railway_registry_is_hash_only": all(
            token in entrypoint_text
            for token in (
                "_decode_device_registry",
                "token_sha256",
                "has no active credential",
                'os.environ.pop("LINGXI_DEVICE_AUTH_REGISTRY_B64"',
                "os.replace(temp_path, RAILWAY_AUTH_FILE)",
            )
        ),
        "railway_upload_excludes_local_secrets": all(
            token in railway_ignore_text
            for token in (
                ".secrets/",
                "deploy/cloud/secrets/",
                "android/app/build/",
                "release-signing.properties",
            )
        ),
        "railway_iac_preserves_secrets_and_contains_no_domain": (
            all(
                f"{name}: preserve()" in railway_text
                for name in (
                    "DASHSCOPE_API_KEY",
                    "QWEATHER_KEY",
                    "AMAP_KEY",
                    "LINGXI_DEVICE_AUTH_REGISTRY_B64",
                )
            )
            and "domains:" not in railway_text
            and "publicNetworking" not in railway_text
        ),
        "cloud3_screenshot_double_gate": (
            "validate_cloud_screenshot" in server_text
            and "CloudVisionPolicy.decide" in automator_text
            and "CloudVisionConsentStore.isEnabled" in automator_text
        ),
        "no_third_party_vlm_fallback": all(
            token not in visual_text
            for token in ("openrouter.ai", "OPENROUTER_API_KEY", "FIREWORKS_API_KEY")
        ),
        "security_middleware_wired": all(
            token in server_text
            for token in (
                "SecurityConfig.from_env",
                "CloudSecurityMiddleware",
                "docs_url=None if _secure_api",
                '@app.get("/live")',
                '@app.get("/ready")',
                'logging.getLogger("lingxi").setLevel(logging.CRITICAL)',
            )
        ),
        "no_key_literal": not bool(
            re.search(
                r"(?:sk-[A-Za-z0-9_-]{12,}|DASHSCOPE_API_KEY\s*[:=]\s*[^/$\s])",
                compose_text + "\n" + secure_compose_text,
            )
        ) and all(
            f"{name}: preserve()" in railway_text
            for name in (
                "DASHSCOPE_API_KEY",
                "QWEATHER_KEY",
                "AMAP_KEY",
                "LINGXI_DEVICE_AUTH_REGISTRY_B64",
            )
        ),
    }
    failed = [name for name, ok in checks.items() if not ok]
    result = {"status": "PASS" if not failed else "FAIL", "checks": checks, "missing": missing}
    print(json.dumps(result, ensure_ascii=False, indent=2))
    if failed:
        raise SystemExit(f"Cloud-1 preflight failed: {', '.join(failed)}")


if __name__ == "__main__":
    main()
