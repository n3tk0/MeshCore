package io.github.n3tk0.loraota.ui

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.provider.Settings
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Checkbox
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import io.github.n3tk0.loraota.BuildConfig
import io.github.n3tk0.loraota.Lang
import io.github.n3tk0.loraota.R
import io.github.n3tk0.loraota.ThemeMode
import io.github.n3tk0.loraota.github.AppUpdater
import java.io.File
import java.util.Locale

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(
    vm: AppViewModel,
    themeMode: ThemeMode,
    setThemeMode: (ThemeMode) -> Unit,
    setLang: (String) -> Unit,
) {
    val ctx = LocalContext.current
    val uri = LocalUriHandler.current
    var showGroupUpdateDialog by remember { mutableStateOf(false) }
    var keepGroupToken by remember { mutableStateOf(true) }
    var showUnknownSourcesDialog by remember { mutableStateOf(false) }
    var showTokenForm by remember { mutableStateOf(false) }
    LaunchedEffect(vm.hasOwnToken) {
        if (vm.hasOwnToken) showTokenForm = false
    }

    fun launchInstall(apk: File) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O && !ctx.packageManager.canRequestPackageInstalls()) {
            showUnknownSourcesDialog = true
            return
        }
        runCatching { ctx.startActivity(AppUpdater.installIntent(ctx, apk)) }
            .onFailure { vm.appUpdateError = it.message ?: it.toString() }
    }

    fun startDownloadOrAskGroup() {
        if (vm.hasBuiltInGroupToken || vm.hasSavedGroupToken) {
            keepGroupToken = true
            showGroupUpdateDialog = true
        } else {
            vm.downloadAppUpdate(keepGroupToken = false) { launchInstall(it) }
        }
    }

    // ---- 1. Appearance (Theme & Language) ----
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(14.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Text(stringResource(R.string.settings_appearance), style = MaterialTheme.typography.titleMedium)

            Text(stringResource(R.string.settings_theme), style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant)
            val themes = listOf(
                ThemeMode.SYSTEM to R.string.theme_system,
                ThemeMode.LIGHT to R.string.theme_light,
                ThemeMode.DARK to R.string.theme_dark,
            )
            SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth()) {
                for ((i, pair) in themes.withIndex()) {
                    val (mode, labelRes) = pair
                    SegmentedButton(
                        selected = themeMode == mode,
                        onClick = { setThemeMode(mode) },
                        shape = SegmentedButtonDefaults.itemShape(i, themes.size),
                    ) {
                        Text(stringResource(labelRes), maxLines = 1)
                    }
                }
            }

            HorizontalDivider(Modifier.padding(vertical = 2.dp))

            Text(stringResource(R.string.settings_language), style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant)
            val currentLang = Lang.current(ctx)
            val langs = listOf("bg" to "Български", "en" to "English")
            SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth()) {
                for ((i, pair) in langs.withIndex()) {
                    val (code, name) = pair
                    SegmentedButton(
                        selected = currentLang == code,
                        onClick = { if (code != currentLang) setLang(code) },
                        enabled = !vm.busy,
                        shape = SegmentedButtonDefaults.itemShape(i, langs.size),
                    ) {
                        Text(name, maxLines = 1)
                    }
                }
            }
        }
    }

    // ---- 2. App updates (GitHub Releases) ----
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(14.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth()) {
                Column(Modifier.weight(1f)) {
                    Text(stringResource(R.string.settings_updates), style = MaterialTheme.typography.titleMedium)
                    Text(
                        stringResource(R.string.settings_current_version, BuildConfig.VERSION_NAME, BuildConfig.VERSION_CODE),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
                val badgeText = when {
                    BuildConfig.IS_GROUP_APK -> stringResource(R.string.settings_build_group)
                    vm.hasSavedGroupToken -> stringResource(R.string.settings_build_public_group_kept)
                    else -> stringResource(R.string.settings_build_public)
                }
                Surface(
                    color = if (BuildConfig.IS_GROUP_APK || vm.hasSavedGroupToken) MaterialTheme.colorScheme.primaryContainer
                    else MaterialTheme.colorScheme.secondaryContainer,
                    shape = MaterialTheme.shapes.small,
                ) {
                    Text(
                        badgeText,
                        modifier = Modifier.padding(horizontal = 8.dp, vertical = 3.dp),
                        style = MaterialTheme.typography.labelSmall,
                        color = if (BuildConfig.IS_GROUP_APK || vm.hasSavedGroupToken) MaterialTheme.colorScheme.onPrimaryContainer
                        else MaterialTheme.colorScheme.onSecondaryContainer,
                    )
                }
            }

            Row(
                verticalAlignment = Alignment.CenterVertically,
                modifier = Modifier.fillMaxWidth().clickable { vm.updateAutoCheck(!vm.autoUpdateCheck) },
            ) {
                Text(stringResource(R.string.settings_auto_update), modifier = Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium)
                Switch(checked = vm.autoUpdateCheck, onCheckedChange = { vm.updateAutoCheck(it) })
            }

            Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
                OutlinedButton(
                    onClick = { vm.checkAppUpdate(manual = true) },
                    enabled = !vm.checkingAppUpdate && !vm.appUpdateDownloading,
                ) {
                    if (vm.checkingAppUpdate) {
                        CircularProgressIndicator(Modifier.size(16.dp), strokeWidth = 2.dp)
                        Spacer(Modifier.width(8.dp))
                    }
                    Text(stringResource(if (vm.checkingAppUpdate) R.string.settings_checking_update else R.string.settings_check_update))
                }
            }

            vm.appUpdateError?.let { err ->
                Card(
                    Modifier.fillMaxWidth(),
                    colors = CardDefaults.cardColors(
                        containerColor = MaterialTheme.colorScheme.errorContainer,
                        contentColor = MaterialTheme.colorScheme.onErrorContainer,
                    ),
                ) {
                    Row(Modifier.padding(10.dp), verticalAlignment = Alignment.Top) {
                        Icon(Icons.Default.Warning, null, modifier = Modifier.size(18.dp))
                        Spacer(Modifier.width(8.dp))
                        Text(stringResource(R.string.settings_update_failed, err), style = MaterialTheme.typography.bodySmall)
                    }
                }
            }

            val avail = vm.availableUpdate
            if (avail != null) {
                val readyFile = vm.downloadedApk?.takeIf { it.exists() }
                Card(
                    Modifier.fillMaxWidth(),
                    colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.primaryContainer),
                ) {
                    Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                        Text(
                            stringResource(R.string.settings_update_available, avail.version),
                            style = MaterialTheme.typography.titleSmall,
                            color = MaterialTheme.colorScheme.onPrimaryContainer,
                        )
                        if (avail.apkSize > 0) {
                            Text(
                                "${avail.apkName} · ${formatSize(avail.apkSize)}",
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onPrimaryContainer,
                            )
                        }
                        if (avail.notes.isNotEmpty()) {
                            Text(
                                avail.notes.take(400),
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onPrimaryContainer,
                            )
                        }
                        if (vm.appUpdateDownloading) {
                            LinearProgressIndicator(
                                progress = { vm.appUpdateProgress / 100f },
                                modifier = Modifier.fillMaxWidth(),
                            )
                            Text(
                                stringResource(R.string.settings_downloading_apk, vm.appUpdateProgress),
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onPrimaryContainer,
                            )
                        } else if (readyFile != null) {
                            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                Button(onClick = { launchInstall(readyFile) }) {
                                    Text(stringResource(R.string.settings_install_downloaded))
                                }
                                TextButton(onClick = { startDownloadOrAskGroup() }) {
                                    Text(stringResource(R.string.settings_redownload))
                                }
                            }
                        } else {
                            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                Button(onClick = { startDownloadOrAskGroup() }) {
                                    Text(stringResource(R.string.settings_download_install))
                                }
                                if (avail.htmlUrl.isNotEmpty()) {
                                    TextButton(onClick = { runCatching { uri.openUri(avail.htmlUrl) } }) {
                                        Text(stringResource(R.string.build_open_run))
                                    }
                                }
                            }
                        }
                    }
                }
            } else if (vm.appUpdateChecked && !vm.checkingAppUpdate && vm.appUpdateError == null) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Icon(Icons.Default.CheckCircle, null, tint = MaterialTheme.colorScheme.primary, modifier = Modifier.size(18.dp))
                    Spacer(Modifier.width(8.dp))
                    Text(
                        if (vm.latestAppRelease != null) stringResource(R.string.settings_up_to_date)
                        else stringResource(R.string.settings_no_app_release),
                        style = MaterialTheme.typography.bodyMedium,
                    )
                }
            }
        }
    }

    // ---- 3. GitHub token ----
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(14.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(stringResource(R.string.settings_github_token), style = MaterialTheme.typography.titleMedium)
            val statusText = when {
                vm.hasOwnToken -> stringResource(R.string.settings_token_own)
                vm.hasBuiltInGroupToken -> stringResource(R.string.settings_token_group_builtin)
                vm.hasSavedGroupToken -> stringResource(R.string.settings_token_group_saved)
                else -> stringResource(R.string.settings_token_none)
            }
            Text(statusText, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                if (vm.hasOwnToken) {
                    TextButton(onClick = { vm.forgetToken() }, enabled = !vm.busy) {
                        Text(stringResource(R.string.build_token_forget), color = MaterialTheme.colorScheme.error)
                    }
                } else {
                    OutlinedButton(onClick = { showTokenForm = !showTokenForm }, enabled = !vm.busy) {
                        Text(stringResource(if (showTokenForm) R.string.cancel else R.string.settings_token_add))
                    }
                }
                if (vm.hasSavedGroupToken && !vm.hasBuiltInGroupToken) {
                    TextButton(onClick = { vm.forgetSavedGroupToken() }, enabled = !vm.busy) {
                        Text(stringResource(R.string.settings_token_forget_group), color = MaterialTheme.colorScheme.error)
                    }
                }
            }
            if (showTokenForm && !vm.hasOwnToken) {
                HorizontalDivider(Modifier.padding(vertical = 2.dp))
                TokenForm(vm)
            }
        }
    }

    // ---- 4. Firmware cache ----
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(14.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(stringResource(R.string.settings_cache), style = MaterialTheme.typography.titleMedium)
            Text(
                pluralStringResource(R.plurals.settings_cache_summary, vm.cacheFileCount, vm.cacheFileCount, formatSize(vm.cacheBytes)),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Text(
                stringResource(R.string.settings_cache_note),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            OutlinedButton(
                onClick = { vm.clearFirmwareCache() },
                enabled = !vm.busy && vm.cacheBytes > 0L,
            ) {
                Text(stringResource(R.string.settings_cache_clear))
            }
        }
    }

    // ---- 5. About ----
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(14.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Text(stringResource(R.string.settings_about), style = MaterialTheme.typography.titleMedium)
            Text("LoRa OTA · v${BuildConfig.VERSION_NAME}", style = MaterialTheme.typography.bodyMedium)
            Text(vm.repo, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
            TextButton(onClick = { runCatching { uri.openUri("https://github.com/${vm.repo}") } }) {
                Icon(Icons.Default.Info, null, modifier = Modifier.size(16.dp))
                Spacer(Modifier.width(6.dp))
                Text(stringResource(R.string.settings_open_repo))
            }
        }
    }

    // Group APK warning & token preservation dialog
    if (showGroupUpdateDialog) {
        AlertDialog(
            onDismissRequest = { showGroupUpdateDialog = false },
            title = { Text(stringResource(R.string.settings_group_update_title)) },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    Text(stringResource(R.string.settings_group_update_text))
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        modifier = Modifier.fillMaxWidth().clickable { keepGroupToken = !keepGroupToken },
                    ) {
                        Checkbox(checked = keepGroupToken, onCheckedChange = { keepGroupToken = it })
                        Text(stringResource(R.string.settings_group_keep_token), style = MaterialTheme.typography.bodyMedium)
                    }
                    if (!keepGroupToken) {
                        Text(
                            stringResource(R.string.settings_group_drop_warning),
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.error,
                        )
                    }
                }
            },
            confirmButton = {
                TextButton(onClick = {
                    showGroupUpdateDialog = false
                    vm.downloadAppUpdate(keepGroupToken = keepGroupToken) { launchInstall(it) }
                }) {
                    Text(stringResource(R.string.settings_download_install))
                }
            },
            dismissButton = {
                TextButton(onClick = { showGroupUpdateDialog = false }) {
                    Text(stringResource(R.string.cancel))
                }
            },
        )
    }

    // Android 8+ "Install unknown apps" permission prompt
    if (showUnknownSourcesDialog) {
        AlertDialog(
            onDismissRequest = { showUnknownSourcesDialog = false },
            title = { Text(stringResource(R.string.settings_unknown_sources_title)) },
            text = { Text(stringResource(R.string.settings_unknown_sources_text)) },
            confirmButton = {
                TextButton(onClick = {
                    showUnknownSourcesDialog = false
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                        runCatching {
                            ctx.startActivity(
                                Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:${ctx.packageName}"))
                                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                            )
                        }
                    }
                }) {
                    Text(stringResource(R.string.settings_open_android_settings))
                }
            },
            dismissButton = {
                TextButton(onClick = { showUnknownSourcesDialog = false }) {
                    Text(stringResource(R.string.cancel))
                }
            },
        )
    }
}

private fun formatSize(bytes: Long): String = when {
    bytes >= 1024 * 1024 -> String.format(Locale.ROOT, "%.1f MB", bytes / (1024.0 * 1024.0))
    bytes >= 1024 -> String.format(Locale.ROOT, "%.0f KB", bytes / 1024.0)
    else -> "$bytes B"
}
