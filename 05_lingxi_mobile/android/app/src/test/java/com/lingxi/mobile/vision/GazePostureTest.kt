package com.lingxi.mobile.vision

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class GazePostureTest {

    private fun box(left: Float, top: Float, right: Float, bottom: Float) = floatArrayOf(
        left, top,
        right, top,
        right, bottom,
        left, bottom,
    )

    @Test
    fun centered_face_at_good_size_is_ok() {
        val q = GazePosture.evaluate(box(0.32f, 0.25f, 0.68f, 0.70f), 0f, 0f)
        assertTrue(q.ok)
    }

    @Test
    fun too_far_face_is_rejected() {
        val q = GazePosture.evaluate(box(0.43f, 0.38f, 0.57f, 0.60f), 0f, 0f)
        assertFalse(q.ok)
    }

    @Test
    fun too_close_face_is_rejected() {
        val q = GazePosture.evaluate(box(0.18f, 0.08f, 0.82f, 0.78f), 0f, 0f)
        assertFalse(q.ok)
    }

    @Test
    fun off_center_face_is_rejected() {
        val q = GazePosture.evaluate(box(0.05f, 0.25f, 0.42f, 0.70f), 0f, 0f)
        assertFalse(q.ok)
    }

    @Test
    fun tilted_head_is_rejected() {
        val q = GazePosture.evaluate(box(0.32f, 0.25f, 0.68f, 0.70f), 24f, 0f)
        assertFalse(q.ok)
    }
}
