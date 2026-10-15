"""TTS 多后端 —— sherpa-onnx 本地VITS / edge-tts(在线) / pyttsx3·SAPI(本地兜底)。

后端选择（response.tts.backend）：
  sherpa   : 本地中文VITS（与ASR同款 sherpa-onnx，离线、音质好、无代理坑、≪3B合规），失败兜底 pyttsx3
  pyttsx3  : 本地 SAPI（0.15s稳，零依赖兜底）
  edge     : 在线 edge-tts（音质好但代理环境实测卡19s，慎用），失败兜底 pyttsx3
  auto     : sherpa 模型就绪则优先，否则 pyttsx3；edge 因代理坑不入 auto（可显式选）
  off      : 静音
synthesize() 返回音频文件路径；speak() 合成后直接播放（demo 用）。
"""
from __future__ import annotations

import asyncio
import tempfile
import time
from pathlib import Path

from ..core.config import PROJECT_ROOT, Config
from ..core.log import get_logger

logger = get_logger("response.tts")


class TTSEngine:
    def __init__(self, cfg: Config) -> None:
        self.backend = cfg.get("response.tts.backend", "auto")  # sherpa|pyttsx3|edge|auto|off
        self.edge_voice = cfg.get("response.tts.edge_voice", "zh-CN-XiaoxiaoNeural")
        self.rate = cfg.get("response.tts.rate", "+10%")        # edge 语速
        # 实测开代理时 edge-tts 可拖到 20s+，超时立即降级本地，保证交互节奏
        self.edge_timeout = float(cfg.get("response.tts.edge_timeout", 6.0))
        # 粘性降级：edge 连续失败2次后本周期内不再尝试（每次都等6s超时太伤体验）
        self.edge_cooldown = float(cfg.get("response.tts.edge_cooldown", 300.0))
        self._edge_fails = 0
        self._edge_disabled_until = 0.0
        # sherpa-onnx 本地 VITS（懒加载；模型缺失/损坏则本进程内标记失败、不再重试）
        self.sherpa_cfg = cfg.get("response.tts.sherpa", {}) or {}
        self._sherpa = None
        self._sherpa_failed = False

    # ---- 合成 ----

    def synthesize(self, text: str, out_path: str | None = None) -> str | None:
        """文本 → 音频文件路径；backend=off 或全部后端失败返回 None。按后端链依次尝试，首个成功即返回。"""
        if self.backend == "off" or not text.strip():
            return None
        out = out_path or tempfile.mktemp(suffix=".wav", prefix="lingxi_tts_")
        t0 = time.perf_counter()
        for b in self._backend_chain():
            try:
                path = self._synth_with(b, text, out)
                if path:
                    logger.info("%s 合成 %.2fs: %s", b, time.perf_counter() - t0, text[:20])
                    return path
            except Exception as e:  # noqa: BLE001 — 单后端失败统一降级到链中下一个
                logger.warning("%s 合成失败(%s)，尝试下一后端", b, type(e).__name__)
                if b == "sherpa":
                    self._sherpa_failed = True  # 模型缺失/损坏：本进程内不再尝试 sherpa
                elif b == "edge":
                    self._edge_fails += 1
                    if self._edge_fails >= 2:
                        self._edge_disabled_until = time.monotonic() + self.edge_cooldown
                        logger.warning("edge-tts 连续失败，%d 秒内改用本地", int(self.edge_cooldown))
        logger.error("所有 TTS 后端均失败: %s", text[:20])
        return None

    def _backend_chain(self) -> list[str]:
        """返回按优先级排列的后端尝试链（本地后端兜底，保证端侧可用）。"""
        if self.backend == "off":
            return []
        if self.backend == "sherpa":
            return ["sherpa", "pyttsx3"]
        if self.backend == "edge":
            return ["edge", "pyttsx3"]
        if self.backend == "pyttsx3":
            return ["pyttsx3"]
        # auto: 本地 sherpa 就绪则优先，再 pyttsx3 兜底；edge 因代理坑不入 auto
        chain = []
        if self._sherpa_ready():
            chain.append("sherpa")
        chain.append("pyttsx3")
        return chain

    def _synth_with(self, backend: str, text: str, out: str) -> str | None:
        """用指定后端合成；按后端确定输出文件后缀（edge→mp3，本地→wav）。"""
        base = str(Path(out).with_suffix(""))
        if backend == "sherpa":
            wav = base + ".wav"
            self._sherpa_synth(text, wav)
            return wav
        if backend == "edge":
            mp3 = base + ".mp3"
            self._edge_synth(text, mp3)
            return mp3
        if backend == "pyttsx3":
            wav = base + ".wav"
            self._pyttsx3_synth(text, wav)
            return wav
        return None

    # ---- sherpa-onnx 本地 VITS ----

    def _sherpa_model_dir(self) -> Path | None:
        md = self.sherpa_cfg.get("model_dir")
        return (PROJECT_ROOT / md) if md else None

    def _sherpa_ready(self) -> bool:
        """模型主文件存在且未被标记失败时才认为就绪（auto 据此决定是否入链）。"""
        if self._sherpa_failed:
            return False
        md = self._sherpa_model_dir()
        model = self.sherpa_cfg.get("model", "model.onnx")
        return bool(md and (md / model).exists())

    def _ensure_sherpa(self):
        if self._sherpa is None:
            import sherpa_onnx

            from ..core.winpath import ensure_ascii_dir

            # sherpa-onnx C++ 层打不开中文路径 → 镜像到 ASCII 安全目录（递归含 dict/ 子目录）
            md = ensure_ascii_dir(self._sherpa_model_dir())
            c = self.sherpa_cfg

            def _p(key: str, default: str = "") -> str:
                v = c.get(key, default)
                return str(md / v) if v else ""

            rule = c.get("rule_fsts", "") or ""
            rule_fsts = ",".join(
                str(md / r.strip()) for r in rule.split(",") if r.strip()
            )
            config = sherpa_onnx.OfflineTtsConfig(
                model=sherpa_onnx.OfflineTtsModelConfig(
                    vits=sherpa_onnx.OfflineTtsVitsModelConfig(
                        model=_p("model", "model.onnx"),
                        lexicon=_p("lexicon"),
                        tokens=_p("tokens"),
                        data_dir=_p("data_dir"),
                        dict_dir=_p("dict_dir"),
                    ),
                    provider="cpu",
                    num_threads=int(c.get("num_threads", 4)),
                ),
                rule_fsts=rule_fsts,
                max_num_sentences=2,
            )
            if not config.validate():
                raise RuntimeError("sherpa-onnx TTS 配置校验失败（检查模型文件路径）")
            t0 = time.perf_counter()
            self._sherpa = sherpa_onnx.OfflineTts(config)
            logger.info("sherpa-onnx TTS 载入完成 (%.1fs)", time.perf_counter() - t0)
        return self._sherpa

    def _sherpa_synth(self, text: str, out_wav: str) -> None:
        import wave

        import numpy as np

        tts = self._ensure_sherpa()
        c = self.sherpa_cfg
        audio = tts.generate(text, sid=int(c.get("sid", 0)), speed=float(c.get("speed", 1.0)))
        samples = np.asarray(audio.samples, dtype=np.float32)
        if samples.size == 0:
            raise RuntimeError("sherpa-onnx TTS 输出为空")
        pcm16 = np.clip(samples * 32767.0, -32768, 32767).astype("<i2")
        with wave.open(out_wav, "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(int(audio.sample_rate))
            w.writeframes(pcm16.tobytes())

    # ---- edge-tts（在线）----

    def _edge_synth(self, text: str, out_path: str) -> None:
        import edge_tts

        async def run():
            # proxy=None 显式禁用代理（开代理时实测拖慢 10~20 倍）
            tts = edge_tts.Communicate(text, voice=self.edge_voice, rate=self.rate,
                                       proxy=None)
            await asyncio.wait_for(tts.save(out_path), timeout=self.edge_timeout)

        # 冷却期内直接判失败（不浪费每次6s超时），交由链降级到本地
        if time.monotonic() < self._edge_disabled_until:
            raise RuntimeError("edge-tts 冷却中")
        asyncio.run(run())
        if Path(out_path).stat().st_size < 1000:
            raise RuntimeError("edge-tts 输出过小，疑似失败")

    # ---- pyttsx3 / SAPI（本地兜底）----

    def _pyttsx3_synth(self, text: str, out_path: str) -> None:
        import pyttsx3

        engine = pyttsx3.init()  # 每次新建：复用实例在连续 save_to_file 时易卡死
        engine.save_to_file(text, out_path)
        engine.runAndWait()
        engine.stop()

    # ---- 播放（demo 用）----

    def speak(self, text: str) -> None:
        path = self.synthesize(text)
        if path is None:
            return
        try:
            import sounddevice as sd
            from faster_whisper.audio import decode_audio  # 复用 PyAV 解码 mp3/wav

            audio = decode_audio(path, sampling_rate=24000)
            sd.play(audio, samplerate=24000, blocking=True)
        except Exception as e:  # noqa: BLE001
            logger.error("播放失败: %s", e)
        finally:
            Path(path).unlink(missing_ok=True)
