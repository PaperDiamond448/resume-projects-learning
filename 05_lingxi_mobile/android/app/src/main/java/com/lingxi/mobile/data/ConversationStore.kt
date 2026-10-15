package com.lingxi.mobile.data

import android.content.Context
import com.google.gson.GsonBuilder
import com.google.gson.reflect.TypeToken
import java.io.File
import java.util.UUID

/** 聊天卡片：点击后直接把已经操作到位的目标 App resume 到前台（不是重新搜索一次）。 */
data class MessageCard(
    val packageName: String,
    val title: String,
    val subtitle: String = "",
    val thumbnailPath: String? = null,
)

data class ConversationMessage(
    val id: String = UUID.randomUUID().toString(),
    val role: String,
    val who: String,
    val text: String,
    val createdAtMs: Long = System.currentTimeMillis(),
    // 可空 + 手动 orEmpty()：旧版 conversations.json 没有这个字段，Gson 反射建对象会绕过默认参数直接留 null。
    val cards: List<MessageCard>? = null,
)

data class ConversationRecord(
    val id: String = UUID.randomUUID().toString(),
    var title: String = "新对话",
    val createdAtMs: Long = System.currentTimeMillis(),
    var updatedAtMs: Long = createdAtMs,
    val messages: MutableList<ConversationMessage> = mutableListOf(),
)

class ConversationStore(context: Context) {
    private val gson = GsonBuilder().setPrettyPrinting().create()
    private val file = File(context.filesDir, "conversations.json")
    private val listType = object : TypeToken<MutableList<ConversationRecord>>() {}.type

    fun newConversation(): ConversationRecord = ConversationRecord()

    fun all(): List<ConversationRecord> =
        load().sortedByDescending { it.updatedAtMs }

    fun addMessage(conversation: ConversationRecord, message: ConversationMessage) {
        conversation.messages.add(message)
        conversation.updatedAtMs = message.createdAtMs
        if (conversation.title == "新对话" && message.role == ROLE_USER) {
            conversation.title = message.text.asTitle()
        }
        upsert(conversation)
    }

    fun upsert(conversation: ConversationRecord) {
        if (conversation.messages.isEmpty()) return
        val conversations = load()
        val index = conversations.indexOfFirst { it.id == conversation.id }
        if (index >= 0) conversations[index] = conversation else conversations.add(conversation)
        save(conversations.sortedByDescending { it.updatedAtMs }.toMutableList())
    }

    fun find(id: String): ConversationRecord? = load().firstOrNull { it.id == id }

    private fun load(): MutableList<ConversationRecord> {
        if (!file.exists()) return mutableListOf()
        return runCatching {
            gson.fromJson<MutableList<ConversationRecord>>(file.readText(Charsets.UTF_8), listType)
        }.getOrNull() ?: mutableListOf()
    }

    private fun save(conversations: MutableList<ConversationRecord>) {
        val tmp = File(file.parentFile, "${file.name}.tmp")
        tmp.writeText(gson.toJson(conversations), Charsets.UTF_8)
        if (!tmp.renameTo(file)) {
            file.writeText(gson.toJson(conversations), Charsets.UTF_8)
            tmp.delete()
        }
    }

    private fun String.asTitle(): String {
        val compact = trim().replace(Regex("\\s+"), " ")
        return if (compact.length <= 24) compact else compact.take(24) + "..."
    }

    companion object {
        const val ROLE_USER = "user"
        const val ROLE_ASSISTANT = "assistant"
        const val ROLE_SYSTEM = "system"
        const val ROLE_EXECUTION = "execution"
        const val ROLE_REPORT = "report"
        const val ROLE_ERROR = "error"
    }
}
