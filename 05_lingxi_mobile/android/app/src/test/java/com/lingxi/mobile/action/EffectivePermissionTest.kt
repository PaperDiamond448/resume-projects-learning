package com.lingxi.mobile.action

import android.app.AppOpsManager
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class EffectivePermissionTest {

    @Test
    fun `package denial always denies`() {
        assertFalse(EffectivePermission.modeAllows(false, AppOpsManager.MODE_ALLOWED))
    }

    @Test
    fun `standard allowed default and foreground modes pass`() {
        assertTrue(EffectivePermission.modeAllows(true, AppOpsManager.MODE_ALLOWED))
        assertTrue(EffectivePermission.modeAllows(true, AppOpsManager.MODE_DEFAULT))
        assertTrue(EffectivePermission.modeAllows(true, AppOpsManager.MODE_FOREGROUND))
    }

    @Test
    fun `ignored errored and MIUI ask mode do not pass`() {
        assertFalse(EffectivePermission.modeAllows(true, AppOpsManager.MODE_IGNORED))
        assertFalse(EffectivePermission.modeAllows(true, AppOpsManager.MODE_ERRORED))
        assertFalse(EffectivePermission.modeAllows(true, 5))
    }
}
