package com.lingxi.mobile.vision

/**
 * 注视方向估计 —— 从 MediaPipe FaceLandmarker 的 blendshapes 算原始注视方向，
 * 再经 [GazeCalibration] 映射到屏幕。
 *
 * 为何用 blendshapes：`eyeLookIn/Out/Up/Down`(左右眼共8个) 已是模型学到的注视语义，
 * 比"虹膜中心相对眼角"几何法更稳、对头姿更鲁棒。原始方向(gx,gy)的符号/灵敏度/边缘畸变
 * 全交给 25 点二次校准学，所以这里不做任何手调系数（治"没校准就不准/方向反"）。
 */
object GazeEstimator {

    /** 当前生效的校准（App 启动/校准完成时由界面注入；默认是粗线性兜底）。 */
    @Volatile var calib: GazeCalibration = GazeCalibration.default()

    /** 原始注视方向 (gx,gy)：gx 正=看(被试)右，gy 正=看上；缺关键 blendshape 返回 null。 */
    fun rawFromBlendshapes(bs: Map<String, Float>): Pair<Float, Float>? {
        val lookInL = bs["eyeLookInLeft"] ?: return null
        val lookOutL = bs["eyeLookOutLeft"] ?: return null
        val lookInR = bs["eyeLookInRight"] ?: return null
        val lookOutR = bs["eyeLookOutRight"] ?: return null
        val upL = bs["eyeLookUpLeft"] ?: 0f
        val upR = bs["eyeLookUpRight"] ?: 0f
        val downL = bs["eyeLookDownLeft"] ?: 0f
        val downR = bs["eyeLookDownRight"] ?: 0f
        val lookRight = (lookInL + lookOutR) / 2f
        val lookLeft = (lookOutL + lookInR) / 2f
        val gx = lookRight - lookLeft
        val gy = (upL + upR) / 2f - (downL + downR) / 2f
        return gx to gy
    }

    /** 校准后的归一化注视点 (nx,ny)∈[0,1]；无脸/缺数据返回 null。hyaw/hpitch=头姿(度)，
     *  无头姿来源时传 0（退化为纯眼动，头姿项系数在 [GazeCalibration.default] 里本就是 0）。 */
    fun fromBlendshapes(bs: Map<String, Float>, hyaw: Float = 0f, hpitch: Float = 0f): Pair<Float, Float>? {
        val r = rawFromBlendshapes(bs) ?: return null
        return calib.apply(r.first, r.second, hyaw, hpitch)
    }
}
