package com.lingxi.mobile.vision

/**
 * 注视目标投票（纯逻辑、与相机/MediaPipe 解耦 → 可在 JVM 上单测，见 GazeVoteTest）。
 *
 * 背景（T-16 改造 Step A）：demo(`GazeDeixisDemoActivity`) 里"锁定"是靠 dwell+迟滞焊死在
 * Activity 里的临时逻辑，单帧 [GazeDeixis.resolve] 结果没有跨帧稳定性保证——一帧抖动
 * 吸附错元素就会误判。真实场景（说话人一边说话一边看，注视天然有跳动/眨眼丢帧）需要
 * 【一个采集窗口内的逐帧解析结果 → 众数投票出稳定目标】，与 [HeadMotionDetector] 的
 * "一个窗口的头姿序列 → 稳定判别"是同一套哲学，抽成同款可复用组件。
 *
 * 判定思路：
 *  - 窗口内每帧调用方各自跑 [GazeDeixis.resolve]，得到 target(String，标签文本) 或 null(无脸/未落入)；
 *  - 众数：出现次数最多的 target 是候选胜者；
 *  - 稳定占比门槛：胜者次数 / 【整窗口帧数】(含 null 帧，故意更保守) 必须 ≥ [MIN_STABLE_RATIO]，
 *    否则视为"看得不够稳"，返回 null——**永不瞎猜**，让上层走大脑澄清；
 *  - 命中则输出胜者首次/末次出现的时刻(t0/t1，毫秒，调用方按需 /1000.0 转秒喂 VisionMark，
 *    与 [ConflictVisionCapture] 里 head_motion/gesture 转换方式一致)。
 */
object GazeVote {

    /** 一帧的解析结果：tMs=采集时刻(相对窗口起点的毫秒)，target=该帧吸附到的元素文本，无脸/未落入=null。 */
    data class Sample(val tMs: Long, val target: String?)

    data class Result(val target: String, val confidence: Float, val t0Ms: Long, val t1Ms: Long)

    private const val MIN_SAMPLES = 3          // 窗口内太少帧(采集刚开始/异常)不做判断
    private const val MIN_STABLE_RATIO = 0.6f  // 众数占比(含 null 帧的保守分母)低于此→不确定

    /** 一个采集窗口内的逐帧解析结果 → 众数投票出稳定目标；不够稳定/样本太少返回 null。 */
    fun vote(samples: List<Sample>): Result? {
        if (samples.size < MIN_SAMPLES) return null
        val hits = samples.filter { it.target != null }
        if (hits.isEmpty()) return null

        val counts = hits.groupingBy { it.target!! }.eachCount()
        val (winner, count) = counts.maxByOrNull { it.value } ?: return null

        // 分母用【整窗口帧数】(含没解出目标的帧)，比只用"已解出的帧数"更保守——
        // 全程半信半疑、偶尔瞄一眼目标的情况不该被判定为"稳定注视"。
        val ratio = count.toFloat() / samples.size
        if (ratio < MIN_STABLE_RATIO) return null

        val winnerHits = hits.filter { it.target == winner }
        return Result(
            target = winner,
            confidence = ratio.coerceIn(0f, 1f),
            t0Ms = winnerHits.minOf { it.tMs },
            t1Ms = winnerHits.maxOf { it.tMs },
        )
    }
}
