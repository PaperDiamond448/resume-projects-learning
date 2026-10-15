"""大脑服务端冒烟测试 —— 不占端口，用 FastAPI TestClient 直连 app。

  python brain/smoke_server.py

- /health：离线必测（验证服务起得来、能跨目录加载 lingxi 配置）。
- /interact：需 DASHSCOPE_API_KEY + 网络；缺 Key 自动跳过（不算失败）。
"""
from __future__ import annotations

import os
import sys
import tempfile
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
sys.path.insert(0, str(Path(__file__).resolve().parent))

from fastapi.testclient import TestClient  # noqa: E402

from server import app  # noqa: E402

client = TestClient(app)


def test_health() -> None:
    r = client.get("/health")
    assert r.status_code == 200, r.status_code
    data = r.json()
    assert data["status"] == "ok", data
    print(f"  /health OK → provider={data['llm_provider']}")


def test_interact_query_time() -> None:
    """即时执行类：问时间，应 executed、reply 非空、无需设备动作。"""
    r = client.post("/interact", json={"user_id": "smoke", "text": "现在几点了"})
    assert r.status_code == 200, r.text
    data = r.json()
    print(f"  问时间 → intent={data['intent']} status={data['status']} "
          f"reply={data['reply']!r} ({data['latency'].get('total')}s)")
    assert data["reply"], "reply 不应为空"


def test_multiturn_ellipsis_weather() -> None:
    """T-23①：省略追问——"那后天呢"应沿用上一轮城市，只换日期。"""
    r1 = client.post("/interact", json={"user_id": "smoke_t23a", "text": "明天哈尔滨天气怎么样"})
    r2 = client.post("/interact", json={"user_id": "smoke_t23a", "text": "那后天呢"})
    d2 = r2.json()
    print(f"  省略追问 → intent={d2['intent']} slots={d2['slots']}")
    assert d2["slots"].get("location") == "哈尔滨", d2


def test_multiturn_correction_recipient() -> None:
    """T-23②：待确认动作的收件人修正——回复文案必须跟着新收件人走，不能停在旧的（2026-07-03 抓到真
    bug：generator 曾被历史带偏、复述回旧收件人"妈妈"，即使 slots 已经正确更新成"李娜"）。"""
    client.post("/interact", json={"user_id": "smoke_t23b", "text": "给妈妈发短信说我晚点回家"})
    r2 = client.post("/interact", json={"user_id": "smoke_t23b", "text": "不对，是发给李娜"})
    d2 = r2.json()
    print(f"  收件人修正 → slots={d2['slots']} reply={d2['reply']!r}")
    assert d2["slots"].get("recipient") == "李娜", d2
    assert "李娜" in d2["reply"] and "妈妈" not in d2["reply"], f"回复文案没跟上修正后的收件人: {d2}"
    r3 = client.post("/interact", json={"user_id": "smoke_t23b", "text": "确认发送"})
    act = r3.json().get("device_action")
    assert act and act["slots"].get("recipient") == "李娜", f"最终下发的动作应发给李娜: {act}"


def test_multiturn_correction_content() -> None:
    """T-23②续：待确认动作的内容修正——2026-07-03 抓到更重的真 bug："内容改成X"曾被误判成
    chitchat，chitchat 的 LLM 编了句"已改好"的假话、但 pending_action 内容其实没变，
    确认后会发出错误内容。修法=prompts.py 补一条 few-shot 示例，教会模型承接 send_message。"""
    client.post("/interact", json={"user_id": "smoke_t23b2", "text": "给妈妈发短信说晚点到"})
    r2 = client.post("/interact", json={"user_id": "smoke_t23b2", "text": "内容改成堵车了要晚点"})
    d2 = r2.json()
    print(f"  内容修正 → intent={d2['intent']} slots={d2['slots']}")
    assert d2["intent"] == "send_message", f"不能被误判成 chitchat: {d2}"
    assert d2["slots"].get("content") == "堵车了要晚点", d2
    r3 = client.post("/interact", json={"user_id": "smoke_t23b2", "text": "确认发送"})
    act = r3.json().get("device_action")
    assert act and act["slots"].get("content") == "堵车了要晚点", f"最终下发内容应是修正后的: {act}"


def test_multiturn_correction_reminder_time() -> None:
    """T-23③：上文指代修正——"改成九点半"应承接上一轮闹钟目标，只改时间。"""
    client.post("/interact", json={"user_id": "smoke_t23c", "text": "订个明天九点的闹钟"})
    r2 = client.post("/interact", json={"user_id": "smoke_t23c", "text": "改成九点半"})
    d2 = r2.json()
    print(f"  时间修正 → intent={d2['intent']} slots={d2['slots']}")
    assert "九点半" in str(d2["slots"].get("time", "")), d2


def test_interact_send_confirm() -> None:
    """两段式：发短信 → 确认 → 应回填 device_action(收件人/内容)。"""
    r1 = client.post("/interact", json={
        "user_id": "smoke", "text": "给妈妈发短信说我晚点回家",
        "screen": {"app": "短信"}})
    d1 = r1.json()
    print(f"  发短信 → intent={d1['intent']} status={d1['status']} "
          f"slots={d1['slots']} reply={d1['reply']!r}")

    r2 = client.post("/interact", json={"user_id": "smoke", "text": "确认发送"})
    d2 = r2.json()
    act = d2.get("device_action")
    print(f"  确认 → intent={d2['intent']} status={d2['status']} device_action={act}")
    if d1["status"] == "pending_confirmation":
        assert act and act["type"] == "send_message", f"确认后应下发发短信动作: {act}"
        assert act["slots"].get("recipient"), f"应带收件人: {act}"


def test_agent_reflect_no_change() -> None:
    """动作后反思离线契约：前后屏幕相同必须判 NO_CHANGE，不调 VLM 也能兜底。"""
    r = client.post("/agent_reflect", json={
        "goal": "打开b站进入第一个直播间",
        "subgoal": "进入直播栏目",
        "action": "tap",
        "target": "直播",
        "before_screen": "首页 推荐 直播 我的",
        "after_screen": "首页 推荐 直播 我的",
        "app": "b站",
    })
    assert r.status_code == 200, r.text
    data = r.json()
    print(f"  /agent_reflect 无变化 → status={data['status']} reason={data.get('reason')!r}")
    assert data["status"] == "NO_CHANGE", data


def test_agent_plan_not_contaminated_by_hints_example() -> None:
    """2026-07-03 真机实锤（Fable5 定位）：酷狗 app_guides hints 反复举"每日推荐"例子，
    "务必遵守"措辞把模型带偏——用户要"点击猜你喜欢栏目"，模型计划却去点"每日推荐"，
    完全无视真实目标。修法=hints 措辞改为"仅供参考背景知识"+ prompt 补"以任务字段为准"规则。
    这里直接测 /agent_plan：目标里的"猜你喜欢"必须原样出现在生成的子步骤里，不能被替换成
    hints 示例里的"每日推荐"。"""
    r = client.post("/agent_plan", json={"goal": "点击猜你喜欢栏目", "app": "酷狗音乐"})
    assert r.status_code == 200, r.text
    d = r.json()
    steps_text = " ".join(d.get("steps", []))
    print(f"  /agent_plan(猜你喜欢) → source={d.get('source')} steps={d.get('steps')}")
    assert "猜你喜欢" in steps_text, f"计划偷换了目标，没提到猜你喜欢: {d}"
    assert "每日推荐" not in steps_text, f"计划被 hints 示例污染，误替换成每日推荐: {d}"


def test_reflect_with_screenshot_falls_back_to_text_llm() -> None:
    """2026-07-03 真机第二发：用户开了屏幕视觉→验收自动带截图→必走 VLM→台式机 VLM 断线
    →APIConnectionError 连环，反而比不开视觉更糟。修法=VLM 调用失败且 text_llm 可用时，
    扔掉截图降级文本重判，绝不把截图当单点故障。此测试直接构造"VLM 必炸+假 text_llm"验证降级路。"""
    import visual

    class _FakeTextLLM:
        def chat(self, messages, temperature=0.0, max_tokens=96):
            return '{"thought":"列表已出现","status":"SUCCESS","reason":"进入列表页"}'

    class _BoomCompletions:
        @staticmethod
        def create(**_kwargs):
            raise ConnectionError("vlm down")

    class _BoomChat:
        completions = _BoomCompletions()

    class _BoomClient:
        chat = _BoomChat()

    old_client = visual._client
    visual._client = _BoomClient()
    try:
        out = visual.reflect_action(
            "酷狗音乐播放每日推荐第一首", "点每日推荐入口", "tap", "每日推荐",
            "首页 每日推荐 听歌识曲", "每日推荐 小半 女儿心如水 播放全部",
            b"\x89PNG-fake-bytes", "", text_llm=_FakeTextLLM(),
        )
        print(f"  带截图VLM挂→降级文本 → status={out['status']} model={out['model']}")
        assert out["status"] == "SUCCESS" and out["model"] == "text-llm-fallback", out
    finally:
        visual._client = old_client


def test_agent_reflect_text_only_survives_vlm_outage() -> None:
    """2026-07-03 真机实测抓到的真 bug：没截图的纯文本验收以前也硬走 VLM 客户端，
    台式机 VLM(3B) 一断线，连这种本该由 14B 稳定判断的文本推理也报废——GUI-agent
    因此在明明已经成功、只是"验收"这一步失败时反复重试同一子步骤(用户观察到的
    "进了每日推荐页却还在原地反复上下滑")。这里故意不传 screenshot，且要求 DASHSCOPE_API_KEY
    在、VLM 不在也能正确判出 SUCCESS——这正是当前真实环境(台式机 VLM 不可达)的写照。"""
    r = client.post("/agent_reflect", json={
        "goal": "酷狗音乐播放每日推荐第一首",
        "subgoal": "在首页找到并点击【每日推荐】入口卡片",
        "action": "tap", "target": "每日推荐",
        "before_screen": "首页 推荐 排行榜 我的 每日推荐 听歌识曲",
        "after_screen": "每日推荐 1 出发 2 稻香 3 晴天 播放全部",
        "app": "酷狗音乐",
    })
    assert r.status_code == 200, r.text
    data = r.json()
    print(f"  纯文本验收(VLM未连) → status={data['status']} model={data.get('model')} reason={data.get('reason')!r}")
    assert data["status"] == "SUCCESS", f"应判成功进入每日推荐列表，不该因VLM离线UNSURE: {data}"
    assert data.get("model") != "APIConnectionError", data


def test_demo_to_skill_docs() -> None:
    """P1-B：真人示教应沉淀为控件级技能文档，且可被相似目标检索。"""
    import app_memory

    old_data, old_demo, old_skill = app_memory.DATA_FILE, app_memory.DEMO_FILE, app_memory.SKILL_FILE
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        app_memory.DATA_FILE = root / "app_memory.json"
        app_memory.DEMO_FILE = root / "app_demos.json"
        app_memory.SKILL_FILE = root / "app_skills.json"
        try:
            res = app_memory.record_demo("b站", "进入第一个直播间", [
                {"action": "tap", "text": "直播,7之1,标签",
                 "bounds": [0, 203, 159, 302], "sig": "首页 推荐 直播 热门 搜索"},
                {"action": "tap", "text": "主播名 在线人数 直播标题",
                 "bounds": [13, 1061, 534, 1590], "sig": "直播 推荐 在线人数 主播"},
            ])
            assert res["skill_docs"] == 2, res
            docs = app_memory.lookup_skill_docs("b站", "打开b站进入第一个直播间")
            hint = app_memory.skill_hints_for("b站", "打开b站进入第一个直播间")
            print(f"  示教→技能文档 → docs={len(docs)} hint={hint[:40]!r}")
            assert docs, "应能检索到示教沉淀的技能文档"
            assert "进入直播栏目" in hint or "直播" in hint, hint
        finally:
            app_memory.DATA_FILE, app_memory.DEMO_FILE, app_memory.SKILL_FILE = old_data, old_demo, old_skill


def test_trace_to_skill_and_avoid_docs() -> None:
    """P1-C：成功 trace 生成技能文档，失败 trace 生成避坑文档。"""
    import app_memory

    old_data, old_demo, old_skill = app_memory.DATA_FILE, app_memory.DEMO_FILE, app_memory.SKILL_FILE
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        app_memory.DATA_FILE = root / "app_memory.json"
        app_memory.DEMO_FILE = root / "app_demos.json"
        app_memory.SKILL_FILE = root / "app_skills.json"
        try:
            ok = app_memory.record(
                "b站",
                "进入第一个直播间",
                ["进入直播栏目", "点第一个直播间", "确认已进入直播间"],
                ["tap:直播", "vision:tap"],
                [
                    {"subgoal": "进入直播栏目", "action": "tap", "target": "直播",
                     "beforeHash": 1, "afterHash": 2, "reason": "进入栏目"},
                    {"subgoal": "点第一个直播间", "action": "vision:tap", "target": "视觉候选",
                     "beforeHash": 2, "afterHash": 3, "reason": "卡片"},
                ],
                True,
                "completed",
            )
            bad = app_memory.record(
                "b站",
                "进入第一个直播间",
                ["进入直播栏目", "点第一个直播间"],
                ["reflect_wrong_page_back"],
                [
                    {"subgoal": "点第一个直播间", "action": "recover:back", "target": "",
                     "beforeHash": 3, "afterHash": 3, "reason": "走错页"},
                ],
                False,
                "failed at: 点第一个直播间",
            )
            hints = app_memory.skill_hints_for("b站", "打开b站进入第一个直播间", limit=8)
            print(f"  trace→技能/避坑 → ok={ok} bad={bad} hints={hints[:60]!r}")
            assert ok["skill_docs"] >= 1, ok
            assert bad["avoid_docs"] >= 1, bad
            assert "成功技能" in hints and "避坑" in hints, hints
        finally:
            app_memory.DATA_FILE, app_memory.DEMO_FILE, app_memory.SKILL_FILE = old_data, old_demo, old_skill


def test_extract_pick_parsing() -> None:
    """T-14：数值解析(带单位)与比较方向推断，纯逻辑不需要 VLM。"""
    import visual

    assert visual._parse_number("1200万") == 1.2e7
    assert visual._parse_number("4.8分") == 4.8
    assert visual._parse_number("¥4999") == 4999.0
    assert visual._parse_number(None) is None
    assert visual.infer_higher_is_better("播放量最高") is True
    assert visual.infer_higher_is_better("哪家便宜") is False
    assert visual.infer_higher_is_better("最便宜") is False
    items = visual._parse_extract(
        '[{"mark":1,"title":"A","value":"800万"},{"mark":2,"title":"B","value":"1200万"}]',
        {1, 2},
    )
    assert len(items) == 2 and items[1]["value"] == 1.2e7, items
    print("  extract_pick 数值解析/方向推断 → OK")


def test_agent_extract_endpoint() -> None:
    """T-14 端到端契约：截图+候选元素 → VLM 只抽数字、代码比大小选中数值更大的编号。
    假 VLM client（不依赖台式机是否在线），验证的是「数值比较交代码」这条契约本身。"""
    import base64
    import io

    import visual
    from PIL import Image

    class _FakeMessage:
        def __init__(self, content: str) -> None:
            self.content = content

    class _FakeChoice:
        def __init__(self, content: str) -> None:
            self.message = _FakeMessage(content)

    class _FakeResp:
        def __init__(self, content: str) -> None:
            self.choices = [_FakeChoice(content)]

    class _FakeCompletions:
        def create(self, **_kwargs):
            return _FakeResp(
                '[{"mark":1,"title":"视频A","value":"800万"},'
                '{"mark":2,"title":"视频B","value":"1200万"}]'
            )

    class _FakeChat:
        completions = _FakeCompletions()

    class _FakeClient:
        chat = _FakeChat()

    old_client = visual._client
    visual._client = _FakeClient()
    try:
        img = Image.new("RGB", (200, 200), "white")
        buf = io.BytesIO()
        img.save(buf, format="PNG")
        png_b64 = base64.b64encode(buf.getvalue()).decode()
        r = client.post("/agent_extract", json={
            "goal": "播放量最高的视频",
            "screenshot": png_b64,
            "elements": [
                {"id": 1, "text": "视频A 800万播放", "bounds": [0, 0, 100, 50]},
                {"id": 2, "text": "视频B 1200万播放", "bounds": [0, 60, 100, 110]},
            ],
        })
        assert r.status_code == 200, r.text
        d = r.json()
        print(f"  /agent_extract → action={d['action']} mark={d['mark']} reason={d['reason']!r}")
        assert d["action"] == "tap" and d["mark"] == 2, d
    finally:
        visual._client = old_client


def test_agent_plan_multi_shopping_compare() -> None:
    """T-13：商品比价应拆成多个 App 子任务，并声明 REMEMBER/merge 协议。"""
    r = client.post("/agent_plan_multi", json={
        "goal": "帮我对比淘宝和京东 ThinkPad E14 首屏价格，看看哪个便宜",
        "apps": ["淘宝", "京东"],
    })
    assert r.status_code == 200, r.text
    d = r.json()
    apps = [t["app"] for t in d.get("tasks", [])]
    print(f"  /agent_plan_multi 商品比价 → mode={d.get('mode')} apps={apps}")
    assert d["workflow"] is True and d["mode"] == "shopping_compare", d
    assert apps == ["淘宝", "京东"], d
    assert "ThinkPad E14" in d.get("query", ""), d
    assert all(t.get("remember", {}).get("kind") == "product_price" for t in d["tasks"]), d
    assert d.get("merge", {}).get("strategy") == "min", d


def test_agent_plan_multi_travel_compare() -> None:
    """T-13：票价比价应保留日期/出发到达地，并生成各 App 的 REMEMBER key。"""
    r = client.post("/agent_plan_multi", json={
        "goal": "明天北京到哈尔滨机票哪家最便宜",
    })
    assert r.status_code == 200, r.text
    d = r.json()
    apps = [t["app"] for t in d.get("tasks", [])]
    route = d.get("route", {})
    print(f"  /agent_plan_multi 票价比价 → route={route} apps={apps}")
    assert d["workflow"] is True and d["mode"] == "travel_compare", d
    assert route.get("date") == "明天" and route.get("origin") == "北京" and route.get("destination") == "哈尔滨", d
    assert apps[:3] == ["携程", "智行", "去哪儿"], d
    assert all(t.get("remember", {}).get("kind") == "flight_price" for t in d["tasks"]), d


def test_pending_app_task_merge() -> None:
    """T-17⑤：模拟"上一轮问了哪个应用"的状态，验证下一轮短答会合并回原始整句，
    不再丢失"日推第三首"这类目标（呆案例 #001 的核心诉求：答完应用名后目标不能丢）。"""
    import server as _server

    uid = "smoke_pending_merge"
    _server._pending_app_task[uid] = {"text": "打开酷狗音乐日推的第三首歌", "age": 0}
    r = client.post("/interact", json={"user_id": uid, "text": "酷狗音乐"})
    d = r.json()
    print(f"  pending合并 → intent={d['intent']} slots={d['slots']}")
    goal = d["slots"].get("goal", "")
    assert "日推" in goal or "第三首" in goal, f"合并后目标应保留原始整句语义: {d}"
    assert uid not in _server._pending_app_task, "处理完应清掉 pending 状态"


def test_pending_app_task_expiry() -> None:
    """T-17⑤：pending 状态超过 age 上限应作废，不能无限期误吞后续无关短答。"""
    import server as _server

    uid = "smoke_pending_expiry"
    _server._pending_app_task[uid] = {"text": "打开酷狗音乐日推的第三首歌", "age": 99}
    r = client.post("/interact", json={"user_id": uid, "text": "现在几点了"})
    assert r.status_code == 200, r.text
    assert uid not in _server._pending_app_task, "超龄 pending 应在本轮被清掉，不能继续挂着"
    print("  pending过期作废 → OK")


def test_confirmation_retry_does_not_execute() -> None:
    """确认态里"再来一次"是重开视觉采集，不是确认发送；高风险动作不能被这句话误触发。"""
    import server as _server

    uid = "smoke_confirm_retry"
    sess = _server._get_session(uid)
    sess.context.pending_action = {
        "type": "send_message",
        "summary": "通过短信给哥哥发送「空调坏了」",
        "slots": {"recipient": "哥哥", "content": "空调坏了", "app": "短信"},
    }
    r = client.post("/interact", json={"user_id": uid, "text": "再来一次"})
    assert r.status_code == 200, r.text
    d = r.json()
    print(f"  确认态重试 → status={d['status']} action={d.get('device_action')} reply={d['reply']!r}")
    assert d["status"] == "pending_confirmation", d
    assert d.get("device_action") is None, d
    assert sess.context.pending_action is not None, "重试不应清掉待确认动作"


def test_confirmation_copy_matches_machine_status() -> None:
    """机器仍是 chat 时，回复不得声称已经进入等待确认。"""
    import server as _server

    guarded = _server._honest_confirmation_reply(
        "已准备好通过短信发送，等待用户确认", "chat", "发短信说我晚点回家"
    )
    assert "还没有进入确认状态" in guarded, guarded
    assert "等待用户确认" not in guarded, guarded
    pending = _server._honest_confirmation_reply(
        "已准备好通过短信发送，等待用户确认",
        "pending_confirmation",
        "给妈妈发短信说我晚点回家",
    )
    assert "等待用户确认" in pending, pending


def test_orphan_confirmation_never_claims_completion() -> None:
    """无 pending 的孤立确认必须确定性拒绝，不调用 LLM、不生成假完成。"""
    import server as _server

    uid = "smoke_orphan_confirmation"
    _server._get_session(uid).context.pending_action = None
    r = client.post("/interact", json={"user_id": uid, "text": "确认执行"})
    assert r.status_code == 200, r.text
    d = r.json()
    assert d["status"] == "clarify", d
    assert d.get("device_action") is None, d
    assert "没有等待确认" in d["reply"], d
    assert "完成" not in d["reply"], d


def test_expired_high_risk_confirmation_never_claims_completion() -> None:
    """外部会话把确认判成 chat 时，适配层必须覆盖假完成并保持零动作。"""
    import server as _server

    req = _server.InteractRequest(user_id="smoke_expired_confirmation", text="确认执行")
    pending = {
        "type": "send_message",
        "summary": "通过短信给妈妈发送「晚点回家」",
        "slots": {"recipient": "妈妈", "content": "晚点回家", "app": "短信"},
    }
    guarded = _server._expired_high_risk_confirmation_response(
        req, pending, "chitchat", "chat"
    )
    assert guarded is not None, guarded
    assert guarded["status"] == "clarify", guarded
    assert guarded.get("device_action") is None, guarded
    assert "确认窗口已超时" in guarded["reply"], guarded
    assert "完成" not in guarded["reply"], guarded
    assert _server._expired_high_risk_confirmation_response(
        req, pending, "confirm", "executed"
    ) is None


def test_deterministic_mobile_message_pending() -> None:
    """明确短信句式连续多次必须稳定进入 pending，且首轮绝不下发动作。"""
    cases = (
        ("给妈妈发短信说晚点回家", "妈妈", "晚点回家"),
        ("给李娜发送消息：会议推迟", "李娜", "会议推迟"),
        ("给哥哥发送空调坏了的消息", "哥哥", "空调坏了"),
    )
    for round_no in range(5):
        for case_no, (text, recipient, content) in enumerate(cases):
            r = client.post("/interact", json={
                "user_id": f"smoke_deterministic_message_{round_no}_{case_no}",
                "text": text,
            })
            assert r.status_code == 200, r.text
            d = r.json()
            assert d["intent"] == "send_message", d
            assert d["status"] == "pending_confirmation", d
            assert d["slots"]["recipient"] == recipient, d
            assert d["slots"]["content"] == content, d
            assert d.get("device_action") is None, d


def test_mobile_pending_resets_external_age() -> None:
    """适配层新建短信 pending 时不能继承上一次动作的老化计数。"""
    import server as _server

    uid = "smoke_mobile_pending_age_reset"
    sess = _server._get_session(uid)
    sess._pending_age = 99
    sess.context.pending_action = {
        "type": "send_message",
        "summary": "旧动作",
        "slots": {"recipient": "旧联系人", "content": "旧内容"},
    }
    r = client.post("/interact", json={
        "user_id": uid,
        "text": "给妈妈发短信说晚点回家",
    })
    assert r.status_code == 200, r.text
    d = r.json()
    assert d["status"] == "pending_confirmation", d
    assert sess._pending_age == 0, "新 pending 必须从第 0 轮开始计龄"
    assert sess.context.pending_action["slots"]["recipient"] == "妈妈"


def test_high_risk_refusal_blocks_after_nod() -> None:
    """明确“不要”必须在外部融合前拦截 AFTER 点头，0 次 device_action。"""
    import server as _server

    for round_no in range(5):
        uid = f"smoke_high_risk_refusal_{round_no}"
        sess = _server._get_session(uid)
        sess.context.pending_action = {
            "type": "send_message",
            "summary": "通过短信给妈妈发送「晚点回家」",
            "slots": {"recipient": "妈妈", "content": "晚点回家", "app": "短信"},
        }
        r = client.post("/interact", json={
            "user_id": uid,
            "text": "不要",
            "speech_t_start": 1.8,
            "speech_t_end": 3.5,
            "vision": [{
                "kind": "head_motion",
                "label": "nod",
                "confidence": 0.93,
                "t_start": 4.9,
                "t_end": 5.4,
            }],
        })
        assert r.status_code == 200, r.text
        d = r.json()
        assert d["intent"] == "cancel", d
        assert d.get("device_action") is None, d
        assert d["conflict"] is True, d
        assert "高风险硬守卫" in d["conflict_resolution"], d
        assert sess.context.pending_action is None, "取消后不应残留 pending"


def test_high_risk_clipped_refusal_never_executes() -> None:
    """ASR 将“不要”截成单字“要”时，即便看到肯定手势，也只能重问、不能执行。"""
    import server as _server

    uid = "smoke_high_risk_clipped_refusal"
    sess = _server._get_session(uid)
    sess.context.pending_action = {
        "type": "send_message",
        "summary": "通过短信给妈妈发送「晚点回家」",
        "slots": {"recipient": "妈妈", "content": "晚点回家", "app": "短信"},
    }
    r = client.post("/interact", json={
        "user_id": uid,
        "text": "要",
        "speech_t_start": 3.074,
        "speech_t_end": 4.754,
        "vision": [{
            "kind": "gesture",
            "label": "thumb_up",
            "confidence": 0.9,
            "t_start": 1.223,
            "t_end": 1.623,
        }],
    })
    assert r.status_code == 200, r.text
    d = r.json()
    assert d["status"] == "pending_confirmation", d
    assert d["intent"] == "clarify", d
    assert d.get("device_action") is None, d
    assert sess.context.pending_action is not None, "含糊肯定后应保留 pending 供用户重答"


def test_high_risk_asr_refusal_variant_cancels() -> None:
    """真机 ASR 曾把“不要，不要发送”识别成“不用不用发送”，仍应确定性取消。"""
    import server as _server

    uid = "smoke_high_risk_asr_refusal_variant"
    sess = _server._get_session(uid)
    sess.context.pending_action = {
        "type": "send_message",
        "summary": "通过短信给妈妈发送「晚点回家」",
        "slots": {"recipient": "妈妈", "content": "晚点回家", "app": "短信"},
    }
    r = client.post("/interact", json={
        "user_id": uid,
        "text": "不用不用发送",
        "speech_t_start": 3.025,
        "speech_t_end": 6.116,
        "vision": [{
            "kind": "gesture",
            "label": "thumb_up",
            "confidence": 0.9,
            "t_start": 1.108,
            "t_end": 1.508,
        }],
    })
    assert r.status_code == 200, r.text
    d = r.json()
    assert d["intent"] == "cancel", d
    assert d["status"] == "executed", d
    assert d.get("device_action") is None, d
    assert sess.context.pending_action is None, "拒绝同义词后应清空 pending"


def main() -> int:
    print("== /health（离线）==")
    test_health()
    test_agent_reflect_no_change()
    test_reflect_with_screenshot_falls_back_to_text_llm()
    test_demo_to_skill_docs()
    test_trace_to_skill_and_avoid_docs()
    test_extract_pick_parsing()
    test_agent_extract_endpoint()
    test_agent_plan_multi_shopping_compare()
    test_agent_plan_multi_travel_compare()
    test_pending_app_task_merge()
    test_pending_app_task_expiry()
    test_confirmation_retry_does_not_execute()
    test_confirmation_copy_matches_machine_status()
    test_orphan_confirmation_never_claims_completion()
    test_expired_high_risk_confirmation_never_claims_completion()
    test_deterministic_mobile_message_pending()
    test_mobile_pending_resets_external_age()
    test_high_risk_refusal_blocks_after_nod()
    test_high_risk_clipped_refusal_never_executes()
    test_high_risk_asr_refusal_variant_cancels()

    if not os.environ.get("DASHSCOPE_API_KEY"):
        print("\n[跳过] /interact 测试：未设置 DASHSCOPE_API_KEY")
        print("\n冒烟通过（仅 /health）。")
        return 0

    print("\n== /interact（需 API Key + 网络）==")
    test_agent_plan_not_contaminated_by_hints_example()
    test_interact_query_time()
    test_interact_send_confirm()
    test_multiturn_ellipsis_weather()
    test_multiturn_correction_recipient()
    test_multiturn_correction_content()
    test_multiturn_correction_reminder_time()
    test_agent_reflect_text_only_survives_vlm_outage()
    print("\n全部冒烟通过 ✅")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
