package com.lingxi.mobile.vision

import android.content.Context

/** 独立于MediaProjection的云端截图同意；首次安装和未选择时一律为关闭。 */
object CloudVisionConsentStore {
    private const val PREF = "lingxi_cloud_vision"
    private const val KEY_ENABLED = "explicit_upload_consent"

    fun isEnabled(ctx: Context): Boolean =
        ctx.getSharedPreferences(PREF, Context.MODE_PRIVATE).getBoolean(KEY_ENABLED, false)

    fun setEnabled(ctx: Context, enabled: Boolean) {
        ctx.getSharedPreferences(PREF, Context.MODE_PRIVATE)
            .edit()
            .putBoolean(KEY_ENABLED, enabled)
            .apply()
    }
}
