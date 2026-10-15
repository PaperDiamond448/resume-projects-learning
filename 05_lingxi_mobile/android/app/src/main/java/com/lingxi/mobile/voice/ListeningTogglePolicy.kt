package com.lingxi.mobile.voice

/** Pure policy for reconciling the persisted wake request with the real service lifecycle. */
object ListeningTogglePolicy {
    /**
     * Only a requested and actually running listener is considered "on" for a toggle.
     * A stale requested=true/running=false state therefore restarts listening instead of
     * launching the service solely to deliver an accidental STOP command.
     */
    fun nextRequested(requested: Boolean, running: Boolean): Boolean = !(requested && running)
}
