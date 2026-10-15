package com.lingxi.mobile.action

import android.app.AppOpsManager
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.os.Process
import androidx.core.content.ContextCompat

/**
 * Effective dangerous-permission check for Android variants such as MIUI.
 *
 * MIUI can keep PackageManager's runtime bit granted while its permission
 * center exposes a red-X denial via a vendor AppOps mode (observed mode=5).
 * High-risk actions must require both layers to allow the operation.
 */
object EffectivePermission {

    fun isGranted(context: Context, permission: String): Boolean {
        val packageGranted = ContextCompat.checkSelfPermission(context, permission) ==
            PackageManager.PERMISSION_GRANTED
        if (!packageGranted) return false

        val op = AppOpsManager.permissionToOp(permission) ?: return true
        val manager = context.getSystemService(AppOpsManager::class.java) ?: return false
        val mode = runCatching {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                manager.unsafeCheckOpNoThrow(op, Process.myUid(), context.packageName)
            } else {
                @Suppress("DEPRECATION")
                manager.checkOpNoThrow(op, Process.myUid(), context.packageName)
            }
        }.getOrElse { return false }
        return modeAllows(packageGranted = true, appOpMode = mode)
    }

    internal fun modeAllows(packageGranted: Boolean, appOpMode: Int): Boolean =
        packageGranted && appOpMode in setOf(
            AppOpsManager.MODE_ALLOWED,
            AppOpsManager.MODE_DEFAULT,
            AppOpsManager.MODE_FOREGROUND,
        )
}
