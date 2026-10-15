package com.lingxi.mobile.vision

import kotlin.math.abs

/**
 * 点头/摇头判别（纯逻辑、与相机/MediaPipe 解耦 → 可在 JVM 上单测，见 HeadMotionDetectorTest）。
 *
 * 输入：一个采集窗口内的头姿样本序列 [HeadSample]（pitch=俯仰/低头抬头，yaw=偏航/左右转头，单位度）。
 * 来源由 [ConflictVisionCapture] 从 MediaPipe FaceLandmarker 的 facialTransformationMatrix 抽出，
 * **图像只在端侧处理、绝不出端（守 D5）**——这里只吃角度数字。
 *
 * 判别思路（鲁棒、与符号无关；借鉴开源同方向做法，见下）：
 *  - 点头 = pitch 轴上有"足够幅度 + 累积位移 + 至少一次方向反转"的振荡，且 pitch 压过 yaw。
 *  - 摇头 = yaw 轴同理。
 *  - "至少一次反转"把【一次性低头/转头看一眼】和【真正点头/摇头】区分开（治误报）。
 *
 * 开源借鉴（不闭门造车）：
 *  - idiap/ccdbhg-head-gesture-recognition（Face&Gesture 2024）证实 nod=pitch 轴上下、shake=yaw 轴左右
 *    旋转——本判别器轴向设计与之一致（它用 CNN 学，端侧 3s 确认窗用不起，故取其特征定义、做轻量启发式）。
 *  - smeschke 的 OpenCV 点头检测用"逐帧绝对位移累积 + 阈值"判 yes/no——本判别器把累积路径长 pathLength
 *    作为运动能量门(borrow)，并叠加自己的"方向反转"要求（纯累积法对"看一眼"会误报，反转要求规避之）。
 */
object HeadMotionDetector {

    /** 一帧头姿：tMs=采集时刻(相对窗口起点的毫秒)，pitchDeg=俯仰角，yawDeg=偏航角。 */
    data class HeadSample(val tMs: Long, val pitchDeg: Float, val yawDeg: Float)

    enum class Motion { NOD, SHAKE, NONE }

    data class Result(
        val motion: Motion,
        val confidence: Float,
        val pitchAmp: Float,
        val yawAmp: Float,
        val pitchReversals: Int,
        val yawReversals: Int,
        val dominantPath: Float = 0f,   // 主轴累积路径长(度)，运动能量证据(借鉴 smeschke 累积位移)
    ) {
        /** 大脑契约里的视觉标签（head_motion 的 label）；NONE→null。 */
        fun label(): String? = when (motion) {
            Motion.NOD -> "nod"
            Motion.SHAKE -> "shake"
            Motion.NONE -> null
        }
    }

    /** 从整段采集里定位真正发生头动的局部时间窗；失败才退回整窗。 */
    data class EventBounds(val startMs: Long, val endMs: Long, val localized: Boolean)

    private const val MIN_SAMPLES = 5
    private const val AMP_THRESH = 7f     // 主轴峰峰值(度)下限：低于此当作没动
    private const val PATH_THRESH = 12f   // 主轴累积路径长(度)下限：滤掉慢漂移（borrow: smeschke 累积位移）
    private const val STEP = 3f           // 累积位移超过此值才算一次"显著方向段"(滤抖动)
    private const val RATIO = 1.25f       // 主轴幅度须比另一轴大这么多倍，才算"明确是点头/摇头"
    private const val MIN_NOD_REVERSALS = 1
    // 高风险拒绝必须是完整左右摆动。真机 V02R2 证明单次侧移回摆（rev=1）会在举手时误报 shake；
    // 明确 N01 摇头为 rev=5。拒绝证据宁可澄清，也不能用一次侧移直接取消当前动作。
    private const val MIN_SHAKE_REVERSALS = 2

    fun classify(samples: List<HeadSample>): Result {
        if (samples.size < MIN_SAMPLES) {
            return Result(Motion.NONE, 0f, 0f, 0f, 0, 0)
        }
        val pitch = FloatArray(samples.size) { samples[it].pitchDeg }
        val yaw = FloatArray(samples.size) { samples[it].yawDeg }
        val pAmp = pitch.max() - pitch.min()
        val yAmp = yaw.max() - yaw.min()
        val pRev = reversals(pitch)
        val yRev = reversals(yaw)
        val pPath = pathLength(pitch)
        val yPath = pathLength(yaw)

        // 三重门：幅度 + 运动能量(累积路径) + 至少一次方向反转；再要求主轴明显压过另一轴。
        val isNod = pAmp >= AMP_THRESH && pPath >= PATH_THRESH &&
            pRev >= MIN_NOD_REVERSALS && pAmp >= yAmp * RATIO
        val isShake = yAmp >= AMP_THRESH && yPath >= PATH_THRESH &&
            yRev >= MIN_SHAKE_REVERSALS && yAmp >= pAmp * RATIO

        val motion = when {
            isNod && (!isShake || pAmp >= yAmp) -> Motion.NOD
            isShake -> Motion.SHAKE
            else -> Motion.NONE
        }
        val (amp, rev, path) = when (motion) {
            Motion.NOD -> Triple(pAmp, pRev, pPath)
            Motion.SHAKE -> Triple(yAmp, yRev, yPath)
            Motion.NONE -> Triple(0f, 0, 0f)
        }
        val conf = if (motion == Motion.NONE) 0f
            else (0.40f + 0.02f * amp + 0.08f * rev).coerceIn(0.45f, 0.97f)
        return Result(motion, conf, pAmp, yAmp, pRev, yRev, path)
    }

    /**
     * M7/M8 时间证据不能把整段相机窗当作点头事件。这里在已确认存在头动后，寻找能独立
     * 复现同一 motion 的最短 240ms～2000ms 子窗，并在两端各留一帧；没有可靠子窗才显式
     * 退回整窗。该函数只定位时间，不改变 [classify] 的类别与置信度。
     */
    fun eventBounds(samples: List<HeadSample>, motion: Motion): EventBounds? {
        if (motion == Motion.NONE || samples.size < MIN_SAMPLES) return null
        var bestStart = -1
        var bestEnd = -1
        var bestDuration = Long.MAX_VALUE
        var bestConfidence = -1f
        for (start in 0..samples.size - MIN_SAMPLES) {
            for (end in start + MIN_SAMPLES - 1 until samples.size) {
                val duration = samples[end].tMs - samples[start].tMs
                if (duration < MIN_EVENT_MS) continue
                if (duration > MAX_EVENT_MS) break
                val candidate = classify(samples.subList(start, end + 1))
                if (candidate.motion != motion) continue
                if (duration < bestDuration ||
                    (duration == bestDuration && candidate.confidence > bestConfidence)) {
                    bestStart = start
                    bestEnd = end
                    bestDuration = duration
                    bestConfidence = candidate.confidence
                }
            }
        }
        if (bestStart < 0) {
            return EventBounds(samples.first().tMs, samples.last().tMs, false)
        }
        val paddedStart = (bestStart - 1).coerceAtLeast(0)
        val paddedEnd = (bestEnd + 1).coerceAtMost(samples.lastIndex)
        return EventBounds(samples[paddedStart].tMs, samples[paddedEnd].tMs, true)
    }

    /** 累积路径长 = 逐帧绝对位移之和（运动能量；振荡比单向漂移路径长得多）。borrow: smeschke 点头检测。 */
    private fun pathLength(v: FloatArray): Float {
        var s = 0f
        for (i in 1 until v.size) s += abs(v[i] - v[i - 1])
        return s
    }

    /** 数显著方向反转次数：累积位移过 STEP 记一个方向段，相邻方向段反向即一次反转。 */
    private fun reversals(v: FloatArray): Int {
        var acc = 0f
        var lastDir = 0
        var count = 0
        for (i in 1 until v.size) {
            acc += v[i] - v[i - 1]
            if (abs(acc) >= STEP) {
                val dir = if (acc > 0) 1 else -1
                if (lastDir != 0 && dir != lastDir) count++
                lastDir = dir
                acc = 0f
            }
        }
        return count
    }

    private const val MIN_EVENT_MS = 240L
    private const val MAX_EVENT_MS = 2000L
}
