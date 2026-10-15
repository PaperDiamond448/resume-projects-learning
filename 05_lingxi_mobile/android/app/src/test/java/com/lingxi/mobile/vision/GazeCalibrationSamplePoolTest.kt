package com.lingxi.mobile.vision

import org.junit.Assert.assertEquals
import org.junit.Test

class GazeCalibrationSamplePoolTest {

    @Test
    fun robust_samples_reject_clear_outlier_for_same_point() {
        val frames = buildList {
            repeat(8) {
                add(GazeCalibrationSamplePool.Frame(
                    gx = 0.10f + it * 0.001f,
                    gy = -0.20f,
                    hyaw = 1f,
                    hpitch = 2f,
                    tx = 0.5f,
                    ty = 0.5f,
                ))
            }
            add(GazeCalibrationSamplePool.Frame(
                gx = 1.2f,
                gy = -1.1f,
                hyaw = 40f,
                hpitch = -40f,
                tx = 0.5f,
                ty = 0.5f,
            ))
        }

        val samples = GazeCalibrationSamplePool.robustSamples(frames)

        assertEquals(1, samples.size)
        assertEquals(0.1035f, samples[0][0], 0.01f)
        assertEquals(-0.20f, samples[0][1], 0.01f)
    }

    @Test
    fun robust_samples_keeps_points_balanced() {
        val frames = listOf(
            GazeCalibrationSamplePool.Frame(0.1f, 0.1f, 0f, 0f, 0.1f, 0.1f),
            GazeCalibrationSamplePool.Frame(0.11f, 0.1f, 0f, 0f, 0.1f, 0.1f),
            GazeCalibrationSamplePool.Frame(0.09f, 0.1f, 0f, 0f, 0.1f, 0.1f),
            GazeCalibrationSamplePool.Frame(0.4f, 0.3f, 0f, 0f, 0.9f, 0.9f),
            GazeCalibrationSamplePool.Frame(0.41f, 0.3f, 0f, 0f, 0.9f, 0.9f),
            GazeCalibrationSamplePool.Frame(0.39f, 0.3f, 0f, 0f, 0.9f, 0.9f),
        )

        val samples = GazeCalibrationSamplePool.robustSamples(frames)

        assertEquals(2, samples.size)
        assertEquals(0.1f, samples[0][4], 0.001f)
        assertEquals(0.9f, samples[1][4], 0.001f)
    }
}
