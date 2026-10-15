"""独立目录基础回归：不联网、不调用 14B/VLM，不依赖外部 PC 项目源码。

运行：
  python -B brain/smoke_standalone.py
"""
from __future__ import annotations

import os
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BRAIN = ROOT / "brain"

# 在导入 server 前冻结独立化条件：旧环境变量即使存在也不能成为默认依赖。
os.environ.pop("LINGXI_CORE_PROJECT", None)
os.environ["LINGXI_PC_PROJECT"] = str(ROOT / "__external_core_must_not_be_used__")
os.environ.setdefault("DASHSCOPE_API_KEY", "standalone-smoke-placeholder")

_profiles = tempfile.TemporaryDirectory(prefix="lingxi_standalone_profiles_")
os.environ["LINGXI_MOBILE_PROFILE_DIR"] = _profiles.name
sys.path.insert(0, str(BRAIN))

from fastapi.testclient import TestClient  # noqa: E402

import server  # noqa: E402
from lingxi.core import config as core_config  # noqa: E402


def main() -> int:
    assert server.BUNDLED_PROJECT == ROOT
    assert server.CORE_PROJECT == ROOT
    assert core_config.PROJECT_ROOT == ROOT
    assert core_config.DEFAULT_CONFIG_PATH == ROOT / "configs" / "config.yaml"

    # 构造会话会真实加载随附的 scikit-learn 域路由模型，但不会发起 LLM 请求。
    session = server._get_session("standalone_smoke")
    assert session.engine.router is not None

    # 所有已加载的 lingxi 模块都必须来自当前独立目录。
    for name, module in tuple(sys.modules.items()):
        if name != "lingxi" and not name.startswith("lingxi."):
            continue
        module_file = getattr(module, "__file__", None)
        if module_file:
            assert Path(module_file).resolve().is_relative_to(ROOT), (name, module_file)

    client = TestClient(server.app)
    health = client.get("/health")
    assert health.status_code == 200 and health.json()["status"] == "ok", health.text

    shortcut = client.post(
        "/interact", json={"user_id": "standalone_shortcut", "text": "打开相机"}
    )
    assert shortcut.status_code == 200, shortcut.text
    shortcut_data = shortcut.json()
    assert shortcut_data["device_action"]["type"] == "open_camera", shortcut_data

    frame = server._build_frame(
        server.InteractRequest(
            user_id="standalone_conflict",
            text="不要",
            speech_t_start=10.0,
            speech_t_end=12.0,
            vision=[
                server.VisionMark(
                    kind="head_motion",
                    label="nod",
                    confidence=0.9,
                    t_start=10.4,
                    t_end=11.6,
                )
            ],
        )
    )
    assert frame.precheck["verdict"] == "conflict", frame.precheck

    print("Standalone smoke: PASS")
    print(f"core={server.CORE_PROJECT}")
    print("router=loaded, health=ok, shortcut=open_camera, conflict=conflict")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    finally:
        _profiles.cleanup()

