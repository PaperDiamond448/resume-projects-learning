package com.lingxi.mobile.voice

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Test

class WakeAudioPreRollTest {
    @Test
    fun keepsOnlyNewestSamplesWithinBound() {
        val buffer = WakeAudioPreRoll(maxSamples = 5)
        buffer.append(floatArrayOf(1f, 2f, 3f))
        buffer.append(floatArrayOf(4f, 5f, 6f, 7f))

        val replayed = mutableListOf<Float>()
        val chunks = buffer.replay { chunk -> replayed.addAll(chunk.toList()) }

        assertEquals(2, chunks)
        assertEquals(5, buffer.sampleCount)
        assertArrayEquals(floatArrayOf(3f, 4f, 5f, 6f, 7f), replayed.toFloatArray(), 0f)
    }

    @Test
    fun copiesInputAndCanBeCleared() {
        val buffer = WakeAudioPreRoll(maxSamples = 4)
        val input = floatArrayOf(1f, 2f)
        buffer.append(input)
        input[0] = 9f

        val replayed = mutableListOf<Float>()
        buffer.replay { chunk -> replayed.addAll(chunk.toList()) }
        assertArrayEquals(floatArrayOf(1f, 2f), replayed.toFloatArray(), 0f)

        buffer.clear()
        assertEquals(0, buffer.sampleCount)
        assertEquals(0, buffer.chunkCount)
    }
}
