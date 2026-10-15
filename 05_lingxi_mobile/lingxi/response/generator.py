"""个性化应答生成器 —— IntentResult + 画像 + 上下文 → 一句自然中文应答。

个性化体现在三层（方案文档论点）：
  内容层：联系人消歧、习惯性偏好补全发生在意图阶段（画像注入 DialogueContext）
  表达层：称呼、长短、语气、emoji 由画像 style 控制（本模块）
  行为层：确认/澄清话术随用户习惯收敛（阶段7+）

延时注：任务类使用确定性执行回执；闲聊优先复用意图JSON中的 direct_reply。
正常轮次最多一次 LLM，旧模型缺失 direct_reply 时才保留闲聊兼容兜底。
"""
from __future__ import annotations

import json
import time

from ..core.config import Config
from ..core.log import get_logger
from ..intent.context import DialogueContext
from ..intent.schema import INTENTS, IntentResult
from ..llm import LLMClient, LLMError
from ..persona.profile import UserProfile

logger = get_logger("response.generator")

SYSTEM_PROMPT = """你是智能终端语音助手「灵犀」的应答生成器。根据已确定的用户意图与用户画像，生成一句要对用户说的中文应答。

要求：
1. 内容：确认即将执行的动作及关键参数（收件人/内容/时间等），查询类直接给出查询动作的反馈；need_clarification=true 时输出澄清反问。
2. 风格严格遵循画像：有称呼则自然带上；长度按 reply_length（简短≤20字，适中≤40字，详细≤70字）；语气按 tone（活泼/自然/正式）；use_emoji=false 时绝不使用任何 emoji 或颜文字。
3. 像真人助手说话，不要机械复述槽位，不要解释你的思考。
4. 应答内容只能基于给定的意图槽位与上下文，**绝不自行猜测收件人、时间等关键参数**。
5. 近期对话仅用于理解指代与省略（如"再发一条""换成八点"），**回应只针对本轮的动作与执行回执，绝不复读历史里已经说过的话**。
6. 只输出应答文本本身。"""

CHAT_SYSTEM_PROMPT = """你是智能终端助手「灵犀」，此刻在和用户自然聊天。

要求：
1. 像一个温暖、机灵的朋友那样回应——简短自然、有来有往，不堆客套、不长篇大论。
2. 严格遵循用户画像：有称呼则自然带上；语气按 tone（活泼/自然/正式）；长度按 reply_length（简短≤25字 / 适中≤45字 / 详细≤80字）；use_emoji=false 时绝不用任何 emoji 或颜文字。
3. 参考近期对话保持连贯（理解指代、不重复追问已知信息），但回应只针对用户最新这句话。**绝不重复你之前已经说过的话——每一句回应都必须是新的内容；用户换话题或追问时，务必给出不一样的回应，哪怕意思相近也要换说法。**
4. 你能帮用户发消息、打电话、查天气与日程、设提醒、放音乐、开应用——仅当用户自然流露相关需求时才顺势提一句，绝不硬推销。
5. 不知道就坦诚说不知道，绝不编造事实。
6. 你只负责聊天，**绝不声称已执行或正在执行任何终端操作**（发消息、打电话、设提醒等都由系统的任务流程负责）；若上下文里有未完成的「待确认动作」，可自然提醒用户它还等着确认。
7. 只输出要对用户说的那句话本身，不要任何前缀、解释或引号。"""


class ResponseGenerator:
    def __init__(self, cfg: Config, llm: LLMClient | None = None) -> None:
        self.llm = llm or LLMClient(cfg)
        self.temperature = float(cfg.get("response.temperature", 0.7))
        self.allow_chitchat_reply_fallback = bool(
            cfg.get("response.allow_chitchat_reply_fallback", True)
        )

    def generate(
        self,
        result: IntentResult,
        profile: UserProfile,
        context: DialogueContext,
        action_feedback: str = "",
        user_text: str = "",
    ) -> str:
        """user_text: 用户本轮原话（对话通路必需）；action_feedback: 执行回执。"""
        is_chat = result.intent == "chitchat"
        # 任务结果已经由槽位校验+确定性执行器生成。再次交给LLM复述不仅多一次串行延迟，
        # 还可能被历史带偏、说错收件人/查询结果。澄清问题同样直接使用意图层已校验字段。
        if not is_chat:
            if result.need_clarification:
                return result.clarify_question or "抱歉，我没太明白你的意思，能再说一遍吗？"
            if action_feedback:
                return action_feedback
            return f"好的，这就{INTENTS.get(result.intent, ('处理', {}))[0]}。"

        # 新契约把闲聊回答合并进同一次结构化意图调用；旧fixture/旧兼容模型缺字段时
        # 才回落到原有第二次生成，避免一次性破坏离线研究轨。
        if result.direct_reply:
            return result.direct_reply.strip().strip('"「」')
        if not self.allow_chitchat_reply_fallback:
            return "我在，想聊什么都可以。"

        t0 = time.perf_counter()
        try:
            text = self._gen_chat(user_text, profile, context)
        except LLMError as e:
            logger.error("应答生成失败，使用模板兜底: %s", e)
            text = "嗯，我在听。"
        latency = time.perf_counter() - t0
        logger.info("应答(%.2fs)[%s]: %s", latency, "chat" if is_chat else "task", text)
        return text

    def _gen_chat(self, user_text: str, profile: UserProfile, context: DialogueContext) -> str:
        # 历史作为真正的多轮对话消息传入（而非塞进文本块），LLM 才不会照抄上一句回复
        sys_prompt = CHAT_SYSTEM_PROMPT + f"\n\n【你的人设/画像】{profile.to_prompt_text()}"
        ctx_brief = context.to_prompt_text(include_history=False)  # 界面/待确认动作，不含历史
        if ctx_brief:
            sys_prompt += f"\n【当前情境】{ctx_brief}"
        messages = [{"role": "system", "content": sys_prompt}]
        for turn in context.history[-context.max_history:]:
            role = "assistant" if turn["role"] == "assistant" else "user"
            messages.append({"role": role, "content": turn["text"]})
        messages.append({"role": "user", "content": user_text or "（无文本）"})
        return self.llm.chat(
            messages, temperature=0.85, max_tokens=220,
        ).strip().strip('"「」')

    def _gen_task(self, result: IntentResult, profile: UserProfile,
                  context: DialogueContext, action_feedback: str) -> str:
        intent_zh = INTENTS.get(result.intent, ("未知", {}))[0]
        payload = {
            "intent": result.intent, "intent_zh": intent_zh, "slots": result.slots,
            "need_clarification": result.need_clarification,
            "clarify_question": result.clarify_question,
            "conflict": result.conflict, "conflict_resolution": result.conflict_resolution,
        }
        # 任务类也带历史（理解"再发一条"等多轮指代）；SYSTEM_PROMPT 第5条约束防复读
        user_content = (
            f"【本轮意图】{json.dumps(payload, ensure_ascii=False)}\n"
            f"【执行回执】{action_feedback or '无'}\n"
            f"【画像】{profile.to_prompt_text()}\n"
            f"【上下文】{context.to_prompt_text(include_history=True)}"
        )
        return self.llm.chat(
            [{"role": "system", "content": SYSTEM_PROMPT},
             {"role": "user", "content": user_content}],
            temperature=self.temperature, max_tokens=120,
        ).strip().strip('"「」')
