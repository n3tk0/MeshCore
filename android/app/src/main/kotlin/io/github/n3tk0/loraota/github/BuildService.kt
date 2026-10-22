package io.github.n3tk0.loraota.github

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
import androidx.core.app.NotificationCompat
import androidx.core.app.ServiceCompat
import androidx.core.content.ContextCompat
import io.github.n3tk0.loraota.Lang
import io.github.n3tk0.loraota.MainActivity
import io.github.n3tk0.loraota.R
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch

/**
 * Keeps the app alive while [BuildWatcher] follows a build on GitHub with the app in the background,
 * with a quiet notification of where the build is. The end of the build has its own notification.
 */
class BuildService : Service() {
    private var scope: CoroutineScope? = null

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        // always first, also for STOP: a service started for the foreground and stopped before this crashes the app
        try {
            ServiceCompat.startForeground(this, ID, notification(this, BuildWatcher.state.value),
                if (Build.VERSION.SDK_INT >= 29) ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC else 0)
        } catch (e: Exception) {
            // refused (e.g. the app went to the background in between): the build is followed while the app lives
            stopSelf()
            return START_NOT_STICKY
        }
        if (intent?.action == ACTION_STOP || BuildWatcher.state.value?.finished != false) {
            ServiceCompat.stopForeground(this, ServiceCompat.STOP_FOREGROUND_REMOVE)
            stopSelf()
            return START_NOT_STICKY
        }
        if (scope == null) {
            scope = CoroutineScope(SupervisorJob() + Dispatchers.Main).also { s ->
                s.launch { BuildWatcher.state.collect { update(it) } }
            }
        }
        return START_NOT_STICKY
    }

    @SuppressLint("MissingPermission")
    private fun update(j: BuildJob?) {
        if (j == null || j.finished) return
        runCatching { getSystemService(NotificationManager::class.java).notify(ID, notification(this, j)) }
    }

    override fun onDestroy() {
        scope?.cancel()
        scope = null
    }

    companion object {
        private const val ID = 2
        private const val CHANNEL = "build"
        private const val ACTION_STOP = "stop"

        private fun notification(ctx: Context, j: BuildJob?): Notification {
            val l = Lang.wrap(ctx)
            val nm = ctx.getSystemService(NotificationManager::class.java)
            if (nm.getNotificationChannel(CHANNEL) == null) {
                nm.createNotificationChannel(NotificationChannel(CHANNEL, l.getString(R.string.build_notif_channel),
                    NotificationManager.IMPORTANCE_LOW))
            }
            val open = PendingIntent.getActivity(ctx, 0,
                Intent(ctx, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP), PendingIntent.FLAG_IMMUTABLE)
            return NotificationCompat.Builder(ctx, CHANNEL)
                .setSmallIcon(android.R.drawable.stat_sys_download)
                .setContentTitle(l.getString(R.string.build_notif_title, j?.board ?: ""))
                .setContentText(if (j == null) "" else phaseText(l, j))
                .setProgress(0, 0, true)
                .setOngoing(true)
                .setOnlyAlertOnce(true)
                .setContentIntent(open)
                .build()
        }

        /** Where the build is, in a few words (the notification and the card on the screen). */
        fun phaseText(ctx: Context, j: BuildJob): String = when (j.phase) {
            BuildPhase.STARTED -> ctx.getString(R.string.build_phase_started)
            BuildPhase.QUEUED -> ctx.getString(R.string.build_phase_queued)
            BuildPhase.RUNNING -> if (j.step.isEmpty()) ctx.getString(R.string.build_phase_running)
                else ctx.getString(R.string.build_phase_step, j.step)
            BuildPhase.DOWNLOADING -> ctx.getString(R.string.build_phase_downloading)
            BuildPhase.DONE -> ctx.getString(R.string.build_phase_done)
            BuildPhase.FAILED -> j.error
        } + if (j.offline) " · " + ctx.getString(R.string.build_offline) else ""

        fun start(ctx: Context) {
            runCatching { ContextCompat.startForegroundService(ctx, Intent(ctx, BuildService::class.java)) }
        }

        fun stop(ctx: Context) {
            runCatching { ctx.startService(Intent(ctx, BuildService::class.java).setAction(ACTION_STOP)) }
                .onFailure { ctx.stopService(Intent(ctx, BuildService::class.java)) }
        }
    }
}
