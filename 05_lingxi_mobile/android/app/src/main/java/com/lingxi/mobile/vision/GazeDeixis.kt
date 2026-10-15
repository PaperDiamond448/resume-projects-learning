package com.lingxi.mobile.vision

import kotlin.math.sqrt

/**
 * 注视指代消解 —— 赛题旗舰场景"看向屏幕上的某个联系人，说'给他发消息'"的核心。
 *
 * 设计要点（为何可行）：**不需要像素级精准注视**。无障碍能把当前屏幕离散成有限个
 * 带坐标+文字的元素（联系人行/卡片/按钮），我们只需把粗注视点"吸附"到最该是的那个元素：
 *   注视点落入某元素框 → 取最具体(最小)的那个；都没落入 → 取中心最近且在阈值内的。
 * 鲁棒性来自"候选离散 + 就近吸附"，而非眼动本身的绝对精度。
 *
 * 这部分是纯逻辑、与 Android/相机无关 → 可在 JVM 上单测（见 GazeDeixisTest）。
 * 注视点的来源（MediaPipe 相机管线）抽象在 [GazeProvider] 后面，端侧实现单独接。
 * 隐私：只把解析出的文本（如"张伟"）当 gaze_target 出端，人脸/图像不出端（守 D5）。
 */
object GazeDeixis {

    /** 触发注视指代的指示代词（句中含这些才需要用注视来定"指的是谁/哪个"）。 */
    private val PRONOUNS = listOf(
        "他", "她", "它", "他们", "她们", "它们",
        "这个", "那个", "这条", "那条", "这位", "那位", "这张", "那张", "这里", "那里",
    )

    /** 指令是否含指示代词（不含则无需注视融合，省一次开摄像头）。 */
    fun hasDeixis(text: String): Boolean = PRONOUNS.any { text.contains(it) }

    /** 排除纯数字/时间/价格/符号这类"徽标文本"（如"8:32"/"99+"/"¥199"），不是"名字"样式。 */
    private val NON_NAME_RE = Regex("^[\\d:：.,%¥$￥+\\-()（）\\s]+$")

    /**
     * [Step C] 真实屏幕候选过滤：`elementsForGaze()` 什么带文字的节点都收（包括时间戳/角标/
     * 长段落），直接拿去 resolve 容易吸附到无意义的小元素上。只留【可点 + 名字样式的短文本】——
     * 联系人行/卡片标题这类真正"可以被指代"的目标，去掉纯数字徽标和过长段落。
     */
    fun filterCandidates(elements: List<ScreenElement>, maxLen: Int = 20): List<ScreenElement> =
        elements.filter {
            val t = it.text.trim()
            it.clickable && t.isNotEmpty() && t.length <= maxLen && !NON_NAME_RE.matches(t)
        }

    /**
     * 把注视点吸附到屏幕元素：优先"落入"（多个取最小=最具体），否则"中心最近且 ≤[maxDistPx]"。
     * 找不到合适元素返回 null（让大脑走澄清，不瞎猜）。
     */
    fun resolve(
        gazeX: Float, gazeY: Float,
        elements: List<ScreenElement>,
        maxDistPx: Float = 220f,
    ): ScreenElement? {
        if (elements.isEmpty()) return null
        val containing = elements.filter { it.bounds.contains(gazeX, gazeY) }
        if (containing.isNotEmpty()) return containing.minByOrNull { it.bounds.area() }

        var best: ScreenElement? = null
        var bestD = Float.MAX_VALUE
        for (e in elements) {
            val d = e.bounds.distanceTo(gazeX, gazeY)
            if (d < bestD) { bestD = d; best = e }
        }
        return if (bestD <= maxDistPx) best else null
    }
}

/** 屏幕像素坐标的矩形（纯数据，避开 android.graphics.Rect 以便 JVM 单测）。 */
data class Bounds(val left: Int, val top: Int, val right: Int, val bottom: Int) {
    fun contains(x: Float, y: Float) = x >= left && x < right && y >= top && y < bottom
    fun area(): Long = (right - left).toLong() * (bottom - top).toLong()
    /** 点到矩形的最短距离（点在内部为 0）。 */
    fun distanceTo(x: Float, y: Float): Float {
        val dx = when { x < left -> left - x; x > right -> x - right; else -> 0f }
        val dy = when { y < top -> top - y; y > bottom -> y - bottom; else -> 0f }
        return sqrt(dx * dx + dy * dy)
    }
}

/** 屏幕上一个可被"看"的元素：文字（联系人名/标签）+ 屏幕坐标 + 是否可点。 */
data class ScreenElement(val text: String, val bounds: Bounds, val clickable: Boolean = false)

/** 注视点（屏幕像素，已做完校准映射）。 */
data class GazePoint(val x: Float, val y: Float, val confidence: Float = 1f)

/**
 * 注视点来源抽象。端侧实现 = MediaPipe（虹膜+头姿）→ 校准映射到屏幕像素（下午带相机接）。
 * 解耦的好处：融合逻辑现在就能写死测好，MediaPipe 实现只填这一个方法。
 */
interface GazeProvider {
    /** 最近一次注视点；无脸/未就绪/置信低 → null。 */
    fun currentGaze(): GazePoint?
}
