package io.github.n3tk0.loraota.ui

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.KeyboardArrowDown
import androidx.compose.material.icons.filled.KeyboardArrowUp
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Card
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.luminance
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import io.github.n3tk0.loraota.R
import io.github.n3tk0.loraota.backup.Backup
import io.github.n3tk0.loraota.backup.DeviceKind
import io.github.n3tk0.loraota.ota.HealthCheck
import io.github.n3tk0.loraota.ota.HealthSeverity
import io.github.n3tk0.loraota.ota.HealthStore
import io.github.n3tk0.loraota.ota.SelfTest
import java.text.DateFormat
import java.util.Date
import java.util.Locale

/**
 * Offline inspection of saved health checks (HealthStore) and device backups (BackupStore)
 * without needing an active Bluetooth connection to a companion.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun OfflineScreen(vm: AppViewModel) {
    var tab by rememberSaveable { mutableIntStateOf(0) }
    var deleteCandidate by remember { mutableStateOf<Backup?>(null) }
    var showExportDialog by rememberSaveable { mutableStateOf(false) }

    val saveFile = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("application/json")) { uri ->
        val b = vm.exportingBackup
        val pw = vm.exportPassword
        vm.exportingBackup = null
        vm.exportPassword = ""
        if (uri != null && b != null) vm.exportBackup(b, uri, pw)
    }

    val tabs = listOf(
        R.string.offline_tab_health to vm.offlineHealth.size,
        R.string.offline_tab_backups to vm.offlineBackups.size,
    )
    SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth()) {
        for ((i, pair) in tabs.withIndex()) {
            val (labelRes, count) = pair
            SegmentedButton(
                selected = tab == i,
                onClick = { tab = i },
                shape = SegmentedButtonDefaults.itemShape(i, tabs.size),
            ) {
                Text("${stringResource(labelRes)} ($count)", maxLines = 1)
            }
        }
    }

    if (tab == 0) {
        OfflineHealthList(vm)
    } else {
        OfflineBackupsList(
            backups = vm.offlineBackups,
            onExport = { b ->
                vm.exportingBackup = b
                vm.exportPassword = ""
                showExportDialog = true
            },
            onDelete = { b -> deleteCandidate = b },
        )
    }

    deleteCandidate?.let { b ->
        AlertDialog(
            onDismissRequest = { deleteCandidate = null },
            title = { Text(stringResource(R.string.backup_delete_title)) },
            text = { Text(stringResource(R.string.backup_delete_text, b.deviceName)) },
            confirmButton = {
                TextButton(onClick = {
                    vm.deleteOfflineBackup(b)
                    deleteCandidate = null
                }) { Text(stringResource(R.string.backup_delete), color = MaterialTheme.colorScheme.error) }
            },
            dismissButton = {
                TextButton(onClick = { deleteCandidate = null }) { Text(stringResource(R.string.cancel)) }
            },
        )
    }

    val exportTarget = vm.exportingBackup
    if (showExportDialog && exportTarget != null) {
        AlertDialog(
            onDismissRequest = {
                showExportDialog = false
                vm.exportingBackup = null
                vm.exportPassword = ""
            },
            title = { Text(stringResource(R.string.backup_export)) },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text(stringResource(R.string.backup_export_hint), style = MaterialTheme.typography.bodySmall)
                    OutlinedTextField(
                        value = vm.exportPassword,
                        onValueChange = { vm.exportPassword = it },
                        label = { Text(stringResource(R.string.backup_password_optional)) },
                        visualTransformation = PasswordVisualTransformation(),
                        singleLine = true,
                        modifier = Modifier.fillMaxWidth(),
                    )
                }
            },
            confirmButton = {
                TextButton(onClick = {
                    showExportDialog = false
                    val safe = exportTarget.deviceName.replace(Regex("[^A-Za-z0-9._-]"), "_").ifEmpty { "meshcore" }
                    saveFile.launch("$safe-backup.json")
                }) { Text(stringResource(R.string.backup_export)) }
            },
            dismissButton = {
                TextButton(onClick = {
                    showExportDialog = false
                    vm.exportingBackup = null
                    vm.exportPassword = ""
                }) { Text(stringResource(R.string.cancel)) }
            },
        )
    }
}

@Composable
private fun OfflineHealthList(vm: AppViewModel) {
    val locale = LocalConfiguration.current.locales[0]
    val fmt = remember(locale) { DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT, locale) }
    val dark = MaterialTheme.colorScheme.background.luminance() < 0.5f
    val warnTint = if (dark) Color(0xFFFFB74D) else Color(0xFF9A5B00)

    if (vm.offlineHealth.isEmpty()) {
        InfoCard(stringResource(R.string.offline_no_health))
        return
    }

    val sorted = vm.offlineHealth.entries.sortedByDescending { it.value.lastOrNull()?.time ?: 0L }
    for ((key, records) in sorted) {
        OfflineHealthDeviceCard(
            deviceName = vm.knownDeviceNames[key] ?: key.take(12),
            keyHex = key,
            records = records,
            fmt = fmt,
            warnTint = warnTint,
        )
    }
}

@Composable
private fun OfflineHealthDeviceCard(
    deviceName: String,
    keyHex: String,
    records: List<HealthStore.Record>,
    fmt: DateFormat,
    warnTint: Color,
) {
    var expanded by rememberSaveable(keyHex) { mutableStateOf(false) }
    val latest = records.lastOrNull() ?: return
    val parsed = SelfTest.parse(latest.line)
    val prev = records.getOrNull(records.size - 2)?.let { SelfTest.parse(it.line) }
    val problems = parsed?.let { HealthCheck.problems(HealthCheck.warnings(it, prev)) }.orEmpty()
    val hasDanger = problems.any { it.severity == HealthSeverity.DANGER }
    val hasWarn = problems.any { it.severity == HealthSeverity.WARN } || (parsed?.ok == false)
    val tint = when {
        hasDanger -> MaterialTheme.colorScheme.error
        hasWarn -> warnTint
        else -> MaterialTheme.colorScheme.primary
    }

    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.fillMaxWidth()) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                modifier = Modifier
                    .fillMaxWidth()
                    .clickable { expanded = !expanded }
                    .padding(12.dp),
            ) {
                Icon(
                    if (hasDanger || hasWarn) Icons.Default.Warning else Icons.Default.CheckCircle,
                    contentDescription = null,
                    tint = tint,
                    modifier = Modifier.size(22.dp),
                )
                Spacer(Modifier.width(10.dp))
                Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                    Text(deviceName, style = MaterialTheme.typography.titleMedium)
                    val summary = buildList {
                        parsed?.batteryMv?.let { add(String.format(Locale.ROOT, "%.2f V", it / 1000.0)) }
                        parsed?.batteryPercent?.let { add("$it%") }
                        add(fmt.format(Date(latest.time)))
                    }.joinToString(" · ")
                    Text(summary, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
                Icon(
                    if (expanded) Icons.Default.KeyboardArrowUp else Icons.Default.KeyboardArrowDown,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            if (expanded) {
                HorizontalDivider()
                Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    for (r in records.asReversed().take(10)) {
                        val t = SelfTest.parse(r.line)
                        val desc = buildString {
                            if (t == null) {
                                append(r.line.take(60))
                            } else {
                                append(if (t.ok) stringResource(R.string.hv_fine) else t.problems.joinToString(", ").ifEmpty { "WARN" })
                                t.batteryMv?.let { append(String.format(Locale.ROOT, " · %.2f V", it / 1000.0)) }
                                t.batteryPercent?.let { append(" $it%") }
                                t.values["up"]?.let { append(" · up $it") }
                            }
                        }
                        Row(Modifier.fillMaxWidth()) {
                            Text(
                                fmt.format(Date(r.time)),
                                modifier = Modifier.weight(0.38f),
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                            Text(desc, modifier = Modifier.weight(0.62f), style = MaterialTheme.typography.bodySmall)
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun OfflineBackupsList(
    backups: List<Backup>,
    onExport: (Backup) -> Unit,
    onDelete: (Backup) -> Unit,
) {
    val locale = LocalConfiguration.current.locales[0]
    val fmt = remember(locale) { DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT, locale) }

    if (backups.isEmpty()) {
        InfoCard(stringResource(R.string.offline_no_backups))
        return
    }

    for (b in backups) {
        OfflineBackupCard(b = b, fmt = fmt, onExport = onExport, onDelete = onDelete)
    }
}

@Composable
private fun OfflineBackupCard(
    b: Backup,
    fmt: DateFormat,
    onExport: (Backup) -> Unit,
    onDelete: (Backup) -> Unit,
) {
    var expanded by rememberSaveable(b.file.ifEmpty { "${b.deviceKey}_${b.time}" }) { mutableStateOf(false) }

    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.fillMaxWidth()) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                modifier = Modifier
                    .fillMaxWidth()
                    .clickable { expanded = !expanded }
                    .padding(12.dp),
            ) {
                Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                    Text(b.deviceName.ifEmpty { b.deviceKey.take(12) }, style = MaterialTheme.typography.titleMedium)
                    Text(
                        "${fmt.format(Date(b.time))} · ${pluralStringResource(R.plurals.offline_backup_items, b.items.size, b.items.size)}",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                    if (b.firmware.isNotEmpty()) {
                        Text(b.firmware, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                }
                Surface(
                    color = MaterialTheme.colorScheme.secondaryContainer,
                    shape = MaterialTheme.shapes.small,
                ) {
                    Text(
                        stringResource(if (b.kind == DeviceKind.COMPANION) R.string.companion else R.string.offline_kind_repeater),
                        modifier = Modifier.padding(horizontal = 8.dp, vertical = 2.dp),
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSecondaryContainer,
                    )
                }
                Spacer(Modifier.width(4.dp))
                Icon(
                    if (expanded) Icons.Default.KeyboardArrowUp else Icons.Default.KeyboardArrowDown,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            if (expanded) {
                HorizontalDivider()
                Column(Modifier.padding(horizontal = 12.dp, vertical = 8.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    val settings = b.items.filter {
                        it.group == io.github.n3tk0.loraota.backup.BackupItem.GROUP_SETTING &&
                            it.id !in io.github.n3tk0.loraota.backup.PasswordBox.SECRET_SETTINGS
                    }
                    val channels = b.items.filter { it.group == io.github.n3tk0.loraota.backup.BackupItem.GROUP_CHANNEL }
                    val contacts = b.items.count { it.group == io.github.n3tk0.loraota.backup.BackupItem.GROUP_CONTACT }

                    for (s in settings) {
                        Row(Modifier.fillMaxWidth()) {
                            Text(
                                s.id,
                                modifier = Modifier.weight(0.42f),
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                            Text(
                                s.value,
                                modifier = Modifier.weight(0.58f),
                                style = MaterialTheme.typography.bodySmall,
                            )
                        }
                    }
                    if (channels.isNotEmpty()) {
                        Text(
                            stringResource(R.string.offline_backup_channels, channels.joinToString(", ") { it.label.ifEmpty { "#${it.id}" } }),
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                    if (contacts > 0) {
                        Text(
                            pluralStringResource(R.plurals.offline_backup_contacts, contacts, contacts),
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                }
            }
            HorizontalDivider()
            Row(
                modifier = Modifier.fillMaxWidth().padding(horizontal = 8.dp, vertical = 2.dp),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                TextButton(onClick = { onExport(b) }) {
                    Text(stringResource(R.string.backup_export))
                }
                Spacer(Modifier.weight(1f))
                TextButton(onClick = { onDelete(b) }) {
                    Icon(Icons.Default.Delete, null, modifier = Modifier.size(16.dp), tint = MaterialTheme.colorScheme.error)
                    Spacer(Modifier.width(4.dp))
                    Text(stringResource(R.string.backup_delete), color = MaterialTheme.colorScheme.error)
                }
            }
        }
    }
}
