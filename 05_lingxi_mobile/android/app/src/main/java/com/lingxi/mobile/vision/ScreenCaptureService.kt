package com.lingxi.mobile.vision

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.graphics.Bitmap
import android.graphics.PixelFormat
import android.hardware.display.DisplayManager
import android.hardware.display.VirtualDisplay
import android.media.ImageReader
import android.media.projection.MediaProjection
import android.media.projection.MediaProjectionManager
import android.os.Build
import android.os.Handler
import android.os.HandlerThread
import android.os.IBinder
import android.util.DisplayMetrics
import android.util.Log
import android.view.WindowManager
import androidx.core.app.NotificationCompat
import androidx.core.app.ServiceCompat
import com.lingxi.mobile.MainActivity
import kotlinx.coroutines.delay
import java.io.ByteArrayOutputStream

/**
 * 路线C 视觉兜底 · 屏幕截图前台服务（MediaProjection）。
 *
 * 为什么需要它：MI 8 Lite = Android10/API29，无障碍的 `takeScreenshot` 要 API30+ 用不了 →
 * 只能走 MediaProjection。它需要：①用户一次性授权（Activity 弹 createScreenCaptureIntent）；
 * ②前台服务 type=mediaProjection 常驻持有投影（API34 还要求截图前先起 FGS）。
 *
 * 用户授权一次后，本服务把屏幕镜像进 ImageReader 持续刷新，`captureOnce()` 随时抓最新一帧 →
 * PNG 字节，供 GUI-agent 在 a11y 卡住时发给 ≤3B 视觉模型做 Set-of-Mark 选号。
 *
 * 隐私：只截【界面像素】（非生物特征），人脸/语音生物特征仍端侧闭环、不经此服务；可随时关闭。
 * 截图分辨率 = 真实屏幕像素，与无障碍 getBoundsInScreen 同坐标系 → tap 坐标零误差。
 */
class ScreenCaptureService : Service() {

    private var projection: MediaProjection? = null
    private var virtualDisplay: VirtualDisplay? = null
    private var imageReader: ImageReader? = null
    private var width = 0
    private var height = 0
    private var bgThread: HandlerThread? = null
    private var bgHandler: Handler? = null

    private val projectionCallback = object : MediaProjection.Callback() {
        override fun onStop() {
            Log.i(TAG, "MediaProjection 已停止")
            teardown()
            stopSelf()
        }
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        createChannel()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_STOP) {
            teardown(); stopSelf(); return START_NOT_STICKY
        }
        // API34 规则：必须先 startForeground(type mediaProjection)，再 getMediaProjection
        startForegroundNotif()

        val resultCode = intent?.getIntExtra(EXTRA_CODE, 0) ?: 0
        val data: Intent? = if (Build.VERSION.SDK_INT >= 33)
            intent?.getParcelableExtra(EXTRA_DATA, Intent::class.java)
        else @Suppress("DEPRECATION") intent?.getParcelableExtra(EXTRA_DATA)
        if (resultCode == 0 || data == null) {
            Log.e(TAG, "缺少投影授权令牌，停止"); stopSelf(); return START_NOT_STICKY
        }

        val mpm = getSystemService(Context.MEDIA_PROJECTION_SERVICE) as MediaProjectionManager
        projection = try {
            mpm.getMediaProjection(resultCode, data)
        } catch (e: Exception) {
            Log.e(TAG, "getMediaProjection 失败", e); null
        }
        if (projection == null) { stopSelf(); return START_NOT_STICKY }

        bgThread = HandlerThread("lingxi-cap").also { it.start() }
        bgHandler = Handler(bgThread!!.looper)
        projection!!.registerCallback(projectionCallback, bgHandler)
        setupVirtualDisplay()
        instance = this
        Log.i(TAG, "屏幕视觉就绪 ${width}x$height")
        return START_STICKY
    }

    private fun setupVirtualDisplay() {
        val metrics = DisplayMetrics()
        @Suppress("DEPRECATION")
        (getSystemService(Context.WINDOW_SERVICE) as WindowManager).defaultDisplay.getRealMetrics(metrics)
        width = metrics.widthPixels
        height = metrics.heightPixels
        val dpi = metrics.densityDpi
        imageReader = ImageReader.newInstance(width, height, PixelFormat.RGBA_8888, 2)
        virtualDisplay = runCatching {
            projection!!.createVirtualDisplay(
                "lingxi-screencap", width, height, dpi,
                DisplayManager.VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR,
                imageReader!!.surface, null, bgHandler
            )
        }.onFailure { Log.e(TAG, "createVirtualDisplay 失败", it) }.getOrNull()
    }

    /** 抓最新一帧 → PNG 字节。无可用帧返回 null（caller 可重试）。Image 的行有 padding，需按 rowStride 还原再裁剪。 */
    private fun grab(): ByteArray? {
        val reader = imageReader ?: return null
        val image = runCatching { reader.acquireLatestImage() }.getOrNull() ?: return null
        return try {
            val plane = image.planes[0]
            val pixelStride = plane.pixelStride
            val rowStride = plane.rowStride
            val rowPadding = rowStride - pixelStride * width
            val full = Bitmap.createBitmap(
                width + rowPadding / pixelStride, height, Bitmap.Config.ARGB_8888
            )
            full.copyPixelsFromBuffer(plane.buffer)
            val bmp = if (rowPadding == 0) full else Bitmap.createBitmap(full, 0, 0, width, height)
            ByteArrayOutputStream().use { bos ->
                bmp.compress(Bitmap.CompressFormat.PNG, 100, bos)
                bos.toByteArray()
            }
        } catch (e: Exception) {
            Log.e(TAG, "grab 转 Bitmap 失败", e); null
        } finally {
            runCatching { image.close() }
        }
    }

    private fun teardown() {
        runCatching { virtualDisplay?.release() }
        runCatching { imageReader?.close() }
        runCatching { projection?.unregisterCallback(projectionCallback) }
        runCatching { projection?.stop() }
        virtualDisplay = null; imageReader = null; projection = null
        runCatching { bgThread?.quitSafely() }; bgThread = null; bgHandler = null
        if (instance === this) instance = null
        ServiceCompat.stopForeground(this, ServiceCompat.STOP_FOREGROUND_REMOVE)
    }

    override fun onDestroy() {
        teardown()
        super.onDestroy()
    }

    // ---- 前台通知 ----
    private fun startForegroundNotif() {
        val type = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q)
            ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION else 0
        ServiceCompat.startForeground(this, NOTIF_ID, buildNotif(), type)
    }

    private fun buildNotif(): Notification {
        val open = PendingIntent.getActivity(
            this, 0, Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )
        val stop = PendingIntent.getService(
            this, 2, Intent(this, ScreenCaptureService::class.java).setAction(ACTION_STOP),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("灵犀 · 屏幕视觉")
            .setContentText("已开启，操作 App 卡住时看一眼屏幕帮你点对")
            .setSmallIcon(android.R.drawable.ic_menu_view)
            .setOngoing(true)
            .setContentIntent(open)
            .addAction(android.R.drawable.ic_menu_close_clear_cancel, "关闭", stop)
            .build()
    }

    private fun createChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val ch = NotificationChannel(
                CHANNEL_ID, "屏幕视觉", NotificationManager.IMPORTANCE_LOW
            ).apply { description = "灵犀 GUI 操作时的截图视觉兜底" }
            (getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager)
                .createNotificationChannel(ch)
        }
    }

    companion object {
        private const val TAG = "LingxiScreenCap"
        private const val NOTIF_ID = 0x1EDA
        private const val CHANNEL_ID = "lingxi_screencap"
        const val ACTION_STOP = "com.lingxi.mobile.SCREENCAP_STOP"
        private const val EXTRA_CODE = "result_code"
        private const val EXTRA_DATA = "result_data"

        @Volatile
        var instance: ScreenCaptureService? = null
            private set

        /** 屏幕视觉是否已授权就绪（GUI-agent 据此决定要不要走视觉兜底）。 */
        val isReady: Boolean get() = instance?.imageReader != null

        /** 用户在 Activity 同意投影后调用：携带授权令牌起前台服务。 */
        fun start(ctx: Context, resultCode: Int, data: Intent) {
            val i = Intent(ctx, ScreenCaptureService::class.java)
                .putExtra(EXTRA_CODE, resultCode)
                .putExtra(EXTRA_DATA, data)
            androidx.core.content.ContextCompat.startForegroundService(ctx, i)
        }

        fun stop(ctx: Context) {
            ctx.startService(Intent(ctx, ScreenCaptureService::class.java).setAction(ACTION_STOP))
        }

        /** 抓一帧截图（PNG 字节）。投影刚建立可能还没首帧 → 短重试。无则返回 null。 */
        suspend fun captureOnce(): ByteArray? {
            val svc = instance ?: return null
            repeat(12) {
                svc.grab()?.let { return it }
                delay(60)
            }
            return null
        }
    }
}
