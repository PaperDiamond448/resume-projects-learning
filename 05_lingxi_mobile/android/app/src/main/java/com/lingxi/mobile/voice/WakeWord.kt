package com.lingxi.mobile.voice

import android.content.Context

/**
 * 唤醒词预设 —— 对标 Siri 的"你叫它什么名字"。
 *
 * 每个预设的 [tokens] 是 sherpa-onnx 双语 KWS 模型的建模单元串（打包期预生成、已逐一核对在词表内）：
 *  - 中文用拼音声韵母带调：你好灵犀 → `n ǐ h ǎo l íng x ī`
 *  - 英文用 CMU 音素：       Siri    → `S IH1 R IY0`、嘿 → `h ēi`
 * [id] 必须与 assets/kws-zh-en/keywords.txt 里 `@` 后的显示名一致，也即 KeywordSpotterResult.keyword。
 */
data class WakeWord(val id: String, val label: String, val tokens: String)

object WakeWords {
    /**
     * 本期预设（纯中文，拼音路最稳）。用户首启在其中挑一个；自定义任意词留作后续增量。
     * 注：曾试过英文「嘿 Siri」(双语模型原生支持)，但 Hey 的英文发音识别率不稳、已撤；
     * 中文拼音词识别准(你好灵犀真机实测稳)。tokens 已逐一核对在模型 tokens.txt 内。
     */
    val PRESETS = listOf(
        WakeWord(id = "你好灵犀", label = "你好灵犀", tokens = "n ǐ h ǎo l íng x ī"),
        WakeWord(id = "小灵同学", label = "小灵同学", tokens = "x iǎo l íng t óng x ué"),
        WakeWord(id = "灵犀灵犀", label = "灵犀灵犀", tokens = "l íng x ī l íng x ī"),
    )

    val DEFAULT = PRESETS[0]

    fun byId(id: String?): WakeWord = PRESETS.firstOrNull { it.id == id } ?: DEFAULT
}

/**
 * 唤醒配置持久化（SharedPreferences）。不存任何音频/隐私数据，只存用户选择。
 */
object WakeWordStore {
    private const val PREF = "lingxi_voice"
    private const val KEY_WORD = "wake_word_id"
    private const val KEY_ONBOARDED = "wake_onboarded"
    private const val KEY_ENABLED = "wake_enabled"

    private fun sp(ctx: Context) = ctx.getSharedPreferences(PREF, Context.MODE_PRIVATE)

    fun get(ctx: Context): WakeWord = WakeWords.byId(sp(ctx).getString(KEY_WORD, null))

    fun set(ctx: Context, word: WakeWord) =
        sp(ctx).edit().putString(KEY_WORD, word.id).putBoolean(KEY_ONBOARDED, true).apply()

    /** 是否已完成首次唤醒词选择。 */
    fun onboarded(ctx: Context): Boolean = sp(ctx).getBoolean(KEY_ONBOARDED, false)

    /** 用户是否开启了语音唤醒（决定 app 启动时是否自动拉起前台服务）。 */
    fun isEnabled(ctx: Context): Boolean = sp(ctx).getBoolean(KEY_ENABLED, false)
    fun setEnabled(ctx: Context, on: Boolean) = sp(ctx).edit().putBoolean(KEY_ENABLED, on).apply()
}
