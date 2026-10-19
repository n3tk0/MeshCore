package io.github.n3tk0.loraota

import android.annotation.SuppressLint
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import android.os.PowerManager
import androidx.core.app.NotificationCompat
import androidx.core.app.ServiceCompat
import androidx.core.content.ContextCompat

/**
 * Keeps the app running while an update goes on with the screen off or another app in front:
 * a notification with the progress, and the CPU awake for the waits between windows.
 * The update itself runs in the app; closing the app (swiping it away) still stops it.
 */
class OtaService : Service() {
    private var wake: PowerManager.WakeLock? = null

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val n = notification(this, intent?.getStringExtra(EXTRA_TITLE) ?: "", intent?.getStringExtra(EXTRA_TEXT) ?: "", 0, 0)
        // always first, also for STOP: a service started for the foreground and stopped before this crashes the app
        ServiceCompat.startForeground(this, ID, n,
            if (Build.VERSION.SDK_INT >= 29) ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE else 0)
        if (intent?.action == ACTION_STOP) {
            ServiceCompat.stopForeground(this, ServiceCompat.STOP_FOREGROUND_REMOVE)
            stopSelf()
            return START_NOT_STICKY
        }
        if (wake == null) {
            wake = (getSystemService(POWER_SERVICE) as PowerManager)
                .newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "loraota:update")
                .apply { acquire(MAX_HOLD_MS) }
        }
        return START_NOT_STICKY
    }

    override fun onDestroy() {
        wake?.let { if (it.isHeld) it.release() }
        wake = null
    }

    companion object {
        private const val ID = 1
        private const val CHANNEL = "update"
        private const val EXTRA_TITLE = "title"
        private const val EXTRA_TEXT = "text"
        private const val ACTION_STOP = "stop"
        private const val MAX_HOLD_MS = 12 * 3600_000L   // a big update with waits for time on air
        private var active = false   // no progress after stop: it would leave a notification behind

        private fun channel(ctx: Context) {
            val nm = ctx.getSystemService(NotificationManager::class.java)
            if (nm.getNotificationChannel(CHANNEL) == null) {
                nm.createNotificationChannel(NotificationChannel(CHANNEL,
                    Lang.wrap(ctx).getString(R.string.notif_channel), NotificationManager.IMPORTANCE_LOW))
            }
        }

        private fun notification(ctx: Context, title: String, text: String, sent: Int, total: Int): Notification {
            val open = PendingIntent.getActivity(ctx, 0,
                Intent(ctx, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP),
                PendingIntent.FLAG_IMMUTABLE)
            return NotificationCompat.Builder(ctx, CHANNEL)
                .setSmallIcon(android.R.drawable.stat_sys_upload)
                .setContentTitle(title)
                .setContentText(text)
                .setStyle(NotificationCompat.BigTextStyle().bigText(text))
                .setProgress(total, sent, total == 0)
                .setOngoing(true)
                .setOnlyAlertOnce(true)
                .setContentIntent(open)
                .build()
        }

        fun start(ctx: Context, title: String, text: String) {
            channel(ctx)
            active = true
            runCatching {
                ContextCompat.startForegroundService(ctx,
                    Intent(ctx, OtaService::class.java).putExtra(EXTRA_TITLE, title).putExtra(EXTRA_TEXT, text))
            }
        }

        /** Without the notifications permission the update still goes on, only with no notification. */
        @SuppressLint("MissingPermission")
        fun update(ctx: Context, title: String, text: String, sent: Int, total: Int) {
            if (!active) return
            runCatching {
                ctx.getSystemService(NotificationManager::class.java).notify(ID, notification(ctx, title, text, sent, total))
            }
        }

        /** Through the service itself: it may not have called startForeground yet. */
        fun stop(ctx: Context) {
            if (!active) return
            active = false
            runCatching { ctx.startService(Intent(ctx, OtaService::class.java).setAction(ACTION_STOP)) }
                .onFailure { ctx.stopService(Intent(ctx, OtaService::class.java)) }
        }
    }
}
