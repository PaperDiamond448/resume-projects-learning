package com.lingxi.mobile.voice

import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.LifecycleRegistry

/**
 * 给前台服务用的极简 [LifecycleOwner] —— 让 CameraX/MediaPipe 能在 Service 里【按需】开/关相机
 * （CameraX `bindToLifecycle` 需要一个 LifecycleOwner，而 Service 本身不是）。
 *
 * 用完整生命周期对（resume→destroy）即可。**所有状态切换必须在主线程调用**（LifecycleRegistry 要求）。
 */
class ServiceLifecycleOwner : LifecycleOwner {
    private val registry = LifecycleRegistry(this)
    override val lifecycle: Lifecycle get() = registry

    fun resume() {
        registry.currentState = Lifecycle.State.CREATED
        registry.currentState = Lifecycle.State.STARTED
        registry.currentState = Lifecycle.State.RESUMED
    }

    fun destroy() {
        registry.currentState = Lifecycle.State.DESTROYED
    }
}
