"""P1-1 域路由小模型推理封装 —— 端侧 CPU 毫秒级"能力边界守门员"。

加载 data/models/domain_router.pkl（TF-IDF char n-gram + 逻辑回归，训练见
eval/train_domain_router.py）。判定一句话属于哪个领域、是否超出设备能力(OOS)。
高置信 OOS 由引擎前置拦截 → web_search 兜底，不调大模型（省延时/API/上云数据）。
可独立加载（pkl 内含 vectorizer+clf+OOS 域表，无需 jieba/训练脚本）。
"""
from __future__ import annotations

import pickle
from pathlib import Path

from ..core.log import get_logger

logger = get_logger("intent.router")


class DomainRouter:
    def __init__(self, model_path: str | Path) -> None:
        with open(model_path, "rb") as f:
            d = pickle.load(f)
        self.vec = d["vectorizer"]
        self.clf = d["clf"]
        self.oos = set(d["oos_smp"])
        self._classes = list(self.clf.classes_)
        self._oos_idx = [i for i, c in enumerate(self._classes) if c in self.oos]
        logger.info("域路由模型已加载: %d 域, OOS 域 %d 个", len(self._classes), len(self.oos))

    def predict(self, text: str) -> dict:
        """返回 {domain: 预测域, is_oos: 顶域是否OOS, p_oos: OOS域概率和, top_p: 顶域概率}。"""
        proba = self.clf.predict_proba(self.vec.transform([text]))[0]
        top_i = int(proba.argmax())
        return {
            "domain": self._classes[top_i],
            "is_oos": self._classes[top_i] in self.oos,
            "p_oos": round(float(sum(proba[i] for i in self._oos_idx)), 3),
            "top_p": round(float(proba[top_i]), 3),
        }

    @classmethod
    def load_default(cls, root: str | Path) -> "DomainRouter | None":
        p = Path(root) / "data" / "models" / "domain_router.pkl"
        if not p.exists():
            return None
        try:
            return cls(p)
        except Exception as e:  # noqa: BLE001 — 模型损坏不应让引擎崩，退回纯 LLM
            logger.warning("域路由模型加载失败，跳过前置路由: %s", e)
            return None
