package com.lingxi.mobile.voice

import org.junit.Assert.assertEquals
import org.junit.Test

class WakeCommandNormalizerTest {
    private val wakeWord = WakeWord(
        id = "你好灵犀",
        label = "你好灵犀",
        tokens = "unused",
    )

    @Test
    fun stripsWakeWordAndChinesePunctuationAtStart() {
        assertEquals(
            "现在几点",
            WakeCommandNormalizer.stripLeadingWakeWord("你好灵犀，现在几点", wakeWord),
        )
    }

    @Test
    fun stripsWakeWordWhenAsrProvidesNoBoundaryPunctuation() {
        assertEquals(
            "查询杭州天气",
            WakeCommandNormalizer.stripLeadingWakeWord("你好灵犀查询杭州天气", wakeWord),
        )
    }

    @Test
    fun stripsOnlyOneLeadingOccurrence() {
        assertEquals(
            "你好灵犀现在几点",
            WakeCommandNormalizer.stripLeadingWakeWord("你好灵犀你好灵犀现在几点", wakeWord),
        )
    }

    @Test
    fun standaloneWakeWordNormalizesToEmptyForCallerToHandle() {
        assertEquals(
            "",
            WakeCommandNormalizer.stripLeadingWakeWord("你好灵犀", wakeWord),
        )
    }

    @Test
    fun leavesWakeWordInTheMiddleUntouched() {
        assertEquals(
            "解释一下你好灵犀是什么意思",
            WakeCommandNormalizer.stripLeadingWakeWord("解释一下你好灵犀是什么意思", wakeWord),
        )
    }

    @Test
    fun leavesOtherWakeWordUntouched() {
        assertEquals(
            "小灵同学现在几点",
            WakeCommandNormalizer.stripLeadingWakeWord("小灵同学现在几点", wakeWord),
        )
    }
}
