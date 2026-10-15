package com.lingxi.mobile.confirmation

/**
 * 决定大脑响应后是否自动开启一次新的视觉确认窗。
 *
 * 自动采集只属于新建 pending 的首次确认提示。低置信、shadow、来源错误等结果虽然会保留
 * pending，但不得再次自动开相机，否则同一手势会被连续采集并造成重复提示/重复事件。
 */
object ConfirmationCapturePolicy {
    fun shouldAutoStart(status: String, decisionReason: String): Boolean =
        status == "pending_confirmation" && decisionReason == "NEW_PENDING"
}
