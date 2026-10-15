package com.lingxi.mobile.vision

import android.content.Context

/**
 * 注视追踪常开开关的持久化（T-16 v2：苹果式常开，替代关键词触发短窗）。
 * 只存一个布尔开关，不存任何采集数据/校准数据（校准数据另见 [GazeCalibration]）。
 *
 * 供 [com.lingxi.mobile.accessibility.LingxiAccessibilityService] 在服务重连时
 * （如 MIUI 杀后台后用户重新打开无障碍）恢复用户上次的开/关意愿——语义与
 * [com.lingxi.mobile.voice.WakeWordStore] 的 KEY_ENABLED 完全同款。
 */
object GazeTrackingStore {
    private const val PREF = "lingxi_gaze_tracking"
    private const val KEY_ENABLED = "enabled"
    private const val KEY_TOUCH_ASSIST = "touch_assist_demo"

    fun isEnabled(ctx: Context): Boolean =
        ctx.getSharedPreferences(PREF, Context.MODE_PRIVATE).getBoolean(KEY_ENABLED, false)

    fun setEnabled(ctx: Context, on: Boolean) =
        ctx.getSharedPreferences(PREF, Context.MODE_PRIVATE).edit().putBoolean(KEY_ENABLED, on).apply()

    /**
     * 拍摄期临时能力：触控拖动悬浮圆点作为指代输入。
     * 该模式会在悬浮层上持续显示“非眼动输入”标签，不能与真实注视证据混用。
     */
    fun isTouchAssistEnabled(ctx: Context): Boolean =
        ctx.getSharedPreferences(PREF, Context.MODE_PRIVATE).getBoolean(KEY_TOUCH_ASSIST, false)

    fun setTouchAssistEnabled(ctx: Context, on: Boolean) =
        ctx.getSharedPreferences(PREF, Context.MODE_PRIVATE).edit().putBoolean(KEY_TOUCH_ASSIST, on).apply()
}
