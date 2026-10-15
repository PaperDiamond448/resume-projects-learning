package com.lingxi.mobile.vision

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject
import kotlin.math.abs

/**
 * 个人注视校准样本池。
 *
 * 这不是训练 MediaPipe/生物识别模型，只是在本机保存最近几次"标准姿势下的合格校准帧"，
 * 拟合校准系数时把多次样本融合，并按点位剔除走神/抖动离群帧，让校准层越用越贴合当前用户。
 */
object GazeCalibrationSamplePool {

    data class Frame(
        val gx: Float,
        val gy: Float,
        val hyaw: Float,
        val hpitch: Float,
        val tx: Float,
        val ty: Float,
    )

    private const val PREF = "lingxi_gaze_calib_pool"
    private const val KEY_RUNS = "runs"
    private const val MAX_RUNS = 3
    private const val MIN_FRAMES_PER_POINT = 3

    fun buildSamplesIncluding(ctx: Context, current: List<Frame>): List<FloatArray> =
        robustSamples(readRuns(ctx).flatten() + current)

    fun saveRun(ctx: Context, frames: List<Frame>) {
        if (frames.isEmpty()) return
        val runs = readRuns(ctx).toMutableList()
        runs.add(frames)
        while (runs.size > MAX_RUNS) runs.removeAt(0)
        ctx.getSharedPreferences(PREF, Context.MODE_PRIVATE)
            .edit()
            .putString(KEY_RUNS, encodeRuns(runs))
            .apply()
    }

    fun sessionCount(ctx: Context): Int = readRuns(ctx).size

    fun robustSamples(frames: List<Frame>): List<FloatArray> {
        val grouped = frames.groupBy { pointKey(it.tx, it.ty) }
        return grouped.values.mapNotNull { group ->
            if (group.size < MIN_FRAMES_PER_POINT) return@mapNotNull null
            val kept = rejectOutliers(group)
            if (kept.size < MIN_FRAMES_PER_POINT) return@mapNotNull null
            val n = kept.size.toFloat()
            val first = kept.first()
            floatArrayOf(
                kept.sumOf { it.gx.toDouble() }.toFloat() / n,
                kept.sumOf { it.gy.toDouble() }.toFloat() / n,
                kept.sumOf { it.hyaw.toDouble() }.toFloat() / n,
                kept.sumOf { it.hpitch.toDouble() }.toFloat() / n,
                first.tx,
                first.ty,
            )
        }.sortedWith(compareBy<FloatArray> { it[5] }.thenBy { it[4] })
    }

    private fun rejectOutliers(frames: List<Frame>): List<Frame> {
        if (frames.size < 6) return frames
        val medGx = median(frames.map { it.gx })
        val medGy = median(frames.map { it.gy })
        val medYaw = median(frames.map { it.hyaw })
        val medPitch = median(frames.map { it.hpitch })
        val scored = frames.map { f ->
            val score = abs(f.gx - medGx) +
                abs(f.gy - medGy) +
                0.02f * abs(f.hyaw - medYaw) +
                0.02f * abs(f.hpitch - medPitch)
            f to score
        }
        val medScore = median(scored.map { it.second })
        val mad = median(scored.map { abs(it.second - medScore) })
        val limit = medScore + maxOf(0.04f, 3f * mad)
        val kept = scored.filter { it.second <= limit }.map { it.first }
        return if (kept.size >= MIN_FRAMES_PER_POINT) kept else frames
    }

    private fun median(values: List<Float>): Float {
        if (values.isEmpty()) return 0f
        val sorted = values.sorted()
        val mid = sorted.size / 2
        return if (sorted.size % 2 == 1) sorted[mid] else (sorted[mid - 1] + sorted[mid]) / 2f
    }

    private fun pointKey(tx: Float, ty: Float) = "%.3f,%.3f".format(tx, ty)

    private fun readRuns(ctx: Context): List<List<Frame>> {
        val raw = ctx.getSharedPreferences(PREF, Context.MODE_PRIVATE).getString(KEY_RUNS, null)
            ?: return emptyList()
        return runCatching {
            val root = JSONArray(raw)
            List(root.length()) { runIdx ->
                val run = root.getJSONObject(runIdx).getJSONArray("frames")
                List(run.length()) { frameIdx ->
                    val f = run.getJSONArray(frameIdx)
                    Frame(
                        gx = f.getDouble(0).toFloat(),
                        gy = f.getDouble(1).toFloat(),
                        hyaw = f.getDouble(2).toFloat(),
                        hpitch = f.getDouble(3).toFloat(),
                        tx = f.getDouble(4).toFloat(),
                        ty = f.getDouble(5).toFloat(),
                    )
                }
            }
        }.getOrDefault(emptyList())
    }

    private fun encodeRuns(runs: List<List<Frame>>): String {
        val root = JSONArray()
        for (run in runs) {
            val obj = JSONObject()
            val frames = JSONArray()
            for (f in run) {
                frames.put(JSONArray().apply {
                    put(f.gx.toDouble())
                    put(f.gy.toDouble())
                    put(f.hyaw.toDouble())
                    put(f.hpitch.toDouble())
                    put(f.tx.toDouble())
                    put(f.ty.toDouble())
                })
            }
            obj.put("frames", frames)
            root.put(obj)
        }
        return root.toString()
    }
}
