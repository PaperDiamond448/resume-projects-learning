package com.lingxi.mobile.confirmation

import android.content.Context

enum class PendingConfirmationOwner(val wireValue: String) {
    ACTIVITY("activity"),
    KWS("kws"),
    UNKNOWN("unknown");

    companion object {
        fun fromWire(value: String?): PendingConfirmationOwner =
            entries.firstOrNull { it.wireValue == value } ?: UNKNOWN
    }
}

data class PendingConfirmationBinding(
    val pendingId: String,
    val windowId: String,
    val owner: PendingConfirmationOwner,
)

/**
 * Shares only opaque confirmation IDs between MainActivity and the KWS service.
 * Recipient, message body, phone number and all biometric evidence are deliberately excluded.
 */
object PendingConfirmationStore {
    private const val PREFS = "pending_confirmation_binding"
    private const val PENDING_ID = "pending_id"
    private const val WINDOW_ID = "window_id"
    private const val OWNER = "owner"

    fun save(
        context: Context,
        pendingId: String?,
        windowId: String?,
        owner: PendingConfirmationOwner,
    ) {
        if (pendingId.isNullOrBlank() || windowId.isNullOrBlank()) {
            clear(context)
            return
        }
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .edit()
            .putString(PENDING_ID, pendingId)
            .putString(WINDOW_ID, windowId)
            .putString(OWNER, owner.wireValue)
            .apply()
    }

    fun load(context: Context): PendingConfirmationBinding? {
        val prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        val pendingId = prefs.getString(PENDING_ID, null).orEmpty()
        val windowId = prefs.getString(WINDOW_ID, null).orEmpty()
        return if (pendingId.isBlank() || windowId.isBlank()) null
        else PendingConfirmationBinding(
            pendingId,
            windowId,
            PendingConfirmationOwner.fromWire(prefs.getString(OWNER, null)),
        )
    }

    fun clear(context: Context) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().clear().apply()
    }
}
