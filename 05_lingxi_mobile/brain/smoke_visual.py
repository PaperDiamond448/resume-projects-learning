# -*- coding: utf-8 -*-
"""灵犀 Mobile · 视觉兜底(路线C) SoM 管线冒烟测试。

验证【管线机械逻辑】是否打通（与具体 VLM 强弱解耦）：
  合成截图 + a11y 元素框 → render_som 画编号 → VLM 选号 → 映射回原图中心坐标。
PASS 标准 = 管线返回【合法动作 + 命中真实元素的坐标】（plumbing 通）；
模型选得【对不对】单独报告；正式链只允许 `qwen2.5-vl-3b-instruct`。

台式机 LM Studio 节点在线后运行：
  $env:LINGXI_VLM_BASE_URL='http://<台式机IP>:1234/v1'
  $env:LINGXI_VLM_MODEL='qwen2.5-vl-3b-instruct'
  $env:LINGXI_VLM_API_KEY='lm-studio'
  $env:LINGXI_CLOUD_VLM_ENABLED='true'
  D:\python\python.exe brain\smoke_visual.py
"""
import io
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8", errors="replace")  # PowerShell GBK 兼容
sys.path.insert(0, str(Path(__file__).resolve().parent))  # 绝对导入 visual

from PIL import Image, ImageDraw, ImageFont  # noqa: E402

import visual  # noqa: E402

_FONT = "C:/Windows/Fonts/msyh.ttc"


def _font(sz):
    try:
        return ImageFont.truetype(_FONT, sz)
    except Exception:  # noqa: BLE001
        return ImageFont.load_default()


def make_screen(rows: list[str], w: int = 1080) -> tuple[bytes, list[dict]]:
    """造竖排列表合成截图 + 对应 a11y 元素框（bounds 用原图像素）。模拟真机分辨率 1080 宽。"""
    row_h, top, pad = 300, 80, 30
    h = top + row_h * len(rows) + pad
    img = Image.new("RGB", (w, h), "white")
    d = ImageDraw.Draw(img)
    f = _font(40)
    elements = []
    y = top
    for i, label in enumerate(rows, 1):
        l, t, r, b = pad, y, w - pad, y + row_h - 40
        d.rectangle([l, t, r, b], outline="#cccccc", width=3)
        d.text((l + 40, t + row_h // 2 - 60), label, fill="black", font=f)
        elements.append({"id": i, "text": label, "bounds": [l, t, r, b]})
        y += row_h
    buf = io.BytesIO()
    img.save(buf, format="PNG")
    return buf.getvalue(), elements


# 两个直击痛点的场景（期望命中=正在直播 / 每日推荐）
CASES = [
    ("进入正在直播的直播间",
     ["李四  正在直播中  3.2万人在线", "王五的录播  直播切片合集", "热门视频  搞笑日常合集"], 1),
    ("打开每日推荐歌单",
     ["每日推荐  根据口味精选30首", "听歌识曲  哼一句帮你找歌", "我的收藏  128首"], 1),
]


def main() -> int:
    print(f"VLM 模型 = {visual._vlm_model()}\n")
    plumbing_ok = 0
    choice_ok = 0
    for goal, rows, expect in CASES:
        png, elements = make_screen(rows)
        res = visual.visual_pick(goal, history=[], png_bytes=png, elements=elements)
        # 管线通过 = 返回合法动作；若是 tap，坐标必须落在某个真实元素框内
        valid_action = res["action"] in ("tap", "scroll", "back", "done")
        coord_ok = True
        if res["action"] == "tap":
            coord_ok = res["x"] is not None and any(
                el["bounds"][0] <= res["x"] <= el["bounds"][2]
                and el["bounds"][1] <= res["y"] <= el["bounds"][3] for el in elements)
        plumb = valid_action and coord_ok
        plumbing_ok += plumb
        hit = res["action"] == "tap" and res["mark"] == expect
        choice_ok += hit
        print(f"[{'管线OK' if plumb else '管线X'}] 目标「{goal}」")
        print(f"    → action={res['action']} mark={res['mark']} 坐标=({res['x']},{res['y']}) "
              f"reason={res['reason']!r}")
        print(f"    选择{'✅命中' if hit else '⚠️未命中'}期望编号 {expect}\n")

    n = len(CASES)
    print(f"=== 管线打通 {plumbing_ok}/{n}（PASS 看这个）；替身模型选对 {choice_ok}/{n}（信息项）===")
    return 0 if plumbing_ok == n else 1


if __name__ == "__main__":
    raise SystemExit(main())
