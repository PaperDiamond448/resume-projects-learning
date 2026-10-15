package com.lingxi.mobile.action

/**
 * Separates model intent wording from device execution truth.
 *
 * A model may phrase a confirmed high-risk action as completed, but SMS/call
 * still has to cross Android/MIUI permission gates. The UI therefore uses a
 * neutral in-progress message until [DeviceActionExecutor] reports its result.
 */
object ActionExecutionMessagePolicy {
    private val permissionGatedHighRisk = setOf("send_message", "make_call")

    fun beforeExecution(actionType: String?, brainReply: String): String =
        if (actionType in permissionGatedHighRisk) {
            "确认已收到，正在交给手机执行；若需要短信、电话或通讯录权限，系统会先征求你的同意。"
        } else {
            brainReply
        }
}
