package com.lingxi.mobile.vision

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.provider.ContactsContract
import androidx.core.content.ContextCompat

/**
 * T-16 Step D 支撑：说"给他发消息"但注视没解出目标时，弹自己的大卡片联系人选择 UI 兜底
 * （见 [com.lingxi.mobile.MainActivity] 的澄清卡片）——这里只负责"给我几个候选联系人"。
 *
 * 用【最近联系频率】(TIMES_CONTACTED) 取常用联系人，而不是造一个"画像常用联系人"新概念——
 * 真实通讯录数据，比自建的假列表更诚实，也不需要额外维护。无权限/查询失败→空列表，
 * 调用方据此决定是否展示卡片（没有候选就不弹，别弹一个空卡片糊弄用户）。
 */
object ContactPicker {

    fun loadFrequentContacts(context: Context, limit: Int = 4): List<String> {
        if (ContextCompat.checkSelfPermission(context, Manifest.permission.READ_CONTACTS)
            != PackageManager.PERMISSION_GRANTED
        ) return emptyList()

        val names = LinkedHashSet<String>()   // 去重且保持"常用程度"排序
        runCatching {
            context.contentResolver.query(
                ContactsContract.Contacts.CONTENT_URI,
                arrayOf(ContactsContract.Contacts.DISPLAY_NAME),
                "${ContactsContract.Contacts.TIMES_CONTACTED} > 0",
                null,
                "${ContactsContract.Contacts.TIMES_CONTACTED} DESC",
            )?.use { c ->
                val idx = c.getColumnIndex(ContactsContract.Contacts.DISPLAY_NAME)
                while (c.moveToNext() && names.size < limit) {
                    if (idx < 0) break
                    c.getString(idx)?.trim()?.takeIf { it.isNotEmpty() }?.let { names.add(it) }
                }
            }
        }
        return names.toList()
    }
}
