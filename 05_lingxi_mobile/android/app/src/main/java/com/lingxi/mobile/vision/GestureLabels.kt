package com.lingxi.mobile.vision

/**
 * MediaPipe GestureRecognizer 的内置手势类别 → 大脑冲突消解能识别的极性标签（gesture 的 label）。
 *
 * 只映射【有明确应答极性】的手势（拇指/胜利手势），与 brain/fusion/conflict.py 的 VISION_POLARITY 对齐：
 *   thumb_up / victory → 肯定，thumb_down → 否定。
 * 其余（Open_Palm/Closed_Fist/Pointing_Up/ILoveYou/None）当作中性，不作为冲突证据上报（返回 null）。
 */
object GestureLabels {

    fun toMark(category: String?): String? = when (category) {
        "Thumb_Up" -> "thumb_up"
        "Thumb_Down" -> "thumb_down"
        "Victory" -> "victory"
        else -> null
    }
}
