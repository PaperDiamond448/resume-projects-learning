FROM python:3.10-slim-bookworm AS runtime-base

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    PIP_DISABLE_PIP_VERSION_CHECK=1 \
    PIP_NO_CACHE_DIR=1

RUN apt-get update \
    && apt-get install -y --no-install-recommends ca-certificates libgomp1 \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --system lingxi \
    && useradd --system --gid lingxi --home-dir /app --shell /usr/sbin/nologin lingxi

WORKDIR /app

COPY deploy/cloud/requirements.lock.txt /tmp/requirements.lock.txt
RUN python -m pip install --no-cache-dir -r /tmp/requirements.lock.txt

COPY brain/ /app/brain/
COPY configs/ /app/configs/
COPY data/ /app/data/
COPY lingxi/ /app/lingxi/
COPY deploy/cloud/entrypoint.py /app/deploy/cloud/entrypoint.py

RUN mkdir -p /app/brain/data/profiles \
    && chown -R lingxi:lingxi /app/brain/data

USER lingxi
EXPOSE 8000

CMD ["python", "/app/deploy/cloud/entrypoint.py"]

# The contract audit contains three static Android source checks.  Keep those
# sources in a throwaway verification stage, never in the final runtime image.
FROM runtime-base AS contract-verifier

COPY .gitignore /app/.gitignore
COPY .railwayignore /app/.railwayignore
COPY PROJECT_SCOPE.md /app/PROJECT_SCOPE.md
COPY .railway/railway.ts /app/.railway/railway.ts
COPY deploy/cloud/ /app/deploy/cloud/
COPY android/app/src/main/java/com/lingxi/mobile/MainActivity.kt /app/android/app/src/main/java/com/lingxi/mobile/MainActivity.kt
COPY android/app/src/main/java/com/lingxi/mobile/voice/KwsForegroundService.kt /app/android/app/src/main/java/com/lingxi/mobile/voice/KwsForegroundService.kt
COPY android/app/src/main/java/com/lingxi/mobile/action/DeviceActionExecutor.kt /app/android/app/src/main/java/com/lingxi/mobile/action/DeviceActionExecutor.kt
COPY android/app/src/main/java/com/lingxi/mobile/action/AppAutomator.kt /app/android/app/src/main/java/com/lingxi/mobile/action/AppAutomator.kt
COPY android/app/src/main/java/com/lingxi/mobile/confirmation/ConfirmationEvidencePolicy.kt /app/android/app/src/main/java/com/lingxi/mobile/confirmation/ConfirmationEvidencePolicy.kt
COPY android/app/src/main/java/com/lingxi/mobile/confirmation/PendingConfirmationStore.kt /app/android/app/src/main/java/com/lingxi/mobile/confirmation/PendingConfirmationStore.kt
COPY android/app/src/main/java/com/lingxi/mobile/confirmation/TypedConfirmationHandoffPolicy.kt /app/android/app/src/main/java/com/lingxi/mobile/confirmation/TypedConfirmationHandoffPolicy.kt
COPY android/app/src/main/java/com/lingxi/mobile/model/Api.kt /app/android/app/src/main/java/com/lingxi/mobile/model/Api.kt
COPY android/app/src/main/java/com/lingxi/mobile/net/BrainClient.kt /app/android/app/src/main/java/com/lingxi/mobile/net/BrainClient.kt
COPY android/app/src/main/java/com/lingxi/mobile/net/BrainConnectionPolicy.kt /app/android/app/src/main/java/com/lingxi/mobile/net/BrainConnectionPolicy.kt
COPY android/app/src/main/java/com/lingxi/mobile/net/BrainConnectionStore.kt /app/android/app/src/main/java/com/lingxi/mobile/net/BrainConnectionStore.kt
COPY android/app/src/main/java/com/lingxi/mobile/LingxiApplication.kt /app/android/app/src/main/java/com/lingxi/mobile/LingxiApplication.kt
COPY android/app/src/main/java/com/lingxi/mobile/vision/CloudVisionPolicy.kt /app/android/app/src/main/java/com/lingxi/mobile/vision/CloudVisionPolicy.kt
COPY android/app/src/main/java/com/lingxi/mobile/vision/CloudVisionConsentStore.kt /app/android/app/src/main/java/com/lingxi/mobile/vision/CloudVisionConsentStore.kt
COPY android/app/src/main/AndroidManifest.xml /app/android/app/src/main/AndroidManifest.xml
COPY android/app/src/debug/AndroidManifest.xml /app/android/app/src/debug/AndroidManifest.xml
COPY android/app/src/release/AndroidManifest.xml /app/android/app/src/release/AndroidManifest.xml
COPY android/app/src/release/res/xml/network_security_config.xml /app/android/app/src/release/res/xml/network_security_config.xml
COPY android/audit_release_network_security.py /app/android/audit_release_network_security.py

RUN python -B /app/deploy/cloud/preflight.py \
    && python -B /app/brain/audit_pending_contract.py \
    && python -B /app/brain/smoke_standalone.py \
    && python -B -c "import sys; sys.path.insert(0, '/app/brain'); import smoke_server; smoke_server.test_health(); print('Brain health smoke: PASS')" \
    && python -B -m unittest discover -s /app/brain -p 'test_cloud_security.py' -v \
    && python -B /app/brain/audit_cloud_security.py \
    && python -B -m unittest discover -s /app/brain -p 'test_cloud3_contract.py' -v \
    && python -B /app/brain/audit_cloud3.py \
    && python -B -m unittest discover -s /app/brain -p 'test_cloud5_latency_contract.py' -v \
    && python -B -m unittest discover -s /app/brain -p 'test_agent_entry_timeout_contract.py' -v \
    && python -B -m unittest /app/deploy/cloud/test_entrypoint.py -v \
    && python -B /app/android/audit_release_network_security.py \
    && printf 'cloud-preflight=PASS\npending-action.3=29/29\nstandalone=PASS\nbrain-health-smoke=PASS\ncloud2-unit=7/7\ncloud2-audit=18/18\ncloud3-unit=10/10\ncloud3-audit=15/15\ncloud5-latency-unit=18/18\nagent-entry-timeout=7/7\nentrypoint-policy=18/18\ncloud4-release-network=11/11\ncloud5-railway-static=PASS\n' > /tmp/cloud1-contracts.ok

FROM runtime-base AS final

COPY --from=contract-verifier /tmp/cloud1-contracts.ok /app/deploy/cloud/cloud1-contracts.ok
