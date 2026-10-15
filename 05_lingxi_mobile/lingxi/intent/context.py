"""对话上下文与模拟界面状态 —— 意图理解的第三类输入（文本模态/上下文）。

界面状态(screen)是端侧智能体的关键信息源：当前应用、聊天对象、草稿等，
支撑指代消解（"给他发消息"的"他"=当前聊天对象）。
真实终端集成时由系统API填充；当前用 mock 数据驱动开发与评测。
"""
from __future__ import annotations

from dataclasses import dataclass, field


@dataclass
class DialogueContext:
    screen: dict = field(default_factory=dict)        # 如 {"app":"微信","view":"聊天-妈妈","draft":"..."}
    history: list[dict] = field(default_factory=list)  # [{"role":"user"/"assistant","text":...}]
    pending_action: dict | None = None                 # 待确认动作 {"type":..., "summary":...}
    profile_summary: str = ""                          # 用户画像摘要(persona层注入)，用于联系人消歧等
    gaze_target: str | None = None                     # 注视焦点：用户此刻看向的屏幕元素(联系人/App)，多模态指代消解证据
    max_history: int = 6

    def add_turn(self, role: str, text: str) -> None:
        self.history.append({"role": role, "text": text})
        if len(self.history) > self.max_history * 2:
            self.history = self.history[-self.max_history * 2:]

    def to_prompt_text(self, include_history: bool = True) -> str:
        lines = []
        if self.screen:
            desc = ", ".join(f"{k}={v}" for k, v in self.screen.items())
            lines.append(f"[当前界面] {desc}")
        else:
            lines.append("[当前界面] 桌面/未知")
        if self.gaze_target:
            lines.append(f"[注视焦点] 用户此刻正看向：{self.gaze_target}")
        if self.pending_action:
            lines.append(
                f"[待确认动作] {self.pending_action.get('type')}: "
                f"{self.pending_action.get('summary')}"
            )
        if self.profile_summary:
            lines.append(self.profile_summary)
        if include_history and self.history:
            recent = self.history[-self.max_history:]
            h = " | ".join(f"{'用户' if t['role'] == 'user' else '助手'}:{t['text']}" for t in recent)
            lines.append(f"[近期对话] {h}")
        return "\n".join(lines)
