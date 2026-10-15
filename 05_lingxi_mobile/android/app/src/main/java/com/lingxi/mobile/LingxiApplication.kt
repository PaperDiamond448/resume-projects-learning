package com.lingxi.mobile

import android.app.Application
import android.security.NetworkSecurityPolicy
import com.lingxi.mobile.net.BrainConnectionStore

class LingxiApplication : Application() {
    override fun onCreate() {
        super.onCreate()
        check(
            BuildConfig.DEBUG ||
                !NetworkSecurityPolicy.getInstance().isCleartextTrafficPermitted
        ) {
            "Release cleartext traffic must be disabled"
        }
        BrainConnectionStore.initialize(this)
    }
}
