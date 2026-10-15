from __future__ import annotations

import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from lingxi.core.config import Config  # noqa: E402
from lingxi.core.schemas import MultimodalFrame, TextInput, VisionEvent, VisionKind  # noqa: E402
from lingxi.intent.context import DialogueContext  # noqa: E402
from lingxi.intent.engine import (  # noqa: E402
    IntentEngine,
    _mobile_colloquial_weather_request,
)
from lingxi.response.actions import ActionExecutor, MockTerminal  # noqa: E402


MISSING_CITY_POSITIVES = (
    "外头冷不冷", "外边现在暖和不", "室外是不是很热", "户外有点凉吗",
    "出门会不会冷", "外面现在闷热吗", "请问外边凉快吗", "帮我看看外头热不热",
    "天儿怎么这么冷", "天好像挺热的", "天看起来有点冷", "天似乎挺暖和",
    "今天外面冷不冷", "今晚外头会凉吗", "现在外面暖和吗", "目前室外热不热",
    "此刻户外冷吗", "我想知道外面是不是凉快",
)

EXPLICIT_CITY_POSITIVES = {
    "杭州外面暖和吗": "杭州",
    "北京外头冷不冷": "北京",
    "上海室外是不是很热": "上海",
    "广州户外闷热吗": "广州",
    "深圳外边凉快吗": "深圳",
    "成都今天外面冷吗": "成都",
    "武汉现在室外热不热": "武汉",
    "西安外面暖和不": "西安",
    "青岛天儿冷不冷": "青岛",
    "南京今天热吗": "南京",
    "苏州外头是不是有点凉": "苏州",
    "重庆现在外面很热吗": "重庆",
}

HARD_NEGATIVES = (
    "空调有点冷", "房间里太热了", "宿舍里暖和吗", "手机怎么这么热",
    "今天我的手机很热", "电脑发热严重", "电池有点烫", "发动机是不是过热",
    "冰箱不冷了", "水凉了吗", "汤还是热的吗", "咖啡有点凉",
    "给我讲个冷笑话", "说一个冷知识", "这个话题很热门", "这条新闻上热搜了吗",
    "他最近对我很冷淡", "我心里有点冷", "对我热情一点", "这是冷色还是暖色",
    "把空调温度调低", "帮我关闭暖气", "天热时帮我开空调", "天冷了提醒我穿外套",
    "如果明天冷就给妈妈发短信", "天冷的时候推荐一首歌", "外面热，给我讲个笑话",
    "外面的手机摸着很热", "室外空调机很热", "我的手脚有点冷", "我是不是发烧了",
    "下个月会比现在暖和吗",
)

EXPLICIT_DELEGATES = (
    "杭州下周会更暖和吗",
    "北京和上海外面哪个更冷",
    "杭州外面的手机很热",
    "杭州外面热，给我讲个笑话",
)


class _RecordingLlm:
    def __init__(self) -> None:
        self.json_calls = 0

    def chat_json(self, *_args, **_kwargs) -> dict:
        self.json_calls += 1
        return {
            "intent": "chitchat",
            "slots": {},
            "confidence": 0.9,
            "conflict": False,
            "conflict_resolution": "",
            "need_clarification": False,
            "clarify_question": "",
            "domain": "chitchat",
            "reasoning": "delegate fixture",
            "direct_reply": "已交给原意图链。",
        }


def _cfg() -> Config:
    return Config({
        "intent": {
            "confidence_threshold": 0.6,
            "temperature": 0.1,
            "high_risk_actions": ["send_message", "make_call", "send_email"],
            "device_capabilities": ["navigation"],
            "use_domain_router": False,
            "repair_invalid_json": False,
        },
        "response": {"temperature": 0.7},
    })


def _frame(text: str, *, visual: bool = False) -> MultimodalFrame:
    events = []
    if visual:
        events.append(VisionEvent(
            t_start=0.0,
            t_end=0.5,
            confidence=0.95,
            kind=VisionKind.HEAD_MOTION,
            label="nod",
        ))
    return MultimodalFrame(
        anchor=TextInput(t_start=0.0, t_end=0.0, text=text),
        vision_events=events,
        vision_summary=[],
    )


class CleanCloudColloquialWeatherTests(unittest.TestCase):
    def test_missing_city_cases_route_to_weather_then_clarify_without_llm(self) -> None:
        for text in MISSING_CITY_POSITIVES:
            with self.subTest(text=text):
                llm = _RecordingLlm()
                result = IntentEngine(_cfg(), llm=llm).understand(
                    _frame(text), DialogueContext()
                )
                self.assertEqual("query_weather", result.intent)
                self.assertEqual({"location": "", "date": "今天"}, result.slots)
                self.assertEqual(0, llm.json_calls)
                outcome = ActionExecutor(MockTerminal()).execute(result)
                self.assertEqual("clarify", outcome.status)
                self.assertIn("城市", outcome.feedback)

    def test_explicit_city_cases_execute_weather_without_llm(self) -> None:
        for text, city in EXPLICIT_CITY_POSITIVES.items():
            with self.subTest(text=text):
                llm = _RecordingLlm()
                result = IntentEngine(_cfg(), llm=llm).understand(
                    _frame(text), DialogueContext()
                )
                self.assertEqual("query_weather", result.intent)
                self.assertEqual({"location": city, "date": "今天"}, result.slots)
                self.assertEqual(0, llm.json_calls)
                outcome = ActionExecutor(MockTerminal()).execute(result)
                self.assertEqual("executed", outcome.status)

    def test_hard_negatives_never_force_weather(self) -> None:
        for text in HARD_NEGATIVES:
            with self.subTest(text=text):
                self.assertIsNone(_mobile_colloquial_weather_request(text))

    def test_future_multi_city_and_compound_cases_delegate(self) -> None:
        for text in EXPLICIT_DELEGATES:
            with self.subTest(text=text):
                llm = _RecordingLlm()
                result = IntentEngine(_cfg(), llm=llm).understand(
                    _frame(text), DialogueContext()
                )
                self.assertEqual("chitchat", result.intent)
                self.assertEqual(1, llm.json_calls)

    def test_pending_and_visual_never_use_the_fast_path(self) -> None:
        cases = (
            (_frame("杭州外面暖和吗"), DialogueContext(pending_action={"type": "send_message"})),
            (_frame("杭州外面暖和吗", visual=True), DialogueContext()),
        )
        for frame, context in cases:
            with self.subTest(text=frame.anchor_text):
                llm = _RecordingLlm()
                IntentEngine(_cfg(), llm=llm).understand(frame, context)
                self.assertEqual(1, llm.json_calls)

    def test_pc_profile_never_uses_the_mobile_fast_path(self) -> None:
        cfg = _cfg()
        cfg.raw()["intent"]["device_capabilities"] = []
        llm = _RecordingLlm()
        IntentEngine(cfg, llm=llm).understand(
            _frame("杭州外面暖和吗"), DialogueContext()
        )
        self.assertEqual(1, llm.json_calls)


if __name__ == "__main__":
    unittest.main()
