package com.lingxi.mobile.voice

/**
 * Removes a wake word only when it leaked into the beginning of the command captured by KWS.
 *
 * Callers decide where invocation syntax is valid. The typed-input caller deliberately invokes
 * this only after teaching, multimodal and pending-confirmation commands have been handled, so
 * normalization cannot broaden an authorization or rewrite a special command.
 */
object WakeCommandNormalizer {
    private val boundary = Regex("^[\\s，。！？!?、；;：:,.]+")

    fun stripLeadingWakeWord(text: String, wakeWord: WakeWord): String {
        val trimmed = text.trim()
        val matched = sequenceOf(wakeWord.label, wakeWord.id)
            .filter { it.isNotBlank() }
            .distinct()
            .sortedByDescending { it.length }
            .firstOrNull { trimmed.startsWith(it) }
            ?: return trimmed

        return trimmed.removePrefix(matched).replaceFirst(boundary, "").trim()
    }
}
