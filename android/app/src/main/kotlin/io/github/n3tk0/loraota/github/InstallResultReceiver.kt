package io.github.n3tk0.loraota.github

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.pm.PackageInstaller
import android.os.Build

/** Receives PackageInstaller session status for the app self-update. */
class InstallResultReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        val status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE)
        val message = intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE)
        when (status) {
            PackageInstaller.STATUS_PENDING_USER_ACTION -> {
                @Suppress("DEPRECATION")
                val confirm: Intent? = if (Build.VERSION.SDK_INT >= 33) {
                    intent.getParcelableExtra(Intent.EXTRA_INTENT, Intent::class.java)
                } else {
                    intent.getParcelableExtra(Intent.EXTRA_INTENT)
                }
                if (confirm != null) {
                    confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                    try {
                        context.startActivity(confirm)
                    } catch (e: Exception) {
                        AppUpdater.reportInstallResult(AppUpdater.InstallResult(false, e.message ?: e.toString()))
                    }
                }
            }
            PackageInstaller.STATUS_SUCCESS ->
                AppUpdater.reportInstallResult(AppUpdater.InstallResult(true, null))
            // the user declined the system dialog: not an error worth showing
            PackageInstaller.STATUS_FAILURE_ABORTED ->
                AppUpdater.reportInstallResult(AppUpdater.InstallResult(true, null))
            else ->
                AppUpdater.reportInstallResult(AppUpdater.InstallResult(false, message ?: "install failed ($status)"))
        }
    }
}
