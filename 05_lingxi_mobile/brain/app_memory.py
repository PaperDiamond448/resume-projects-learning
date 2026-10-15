"""Self-filling App skill memory for Lingxi Mobile.

This is deliberately conservative: it stores only textual task traces
(app name, goal, subgoals, text actions), never screenshots/audio/face data.
Successful runs can be reused as skills; failed runs are kept as diagnostics.
"""
from __future__ import annotations

import json
import re
import threading
import time
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parent
DATA_FILE = ROOT / "data" / "app_memory.json"
DEMO_FILE = ROOT / "data" / "app_demos.json"   # 真人演示（教学模式）——元素级可回放步骤，最高优先
SKILL_FILE = ROOT / "data" / "app_skills.json"  # 从真人示教/成功轨迹沉淀出的控件级技能文档
_lock = threading.Lock()


def _now() -> str:
    return time.strftime("%Y-%m-%d %H:%M:%S")


def _norm(s: str) -> str:
    return re.sub(r"\s+", "", (s or "").strip().lower())


def _bigrams(s: str) -> set[str]:
    s = _norm(s)
    if not s:
        return set()
    if len(s) == 1:
        return {s}
    return {s[i:i + 2] for i in range(len(s) - 1)}


def _goal_score(a: str, b: str) -> float:
    na, nb = _norm(a), _norm(b)
    if not na or not nb:
        return 0.0
    if na == nb:
        return 1.0
    if na in nb or nb in na:
        return 0.88
    ba, bb = _bigrams(na), _bigrams(nb)
    if not ba or not bb:
        return 0.0
    return len(ba & bb) / max(1, len(ba | bb))


def _load() -> dict[str, Any]:
    if not DATA_FILE.exists():
        return {"version": 1, "entries": []}
    try:
        return json.loads(DATA_FILE.read_text(encoding="utf-8"))
    except Exception:
        return {"version": 1, "entries": []}


def _save(data: dict[str, Any]) -> None:
    DATA_FILE.parent.mkdir(parents=True, exist_ok=True)
    tmp = DATA_FILE.with_suffix(".tmp")
    tmp.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")
    tmp.replace(DATA_FILE)


def lookup(app: str, goal: str, min_score: float = 0.55) -> dict[str, Any] | None:
    """Return the best successful memory for a similar app+goal."""
    q_app = _norm(app)
    best: tuple[float, dict[str, Any]] | None = None
    with _lock:
        data = _load()
        for e in data.get("entries", []):
            if int(e.get("successes", 0)) <= 0:
                continue
            if q_app and _norm(e.get("app", "")) != q_app:
                continue
            steps = [str(s).strip() for s in e.get("steps", []) if str(s).strip()]
            if not steps:
                continue
            score = _goal_score(goal, e.get("goal", ""))
            if score >= min_score and (best is None or score > best[0]):
                best = (score, e)
    if best is None:
        return None
    score, entry = best
    return {
        "app": entry.get("app", ""),
        "goal": entry.get("goal", ""),
        "steps": list(entry.get("steps", [])),
        "score": round(score, 3),
        "successes": int(entry.get("successes", 0)),
        "failures": int(entry.get("failures", 0)),
        "last_history": list(entry.get("last_history", [])),
        "last_trace": list(entry.get("last_trace", [])),
    }


def record(
    app: str,
    goal: str,
    steps: list[str],
    history: list[str],
    trace: list[dict] | None,
    success: bool,
    note: str = "",
) -> dict[str, Any]:
    """Merge a run into memory. Successful runs become reusable skills."""
    clean_steps = [str(s).strip() for s in steps if str(s).strip()][:8]
    clean_history = [str(h).strip() for h in history if str(h).strip()][-20:]
    clean_trace = []
    for item in (trace or [])[-40:]:
        if not isinstance(item, dict):
            continue
        clean_trace.append({
            "subgoal": str(item.get("subgoal", ""))[:120],
            "thought": str(item.get("thought", ""))[:160],
            "action": str(item.get("action", ""))[:40],
            "target": str(item.get("target", ""))[:80],
            "beforeHash": item.get("beforeHash", 0),
            "afterHash": item.get("afterHash", 0),
            "changed": item.get("beforeHash", 0) != item.get("afterHash", 0),
            "done": bool(item.get("done", False)),
            "reason": str(item.get("reason", ""))[:80],
        })
    app = (app or "").strip()
    goal = (goal or "").strip()
    note = (note or "").strip()[:120]
    if not app and not goal:
        return {"ok": False, "reason": "empty"}

    with _lock:
        data = _load()
        entries = data.setdefault("entries", [])
        hit = None
        for e in entries:
            same_app = _norm(e.get("app", "")) == _norm(app)
            same_goal = _goal_score(goal, e.get("goal", "")) >= 0.9
            if same_app and same_goal:
                hit = e
                break
        if hit is None:
            hit = {
                "id": f"skill_{int(time.time() * 1000)}",
                "app": app,
                "goal": goal,
                "steps": clean_steps,
                "successes": 0,
                "failures": 0,
                "created_at": _now(),
            }
            entries.append(hit)

        if success and clean_steps:
            hit["steps"] = clean_steps
            hit["successes"] = int(hit.get("successes", 0)) + 1
        else:
            hit["failures"] = int(hit.get("failures", 0)) + 1
        hit["last_history"] = clean_history
        hit["last_trace"] = clean_trace
        hit["last_note"] = note
        hit["updated_at"] = _now()
        docs = _upsert_trace_skills(app, goal, hit["id"], clean_trace, success, note)
        hit["skill_docs"] = [d["id"] for d in docs if d.get("kind") == "positive"]
        hit["avoid_docs"] = [d["id"] for d in docs if d.get("kind") == "avoid"]
        _save(data)
        return {
            "ok": True,
            "id": hit["id"],
            "successes": hit["successes"],
            "failures": hit["failures"],
            "skill_docs": len(hit["skill_docs"]),
            "avoid_docs": len(hit["avoid_docs"]),
        }


def list_entries(limit: int = 50) -> list[dict[str, Any]]:
    with _lock:
        entries = list(_load().get("entries", []))
    entries.sort(key=lambda e: e.get("updated_at", ""), reverse=True)
    return entries[:limit]


# —— 真人演示（教学模式）：用户手动示范一次，记录元素级步骤，之后【确定性回放】——
# 隐私：只存元素文字/坐标/页面文本签名，绝不存截图/音频/人脸（与技能库同口径）。
def _load_demos() -> dict[str, Any]:
    if not DEMO_FILE.exists():
        return {"version": 1, "demos": []}
    try:
        return json.loads(DEMO_FILE.read_text(encoding="utf-8"))
    except Exception:  # noqa: BLE001
        return {"version": 1, "demos": []}


def _save_demos(data: dict[str, Any]) -> None:
    DEMO_FILE.parent.mkdir(parents=True, exist_ok=True)
    tmp = DEMO_FILE.with_suffix(".tmp")
    tmp.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")
    tmp.replace(DEMO_FILE)


def _load_skills() -> dict[str, Any]:
    if not SKILL_FILE.exists():
        return {"version": 1, "skills": []}
    try:
        return json.loads(SKILL_FILE.read_text(encoding="utf-8"))
    except Exception:  # noqa: BLE001
        return {"version": 1, "skills": []}


def _save_skills(data: dict[str, Any]) -> None:
    SKILL_FILE.parent.mkdir(parents=True, exist_ok=True)
    tmp = SKILL_FILE.with_suffix(".tmp")
    tmp.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")
    tmp.replace(SKILL_FILE)


def _clean_demo_steps(steps: list[dict]) -> list[dict[str, Any]]:
    out: list[dict[str, Any]] = []
    for s in (steps or [])[:30]:
        if not isinstance(s, dict):
            continue
        bounds = s.get("bounds") or []
        if not (isinstance(bounds, list) and len(bounds) == 4):
            bounds = [0, 0, 0, 0]
        out.append({
            "action": str(s.get("action", "tap"))[:12] or "tap",
            "text": str(s.get("text", ""))[:60],
            "bounds": [int(x) for x in bounds],
            "sig": str(s.get("sig", ""))[:300],   # 这步执行前页面文本签名→回放时校验是否在对的页
        })
    return out


_SIG_STOPWORDS = {
    "按钮", "标签", "搜索", "登录", "消息", "首页", "我的", "推荐", "热门", "广告",
    "正在缓冲", "ACD_Underlay", "ACD_BufferingView",
}


def _clean_label(text: str) -> str:
    """把 a11y 冗长文本压成可复用控件名，如 '直播,7之1,标签' → '直播'。"""
    raw = (text or "").strip()
    if not raw:
        return "内容卡片"
    first = re.split(r"[,，\n]", raw, maxsplit=1)[0].strip()
    return first[:30] or raw[:30]


def _sig_terms(sig: str, limit: int = 12) -> list[str]:
    """从页面签名里取少量可检索关键词。只存文本关键词，不存截图。"""
    terms: list[str] = []
    for part in re.split(r"[\s,，。|/]+", sig or ""):
        p = part.strip()
        if not (2 <= len(p) <= 24):
            continue
        if p in _SIG_STOPWORDS or p.isdigit():
            continue
        if re.fullmatch(r"[\d.万]+", p):
            continue
        if p not in terms:
            terms.append(p)
        if len(terms) >= limit:
            break
    return terms


def _infer_purpose(goal: str, label: str, step: dict[str, Any], index: int) -> str:
    goal_l = goal or ""
    label_l = label or ""
    action = step.get("action", "tap")
    if action == "scroll":
        return "翻到更多候选内容"
    if "直播" in goal_l and "直播" in label_l and index == 0:
        return "进入直播栏目"
    if "直播间" in goal_l and (index > 0 or label_l == "内容卡片" or len(str(step.get("text", ""))) > 40):
        return "进入目标直播间卡片"
    if any(k in goal_l for k in ("视频", "播放", "看")) and (label_l == "内容卡片" or len(str(step.get("text", ""))) > 40):
        return "打开目标视频/内容卡片"
    if any(k in label_l for k in ("每日推荐", "日推")):
        return "进入每日推荐入口"
    if label_l in ("+", "新建", "新建笔记"):
        return "新建内容"
    return f"执行“{label_l}”相关功能"


def _derive_demo_skills(app: str, goal: str, demo_id: str, steps: list[dict[str, Any]]) -> list[dict[str, Any]]:
    docs: list[dict[str, Any]] = []
    for idx, step in enumerate(steps):
        label = _clean_label(str(step.get("text", "")))
        purpose = _infer_purpose(goal, label, step, idx)
        terms = _sig_terms(str(step.get("sig", "")))
        action = str(step.get("action", "tap"))[:12] or "tap"
        doc = (
            f"示教技能：在任务“{goal}”中，第{idx + 1}步对“{label}”执行 {action}，"
            f"用途是：{purpose}。"
        )
        if terms:
            doc += " 执行前页面常见关键词：" + "、".join(terms[:8]) + "。"
        docs.append({
            "id": f"skilldoc_{demo_id}_{idx + 1}",
            "source": "demo",
            "demo_id": demo_id,
            "app": app,
            "goal": goal,
            "step_index": idx + 1,
            "action": action,
            "label": label,
            "element_text": str(step.get("text", ""))[:120],
            "bounds": list(step.get("bounds", []))[:4],
            "page_terms": terms,
            "purpose": purpose,
            "doc": doc,
            "kind": "positive",
            "confidence": 0.72,
            "updated_at": _now(),
        })
    return docs


def _upsert_demo_skills(app: str, goal: str, demo_id: str, steps: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """把示教轨迹同步沉淀为控件级技能文档。同 demo 覆盖，避免重复污染。"""
    data = _load_skills()
    skills = [s for s in data.get("skills", []) if s.get("demo_id") != demo_id]
    docs = _derive_demo_skills(app, goal, demo_id, steps)
    skills.extend(docs)
    data["skills"] = skills[-500:]
    _save_skills(data)
    return docs


def _action_label(item: dict[str, Any]) -> str:
    target = str(item.get("target", "")).strip()
    action = str(item.get("action", "")).strip()
    if target:
        return _clean_label(target)
    if action.startswith("vision"):
        return "视觉候选"
    if action.startswith("recover:"):
        return action.replace("recover:", "恢复动作:")
    return action or "动作"


def _derive_trace_skills(
    app: str,
    goal: str,
    memory_id: str,
    trace: list[dict[str, Any]],
    success: bool,
    note: str = "",
) -> list[dict[str, Any]]:
    """从成功/失败 run_agent trace 沉淀技能文档。失败只生成 avoid 避坑。"""
    docs: list[dict[str, Any]] = []
    clean_trace = [t for t in trace if isinstance(t, dict)]
    if success:
        usable = [
            t for t in clean_trace
            if str(t.get("action", "")) not in ("", "none")
            and not str(t.get("action", "")).startswith("recover:")
            and not str(t.get("action", "")).startswith("vision_not_ready")
            and bool(t.get("changed", t.get("beforeHash", 0) != t.get("afterHash", 0)))
        ][:10]
        for idx, item in enumerate(usable):
            action = str(item.get("action", ""))[:40]
            label = _action_label(item)
            subgoal = str(item.get("subgoal", ""))[:120] or goal
            reason = str(item.get("reason", ""))[:60]
            doc = (
                f"成功技能：在任务“{goal}”的子步骤“{subgoal}”中，"
                f"对“{label}”执行 {action} 后页面发生有效变化。"
            )
            if reason:
                doc += f" 线索：{reason}。"
            docs.append({
                "id": f"skilldoc_{memory_id}_success_{idx + 1}",
                "source": "success_trace",
                "memory_id": memory_id,
                "app": app,
                "goal": goal,
                "step_index": idx + 1,
                "action": action,
                "label": label,
                "element_text": str(item.get("target", ""))[:120],
                "bounds": [],
                "page_terms": _sig_terms(" ".join([subgoal, reason, str(item.get("thought", ""))]), 10),
                "purpose": subgoal,
                "doc": doc,
                "kind": "positive",
                "confidence": 0.58,
                "updated_at": _now(),
            })
    else:
        bad = [
            t for t in clean_trace
            if str(t.get("action", "")).startswith("recover:")
            or not bool(t.get("changed", t.get("beforeHash", 0) != t.get("afterHash", 0)))
            or str(t.get("reason", "")).strip()
        ][-8:]
        if not bad and note:
            bad = [{"subgoal": goal, "action": "failed", "reason": note}]
        for idx, item in enumerate(bad):
            action = str(item.get("action", "failed"))[:40]
            label = _action_label(item)
            subgoal = str(item.get("subgoal", ""))[:120] or goal
            reason = (str(item.get("reason", "")) or note or "失败路径")[:80]
            doc = (
                f"避坑经验：任务“{goal}”在子步骤“{subgoal}”中，"
                f"动作“{action} / {label}”未可靠完成。下次不要反复走同一路径，"
                "优先返回、换入口、使用示教技能或请求用户教学。"
            )
            if reason:
                doc += f" 原因：{reason}。"
            docs.append({
                "id": f"skilldoc_{memory_id}_avoid_{idx + 1}",
                "source": "failure_trace",
                "memory_id": memory_id,
                "app": app,
                "goal": goal,
                "step_index": idx + 1,
                "action": action,
                "label": label,
                "element_text": str(item.get("target", ""))[:120],
                "bounds": [],
                "page_terms": _sig_terms(" ".join([subgoal, reason, note]), 10),
                "purpose": "避免失败路径",
                "doc": doc,
                "kind": "avoid",
                "confidence": 0.5,
                "updated_at": _now(),
            })
    return docs


def _upsert_trace_skills(
    app: str,
    goal: str,
    memory_id: str,
    trace: list[dict[str, Any]],
    success: bool,
    note: str = "",
) -> list[dict[str, Any]]:
    data = _load_skills()
    replace_source = "success_trace" if success else "failure_trace"
    skills = [
        s for s in data.get("skills", [])
        if not (s.get("memory_id") == memory_id and s.get("source") == replace_source)
    ]
    docs = _derive_trace_skills(app, goal, memory_id, trace, success, note)
    skills.extend(docs)
    data["skills"] = skills[-700:]
    _save_skills(data)
    return docs


def record_demo(app: str, goal: str, steps: list[dict]) -> dict[str, Any]:
    """存一条真人演示。同 (app, 相近 goal) 覆盖（最新示范为准）。"""
    app = (app or "").strip()
    goal = (goal or "").strip()
    clean = _clean_demo_steps(steps)
    if not goal or not clean:
        return {"ok": False, "reason": "empty goal or steps"}
    with _lock:
        data = _load_demos()
        demos = data.setdefault("demos", [])
        hit = None
        for d in demos:
            if _norm(d.get("app", "")) == _norm(app) and _goal_score(goal, d.get("goal", "")) >= 0.9:
                hit = d
                break
        if hit is None:
            hit = {"id": f"demo_{int(time.time() * 1000)}", "app": app, "goal": goal,
                   "created_at": _now()}
            demos.append(hit)
        hit["steps"] = clean
        hit["updated_at"] = _now()
        docs = _upsert_demo_skills(app, goal, hit["id"], clean)
        hit["skill_docs"] = [d["id"] for d in docs]
        _save_demos(data)
        return {"ok": True, "id": hit["id"], "steps": len(clean), "skill_docs": len(docs)}


def lookup_demo(app: str, goal: str, min_score: float = 0.6) -> dict[str, Any] | None:
    """按相近 goal 找最佳真人演示（v0 主要按 goal 匹配——教学时只知包名、回放传 App 名，
    硬过滤 app 会失配；单用户教学场景下 goal 短语足够区分）。返回 {app, goal, steps, score} 或 None。"""
    best: tuple[float, dict[str, Any]] | None = None
    with _lock:
        data = _load_demos()
        for d in data.get("demos", []):
            if not d.get("steps"):
                continue
            score = _goal_score(goal, d.get("goal", ""))
            if score >= min_score and (best is None or score > best[0]):
                best = (score, d)
    if best is None:
        return None
    score, d = best
    return {"app": d.get("app", ""), "goal": d.get("goal", ""),
            "steps": list(d.get("steps", [])), "score": round(score, 3)}


def list_demos(limit: int = 50) -> list[dict[str, Any]]:
    with _lock:
        demos = list(_load_demos().get("demos", []))
    demos.sort(key=lambda d: d.get("updated_at", ""), reverse=True)
    return demos[:limit]


def lookup_skill_docs(app: str, goal: str, screen: str = "", limit: int = 6,
                      min_score: float = 0.25) -> list[dict[str, Any]]:
    """检索控件级技能文档。用于 planning / visual / reflection prompt 注入。"""
    q_app = _norm(app)
    q_goal = goal or ""
    screen_terms = set(_sig_terms(screen, limit=30))
    hits: list[tuple[float, dict[str, Any]]] = []
    with _lock:
        data = _load_skills()
        for s in data.get("skills", []):
            score = _goal_score(q_goal, s.get("goal", "")) * 0.65
            if q_app and _norm(s.get("app", "")) == q_app:
                score += 0.2
            elif not s.get("app"):
                score += 0.05
            label = str(s.get("label", ""))
            purpose = str(s.get("purpose", ""))
            kind = str(s.get("kind", "positive"))
            if label and label in q_goal:
                score += 0.12
            if purpose and any(k in q_goal for k in re.findall(r"[\u4e00-\u9fff]{2,}", purpose)):
                score += 0.08
            if kind == "avoid":
                score -= 0.03  # 避坑也要注入，但排序略低于正向技能
            terms = set(str(t) for t in s.get("page_terms", []))
            if screen_terms and terms:
                score += min(0.2, len(screen_terms & terms) / max(1, len(terms)) * 0.2)
            if score >= min_score:
                hits.append((score, s))
    hits.sort(key=lambda x: x[0], reverse=True)
    out = []
    for score, s in hits[:limit]:
        item = dict(s)
        item["score"] = round(score, 3)
        out.append(item)
    return out


def skill_hints_for(app: str, goal: str, screen: str = "", limit: int = 5) -> str:
    docs = lookup_skill_docs(app, goal, screen, limit=limit)
    if not docs:
        return ""
    lines = []
    for d in docs:
        if d.get("kind") == "avoid":
            lines.append(
                f"- 避坑：{d.get('doc', '')} 看到类似失败迹象时，优先换路径/返回/请求示教。"
            )
        else:
            lines.append(
                f"- 技能：{d.get('doc', '')} 推荐定位：优先找“{d.get('label', '')}”；"
                f"验收：{d.get('purpose', '')}。"
            )
    return "\n".join(lines)


def list_skill_docs(limit: int = 80) -> list[dict[str, Any]]:
    with _lock:
        skills = list(_load_skills().get("skills", []))
    skills.sort(key=lambda s: s.get("updated_at", ""), reverse=True)
    return skills[:limit]


def rebuild_skill_docs_from_demos() -> dict[str, Any]:
    """一次性把历史真人演示迁移为控件级技能文档。用于升级旧数据，不改变 demo 本身。"""
    with _lock:
        demos = _load_demos().get("demos", [])
        data = _load_skills()
        non_demo = [s for s in data.get("skills", []) if s.get("source") != "demo"]
        docs: list[dict[str, Any]] = []
        for d in demos:
            docs.extend(_derive_demo_skills(
                str(d.get("app", "")),
                str(d.get("goal", "")),
                str(d.get("id", f"demo_{len(docs)}")),
                list(d.get("steps", [])),
            ))
        data["skills"] = (non_demo + docs)[-500:]
        _save_skills(data)
        return {"ok": True, "demos": len(demos), "skill_docs": len(docs)}


def rebuild_trace_skill_docs_from_memory() -> dict[str, Any]:
    """把历史 run_agent memory 的 last_trace 迁移为成功/避坑技能文档。"""
    with _lock:
        entries = _load().get("entries", [])
        data = _load_skills()
        keep = [s for s in data.get("skills", []) if s.get("source") not in ("success_trace", "failure_trace")]
        docs: list[dict[str, Any]] = []
        for e in entries:
            memory_id = str(e.get("id", f"skill_{len(docs)}"))
            trace = list(e.get("last_trace", []))
            if not trace:
                continue
            app = str(e.get("app", ""))
            goal = str(e.get("goal", ""))
            note = str(e.get("last_note", ""))
            if int(e.get("successes", 0)) > 0:
                docs.extend(_derive_trace_skills(app, goal, memory_id, trace, True, note))
            if int(e.get("failures", 0)) > 0:
                docs.extend(_derive_trace_skills(app, goal, memory_id, trace, False, note))
        data["skills"] = (keep + docs)[-700:]
        _save_skills(data)
        return {"ok": True, "memories": len(entries), "skill_docs": len(docs)}
