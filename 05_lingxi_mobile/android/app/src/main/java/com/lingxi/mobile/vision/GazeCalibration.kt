package com.lingxi.mobile.vision

import android.content.Context
import kotlin.math.abs

/**
 * 注视→屏幕 校准映射（二次多项式 + 头姿线性项，最小二乘拟合）。
 *
 * 为何二次而非线性：注视方向→屏幕坐标在边角是非线性的，线性(仿射)模型加再多校准点也只能降噪、
 * 拟合不了边缘弯曲；二次多项式项 [gx², gy², gx·gy] 能贴合畸变。
 * 拟合自动学到方向(符号)/灵敏度/偏置，所以"方向反了"也由校准一并解决。
 *
 * T-16 Step B 新增头姿项 [hyaw, hpitch]（近距离持机看屏幕上/下时头会自然微动，
 * 这是比眼动 blendshape 更稳的纵轴细分信号——同款头姿提取见 [ConflictVisionCapture]）。
 * 8 维特征：[gx, gy, hyaw, hpitch, gx², gy², gx·gy, 1]。
 *
 * nx = Px · features(...)， ny = Py · features(...)。系数存本地，隐私不出端。
 */
class GazeCalibration(val px: DoubleArray, val py: DoubleArray) {

    fun apply(gx: Float, gy: Float, hyaw: Float, hpitch: Float): Pair<Float, Float> {
        val f = features(gx.toDouble(), gy.toDouble(), hyaw.toDouble(), hpitch.toDouble())
        var nx = 0.0; var ny = 0.0
        for (i in f.indices) { nx += px[i] * f[i]; ny += py[i] * f[i] }
        return nx.toFloat().coerceIn(0f, 1f) to ny.toFloat().coerceIn(0f, 1f)
    }

    fun save(ctx: Context) {
        val e = ctx.getSharedPreferences(PREF, Context.MODE_PRIVATE).edit()
        for (i in 0 until DIM) { e.putFloat("px$i", px[i].toFloat()); e.putFloat("py$i", py[i].toFloat()) }
        e.putBoolean("set", true).apply()
    }

    companion object {
        private const val PREF = "lingxi_gaze_calib"
        const val DIM = 8   // [gx, gy, hyaw, hpitch, gx², gy², gx·gy, 1]

        fun features(gx: Double, gy: Double, hyaw: Double, hpitch: Double) =
            doubleArrayOf(gx, gy, hyaw, hpitch, gx * gx, gy * gy, gx * gy, 1.0)

        /** 未校准时的粗线性兜底（仅占位，真用前请校准）；头姿项系数先给 0，校准后才生效。 */
        fun default() = GazeCalibration(
            px = doubleArrayOf(2.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.5),
            py = doubleArrayOf(0.0, -2.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.55),
        )

        fun isCalibrated(ctx: Context) =
            ctx.getSharedPreferences(PREF, Context.MODE_PRIVATE).getBoolean("set", false)

        fun load(ctx: Context): GazeCalibration {
            val sp = ctx.getSharedPreferences(PREF, Context.MODE_PRIVATE)
            if (!sp.getBoolean("set", false)) return default()
            val d = default()
            val px = DoubleArray(DIM) { sp.getFloat("px$it", d.px[it].toFloat()).toDouble() }
            val py = DoubleArray(DIM) { sp.getFloat("py$it", d.py[it].toFloat()).toDouble() }
            return GazeCalibration(px, py)
        }

        /**
         * 最小二乘拟合。samples 每条 = [gx, gy, hyaw, hpitch, targetNx, targetNy]。点数 < DIM 返回 null。
         * 带极小岭回归(λ)防奇异。
         */
        fun fit(samples: List<FloatArray>): GazeCalibration? {
            if (samples.size < DIM) return null
            // 正规方程：A = Σ f·fᵀ (DIM×DIM)， bX = Σ f·tx， bY = Σ f·ty
            val A = Array(DIM) { DoubleArray(DIM) }
            val bX = DoubleArray(DIM); val bY = DoubleArray(DIM)
            for (s in samples) {
                val f = features(s[0].toDouble(), s[1].toDouble(), s[2].toDouble(), s[3].toDouble())
                val tx = s[4].toDouble(); val ty = s[5].toDouble()
                for (i in 0 until DIM) {
                    bX[i] += f[i] * tx; bY[i] += f[i] * ty
                    for (j in 0 until DIM) A[i][j] += f[i] * f[j]
                }
            }
            for (i in 0 until DIM) A[i][i] += 1e-4   // 岭，防共线奇异
            val px = solve(A, bX) ?: return null
            val py = solve(copy(A), bY) ?: return null
            return GazeCalibration(px, py)
        }

        private fun copy(a: Array<DoubleArray>) = Array(a.size) { a[it].copyOf() }

        /** 高斯消元(部分主元)解 A x = b。 */
        private fun solve(A: Array<DoubleArray>, b: DoubleArray): DoubleArray? {
            val n = b.size
            val m = Array(n) { i -> DoubleArray(n + 1).also { row ->
                for (j in 0 until n) row[j] = A[i][j]; row[n] = b[i] } }
            for (col in 0 until n) {
                var piv = col
                for (r in col until n) if (abs(m[r][col]) > abs(m[piv][col])) piv = r
                if (abs(m[piv][col]) < 1e-12) return null
                val t = m[col]; m[col] = m[piv]; m[piv] = t
                for (r in 0 until n) {
                    if (r == col) continue
                    val f = m[r][col] / m[col][col]
                    for (c in col..n) m[r][c] -= f * m[col][c]
                }
            }
            return DoubleArray(n) { m[it][n] / m[it][it] }
        }
    }
}
