package com.lingxi.mobile.action

import android.Manifest

/**
 * 设备动作的最小权限策略。
 *
 * 这里只描述“为了直接完成本次动作需要哪些权限”，不负责申请权限，也不决定是否执行。
 * 保持成纯逻辑，方便用 JVM 单测冻结“按需申请、绝不启动时一把梭”的契约。
 */
object ActionPermissionPolicy {

    fun required(type: String, slots: Map<String, String>): List<String> = when (type) {
        "send_message" -> buildList {
            val recipient = slots["recipient"].orEmpty()
            if (recipient.isBlank()) return@buildList
            add(Manifest.permission.SEND_SMS)
            if (!looksLikePhoneNumber(recipient)) {
                add(Manifest.permission.READ_CONTACTS)
            }
        }
        "make_call" -> buildList {
            val contact = slots["contact"].orEmpty()
            if (contact.isBlank()) return@buildList
            add(Manifest.permission.CALL_PHONE)
            if (!looksLikePhoneNumber(contact)) {
                add(Manifest.permission.READ_CONTACTS)
            }
        }
        "query_calendar" -> listOf(Manifest.permission.READ_CALENDAR)
        else -> emptyList()
    }

    internal fun looksLikePhoneNumber(value: String): Boolean {
        val compact = value.trim().replace(Regex("[\\s()-]"), "")
        if (compact.startsWith("+")) return compact.drop(1).length >= 3 && compact.drop(1).all(Char::isDigit)
        return compact.length >= 3 && compact.all(Char::isDigit)
    }
}
