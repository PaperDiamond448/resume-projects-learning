"""Cloud-3 screenshot upload contract.

The Android client performs the first privacy decision from the current a11y
screen text. This module is the server-side second gate: an old or malformed
client cannot reach the VLM merely by posting a base64 image.
"""
from __future__ import annotations

import base64
import hashlib
import hmac
from dataclasses import dataclass


POLICY_VERSION = "cloud-screen.1"
ALLOWED_PURPOSES = {"visual_pick", "structured_extract", "action_reflect"}
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


@dataclass(frozen=True)
class CloudScreenshotDecision:
    allowed: bool
    code: str
    png: bytes | None = None


def validate_cloud_screenshot(
    *,
    screenshot_b64: str,
    consent: bool,
    policy_version: str,
    purpose: str,
    expected_purpose: str,
    screen_sensitivity: str,
    screenshot_sha256: str,
) -> CloudScreenshotDecision:
    """Validate consent metadata and bind it to the exact uploaded PNG."""
    if not consent:
        return CloudScreenshotDecision(False, "CLOUD_VISION_CONSENT_REQUIRED")
    if policy_version != POLICY_VERSION:
        return CloudScreenshotDecision(False, "CLOUD_VISION_POLICY_MISMATCH")
    if purpose not in ALLOWED_PURPOSES or purpose != expected_purpose:
        return CloudScreenshotDecision(False, "CLOUD_VISION_PURPOSE_MISMATCH")
    if screen_sensitivity != "clear":
        return CloudScreenshotDecision(False, "SENSITIVE_SCREEN_BLOCKED")
    digest = (screenshot_sha256 or "").strip().lower()
    if len(digest) != 64 or any(ch not in "0123456789abcdef" for ch in digest):
        return CloudScreenshotDecision(False, "SCREENSHOT_DIGEST_INVALID")
    try:
        png = base64.b64decode(screenshot_b64, validate=True)
    except Exception:  # noqa: BLE001 - malformed input is a policy rejection
        return CloudScreenshotDecision(False, "SCREENSHOT_DECODE_FAILED")
    if not png.startswith(PNG_SIGNATURE):
        return CloudScreenshotDecision(False, "SCREENSHOT_NOT_PNG")
    actual = hashlib.sha256(png).hexdigest()
    if not hmac.compare_digest(actual, digest):
        return CloudScreenshotDecision(False, "SCREENSHOT_DIGEST_MISMATCH")
    return CloudScreenshotDecision(True, "ALLOW", png)
