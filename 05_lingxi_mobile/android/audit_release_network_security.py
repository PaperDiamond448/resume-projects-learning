"""Cloud-4 static release-network contract; stdlib only and no device/network use."""
from __future__ import annotations

import json
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parent
APP = ROOT / "app"
ANDROID = "{http://schemas.android.com/apk/res/android}"


def attr(node: ET.Element, name: str) -> str:
    return node.attrib.get(ANDROID + name, "")


def main() -> None:
    main_manifest = ET.parse(APP / "src/main/AndroidManifest.xml").getroot()
    debug_manifest = ET.parse(APP / "src/debug/AndroidManifest.xml").getroot()
    release_manifest = ET.parse(APP / "src/release/AndroidManifest.xml").getroot()
    security = ET.parse(
        APP / "src/release/res/xml/network_security_config.xml"
    ).getroot()

    main_app = main_manifest.find("application")
    release_app = release_manifest.find("application")
    base = security.find("base-config")
    certs = security.findall("./base-config/trust-anchors/certificates")
    release_text = (APP / "src/release/AndroidManifest.xml").read_text(encoding="utf-8")
    policy_text = (
        APP / "src/main/java/com/lingxi/mobile/net/BrainConnectionPolicy.kt"
    ).read_text(encoding="utf-8")
    application_text = (
        APP / "src/main/java/com/lingxi/mobile/LingxiApplication.kt"
    ).read_text(encoding="utf-8")

    checks = {
        "debug_inherits_local_cleartext_capability": (
            main_app is not None
            and attr(main_app, "usesCleartextTraffic") == "true"
            and debug_manifest.find("application") is not None
        ),
        "release_manifest_denies_cleartext": (
            release_app is not None
            and attr(release_app, "usesCleartextTraffic") == "false"
        ),
        "release_manifest_binds_network_config": (
            release_app is not None
            and attr(release_app, "networkSecurityConfig") == "@xml/network_security_config"
        ),
        "release_override_is_explicit": "tools:replace=\"android:usesCleartextTraffic\"" in release_text,
        "release_base_denies_cleartext": (
            base is not None and base.attrib.get("cleartextTrafficPermitted") == "false"
        ),
        "release_trusts_system_ca_only": (
            len(certs) == 1 and certs[0].attrib.get("src") == "system"
        ),
        "release_has_no_domain_exception": security.find("domain-config") is None,
        "release_has_no_debug_override": security.find("debug-overrides") is None,
        "runtime_release_policy_is_https_only": (
            'else "正式版只允许HTTPS"' in policy_text
            and 'scheme != "https"' in policy_text
        ),
        "debug_http_is_loopback_only": (
            'setOf("127.0.0.1", "localhost", "10.0.2.2")' in policy_text
        ),
        "release_startup_asserts_platform_cleartext_denial": all(
            token in application_text
            for token in (
                "NetworkSecurityPolicy.getInstance().isCleartextTrafficPermitted",
                "BuildConfig.DEBUG ||",
                "Release cleartext traffic must be disabled",
            )
        ),
    }
    failed = [name for name, ok in checks.items() if not ok]
    print(json.dumps({
        "status": "PASS" if not failed else "FAIL",
        "passed": len(checks) - len(failed),
        "total": len(checks),
        "checks": checks,
    }, ensure_ascii=False, indent=2))
    if failed:
        raise SystemExit("Cloud-4 release network audit failed: " + ", ".join(failed))


if __name__ == "__main__":
    main()
