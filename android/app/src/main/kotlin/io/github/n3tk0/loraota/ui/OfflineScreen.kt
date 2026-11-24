package io.github.n3tk0.loraota.ui

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.KeyboardArrowDown
import androidx.compose.material.icons.filled.KeyboardArrowUp
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
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
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import io.github.n3tk0.loraota.R
import io.github.n3tk0.loraota.backup.Backup
import io.github.n3tk0.loraota.backup.DeviceKind
import io.github.n3tk0.loraota.flash.BoardCheck
import io.github.n3tk0.loraota.flash.Role
import io.github.n3tk0.loraota.ota.FirmwareFile
import io.github.n3tk0.loraota.ota.HealthCheck
import io.github.n3tk0.loraota.ota.HealthSeverity
import io.github.n3tk0.loraota.ota.HealthStore
import io.github.n3tk0.loraota.ota.SelfTest
import io.github.n3tk0.loraota.tools.AuditIssueKind
import io.github.n3tk0.loraota.tools.ConfigProfileLogic
import io.github.n3tk0.loraota.tools.DeltaReadinessStatus
import io.github.n3tk0.loraota.tools.FleetAuditLogic
import io.github.n3tk0.loraota.tools.MeshGeoTopology
import io.github.n3tk0.loraota.tools.OfflineDeltaInspector
import java.text.DateFormat
import java.util.Date
import java.util.Locale

/**
 * Offline hub: inspect saved health checks, device backups (with import, diff, save-as-profile),
 * prepare/build/download/export/flash firmware for repeaters, companions, or room servers,
 * and run offline LoRa OTA readiness, fleet security audits, and mesh link asymmetry analysis.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun OfflineScreen(vm: AppViewModel) {
    var tab by rememberSaveable { mutableIntStateOf(0) }
    var deleteCandidate by remember { mutableStateOf<Backup?>(null) }
    var deleteFirmwareCandidate by remember { mutableStateOf<FirmwareFile?>(null) }
    var showExportDialog by rememberSaveable { mutableStateOf(false) }
    var compareBase by remember { mutableStateOf<Backup?>(null) }
    var diffTarget by remember { mutableStateOf<Pair<Backup, Backup>?>(null) }
    var saveProfileTarget by remember { mutableStateOf<Backup?>(null) }
    var profileNameInput by rememberSaveable { mutableStateOf("") }

    val saveFile = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("application/json")) { uri ->
        val b = vm.exportingBackup
        val pw = vm.exportPassword
        vm.exportingBackup = null
        vm.exportPassword = ""
        if (uri != null && b != null) vm.exportBackup(b, uri, pw)
    }

    val importBackupFile = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) vm.importOfflineBackup(uri)
    }

    val saveFirmwareFile = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("application/octet-stream")) { uri ->
        val pending = vm.exportingFirmware
        vm.exportingFirmware = null
        if (uri != null && pending != null) {
            vm.exportCachedFirmware(pending.first, pending.second, uri)
        }
    }

    val auditFindings = remember(vm.offlineBackups, vm.offlineHealth, vm.knownDeviceNames) {
        val latestHealth = vm.offlineHealth.mapNotNull { (k, list) ->
            list.lastOrNull()?.let { k to it }
        }.toMap()
        FleetAuditLogic.audit(vm.offlineBackups, latestHealth, vm.knownDeviceNames)
    }

    val tabs = listOf(
        R.string.offline_tab_health to vm.offlineHealth.size,
        R.string.offline_tab_backups to vm.offlineBackups.size,
        R.string.offline_tab_firmware to vm.offlineFirmwares.size,
        R.string.offline_tab_audit to auditFindings.size,
    )
    SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth()) {
        for ((i, pair) in tabs.withIndex()) {
            val (labelRes, count) = pair
            SegmentedButton(
                selected = tab == i,
                onClick = { tab = i },
                shape = SegmentedButtonDefaults.itemShape(i, tabs.size),
            ) {
                Text("${stringResource(labelRes)} ($count)", maxLines = 1, style = MaterialTheme.typography.labelSmall)
            }
        }
    }

    when (tab) {
        0 -> OfflineHealthList(vm)
        1 -> OfflineBackupsList(
            backups = vm.offlineBackups,
            enabled = !vm.busy,
            compareBase = compareBase,
            onImportFile = { importBackupFile.launch(arrayOf("application/json", "text/plain", "*/*")) },
            onToggleCompare = { b ->
                val cur = compareBase
                when {
                    cur == null -> compareBase = b
                    cur.file == b.file && cur.time == b.time -> compareBase = null
                    else -> {
                        diffTarget = cur to b
                        compareBase = null
                    }
                }
            },
            onSaveAsProfile = { b ->
                profileNameInput = b.deviceName.ifEmpty { "Profile" }
                saveProfileTarget = b
            },
            onExport = { b ->
                vm.exportingBackup = b
                vm.exportPassword = ""
                showExportDialog = true
            },
            onDelete = { b -> deleteCandidate = b },
        )
        2 -> OfflineFirmwareTab(
            vm = vm,
            onFlashDirect = { f -> vm.startDirectOfflineFlash(f) },
            onExportFirmware = { f, dfu ->
                vm.exportingFirmware = f to dfu
                val name = (if (dfu) f.dfu else f.file).ifEmpty {
                    "${f.env}-${f.version.ifEmpty { f.release }}${if (dfu && f.dfuIsZip) ".zip" else ".bin"}"
                }
                saveFirmwareFile.launch(name)
            },
            onDeleteFirmware = { f -> deleteFirmwareCandidate = f },
        )
        else -> OfflineAuditAndMeshTab(vm = vm, auditFindings = auditFindings)
    }

    FieldPrepDialog(vm)

    // Encrypted backup import password prompt
    if (vm.importText != null && vm.step == Step.OFFLINE) {
        AlertDialog(
            onDismissRequest = {
                vm.importText = null
                vm.importPassword = ""
            },
            title = { Text(stringResource(R.string.offline_backup_import)) },
            text = {
                OutlinedTextField(
                    value = vm.importPassword,
                    onValueChange = { vm.importPassword = it },
                    label = { Text(stringResource(R.string.import_password)) },
                    visualTransformation = PasswordVisualTransformation(),
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
            },
            confirmButton = {
                TextButton(
                    onClick = { vm.importOfflineBackupWithPassword() },
                    enabled = vm.importPassword.isNotEmpty(),
                ) { Text(stringResource(R.string.import_ok)) }
            },
            dismissButton = {
                TextButton(onClick = {
                    vm.importText = null
                    vm.importPassword = ""
                }) { Text(stringResource(R.string.cancel)) }
            },
        )
    }

    // Save backup as reusable config profile dialog
    saveProfileTarget?.let { b ->
        AlertDialog(
            onDismissRequest = { saveProfileTarget = null },
            title = { Text(stringResource(R.string.offline_backup_save_profile)) },
            text = {
                OutlinedTextField(
                    value = profileNameInput,
                    onValueChange = { profileNameInput = it },
                    label = { Text(stringResource(R.string.profile_name_hint)) },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
            },
            confirmButton = {
                TextButton(
                    onClick = {
                        vm.saveBackupAsProfile(profileNameInput, b)
                        saveProfileTarget = null
                    },
                    enabled = profileNameInput.trim().isNotEmpty(),
                ) { Text(stringResource(R.string.offline_backup_save_profile)) }
            },
            dismissButton = {
                TextButton(onClick = { saveProfileTarget = null }) { Text(stringResource(R.string.cancel)) }
            },
        )
    }

    // Backup Diff comparison dialog
    diffTarget?.let { (b1, b2) ->
        val diffs = remember(b1, b2) { ConfigProfileLogic.diffBackups(b1, b2) }
        AlertDialog(
            onDismissRequest = { diffTarget = null },
            title = {
                Text(
                    stringResource(
                        R.string.offline_backup_diff_title,
                        b1.deviceName.ifEmpty { b1.deviceKey.take(8) },
                        b2.deviceName.ifEmpty { b2.deviceKey.take(8) },
                    ),
                )
            },
            text = {
                if (diffs.isEmpty()) {
                    Text(stringResource(R.string.offline_backup_diff_none))
                } else {
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(280.dp)
                            .verticalScroll(rememberScrollState()),
                        verticalArrangement = Arrangement.spacedBy(6.dp),
                    ) {
                        for (row in diffs) {
                            BackupDiffRowView(row)
                        }
                    }
                }
            },
            confirmButton = {
                TextButton(onClick = { diffTarget = null }) { Text(stringResource(R.string.build_close)) }
            },
        )
    }

    deleteFirmwareCandidate?.let { f ->
        AlertDialog(
            onDismissRequest = { deleteFirmwareCandidate = null },
            title = { Text(stringResource(R.string.offline_fw_delete_title)) },
            text = { Text(stringResource(R.string.offline_fw_delete_text, "${f.env.replace('_', ' ')} (${f.label})")) },
            confirmButton = {
                TextButton(onClick = {
                    vm.deleteOfflineFirmware(f)
                    deleteFirmwareCandidate = null
                }) { Text(stringResource(R.string.backup_delete), color = MaterialTheme.colorScheme.error) }
            },
            dismissButton = {
                TextButton(onClick = { deleteFirmwareCandidate = null }) { Text(stringResource(R.string.cancel)) }
            },
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
private fun BackupDiffRowView(row: Triple<String, String, String>) {
    Column(Modifier.fillMaxWidth()) {
        Text(
            row.first,
            style = MaterialTheme.typography.labelMedium,
            fontFamily = FontFamily.Monospace,
            color = MaterialTheme.colorScheme.primary,
        )
        Text(
            "${row.second}  →  ${row.third}",
            style = MaterialTheme.typography.bodySmall,
            fontFamily = FontFamily.Monospace,
        )
        HorizontalDivider(Modifier.padding(top = 4.dp))
    }
}

@Composable
private fun OfflineHealthList(vm: AppViewModel) {
    val locale = LocalConfiguration.current.locales[0]
    val fmt = remember(locale) { DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT, locale) }
    val dark = MaterialTheme.colorScheme.background.luminance() < 0.5f
    val warnTint = if (dark) Color(0xFFFFB74D) else Color(0xFF9A5B00)
    var query by rememberSaveable { mutableStateOf("") }

    if (vm.offlineHealth.isEmpty()) {
        InfoCard(stringResource(R.string.offline_no_health))
        return
    }

    OutlinedTextField(
        value = query,
        onValueChange = { query = it },
        placeholder = { Text(stringResource(R.string.offline_search_health)) },
        singleLine = true,
        modifier = Modifier.fillMaxWidth(),
    )

    val sorted = vm.offlineHealth.entries
        .sortedByDescending { it.value.lastOrNull()?.time ?: 0L }
        .filter { (key, _) ->
            val name = vm.knownDeviceNames[key] ?: ""
            val q = query.trim()
            q.isEmpty() || name.contains(q, ignoreCase = true) || key.contains(q, ignoreCase = true)
        }
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
    enabled: Boolean,
    compareBase: Backup?,
    onImportFile: () -> Unit,
    onToggleCompare: (Backup) -> Unit,
    onSaveAsProfile: (Backup) -> Unit,
    onExport: (Backup) -> Unit,
    onDelete: (Backup) -> Unit,
) {
    val locale = LocalConfiguration.current.locales[0]
    val fmt = remember(locale) { DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT, locale) }
    var query by rememberSaveable { mutableStateOf("") }

    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        OutlinedTextField(
            value = query,
            onValueChange = { query = it },
            placeholder = { Text(stringResource(R.string.offline_search_backups)) },
            singleLine = true,
            modifier = Modifier.weight(1f),
        )
        OutlinedButton(onClick = onImportFile, enabled = enabled) {
            Text(stringResource(R.string.offline_backup_import))
        }
    }

    if (compareBase != null) {
        Card(
            Modifier.fillMaxWidth(),
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.primaryContainer),
        ) {
            Row(
                Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 8.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(
                    stringResource(R.string.offline_backup_compare_with, compareBase.deviceName.ifEmpty { compareBase.deviceKey.take(8) }),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onPrimaryContainer,
                    modifier = Modifier.weight(1f),
                )
                TextButton(onClick = { onToggleCompare(compareBase) }) {
                    Text(stringResource(R.string.cancel))
                }
            }
        }
    }

    if (backups.isEmpty()) {
        InfoCard(stringResource(R.string.offline_no_backups))
        return
    }

    val filtered = backups.filter { b ->
        val q = query.trim()
        q.isEmpty() ||
            b.deviceName.contains(q, ignoreCase = true) ||
            b.firmware.contains(q, ignoreCase = true) ||
            b.deviceKey.contains(q, ignoreCase = true)
    }

    for (b in filtered) {
        val isCompareSelected = compareBase != null && compareBase.file == b.file && compareBase.time == b.time
        OfflineBackupCard(
            b = b,
            enabled = enabled,
            fmt = fmt,
            isCompareSelected = isCompareSelected,
            canCompare = backups.size >= 2,
            onToggleCompare = { onToggleCompare(b) },
            onSaveAsProfile = { onSaveAsProfile(b) },
            onExport = onExport,
            onDelete = onDelete,
        )
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun OfflineBackupCard(
    b: Backup,
    enabled: Boolean,
    fmt: DateFormat,
    isCompareSelected: Boolean,
    canCompare: Boolean,
    onToggleCompare: () -> Unit,
    onSaveAsProfile: () -> Unit,
    onExport: (Backup) -> Unit,
    onDelete: (Backup) -> Unit,
) {
    var expanded by rememberSaveable(b.file.ifEmpty { "${b.deviceKey}_${b.time}" }) { mutableStateOf(false) }

    Card(
        Modifier.fillMaxWidth(),
        colors = if (isCompareSelected) CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.secondaryContainer)
        else CardDefaults.cardColors(),
    ) {
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
                    val hiddenKeys = io.github.n3tk0.loraota.backup.PasswordBox.SECRET_SETTINGS + setOf("pin", "wifi.pwd")
                    val settings = b.items.filter {
                        it.group == io.github.n3tk0.loraota.backup.BackupItem.GROUP_SETTING &&
                            it.id !in hiddenKeys
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
            FlowRow(
                modifier = Modifier.fillMaxWidth().padding(horizontal = 8.dp, vertical = 2.dp),
                horizontalArrangement = Arrangement.spacedBy(4.dp),
            ) {
                TextButton(onClick = { onExport(b) }, enabled = enabled) {
                    Text(stringResource(R.string.backup_export))
                }
                if (canCompare) {
                    TextButton(onClick = onToggleCompare, enabled = enabled) {
                        Text(stringResource(R.string.offline_backup_compare))
                    }
                }
                if (b.kind == DeviceKind.REPEATER) {
                    TextButton(onClick = onSaveAsProfile, enabled = enabled) {
                        Text(stringResource(R.string.offline_backup_save_profile))
                    }
                }
                Spacer(Modifier.weight(1f))
                TextButton(onClick = { onDelete(b) }, enabled = enabled) {
                    Icon(Icons.Default.Delete, null, modifier = Modifier.size(16.dp), tint = MaterialTheme.colorScheme.error)
                    Spacer(Modifier.width(4.dp))
                    Text(stringResource(R.string.backup_delete), color = MaterialTheme.colorScheme.error)
                }
            }
        }
    }
}

/**
 * Offline Firmware Hub:
 * 1) Download/prepare ready firmwares from GitHub releases for any board/role
 * 2) Trigger a custom GitHub build for any device (Repeater, Companion, or Room server) — collapsed until opened
 *    so entering Offline without internet never blocks the UI or throws a network error
 * 3) Inspect, search/filter, flash directly (BLE DFU / Wi-Fi OTA), export (.bin / DFU .zip), or remove cached firmwares.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun OfflineFirmwareTab(
    vm: AppViewModel,
    onFlashDirect: (FirmwareFile) -> Unit,
    onExportFirmware: (FirmwareFile, Boolean) -> Unit,
    onDeleteFirmware: (FirmwareFile) -> Unit,
) {
    var buildExpanded by rememberSaveable { mutableStateOf(vm.buildJob != null) }
    var searchQuery by rememberSaveable { mutableStateOf("") }
    var roleFilter by rememberSaveable { mutableStateOf<Role?>(null) }

    // 1. Download / prepare ready firmwares for field use
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(stringResource(R.string.offline_fw_prepare_title), style = MaterialTheme.typography.titleMedium)
            Text(
                stringResource(R.string.offline_fw_prepare_desc),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Text(
                pluralStringResource(R.plurals.settings_cache_summary, vm.cacheFileCount, vm.cacheFileCount, formatSize(vm.cacheBytes)),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.primary,
            )
            vm.fieldPrepProgress?.let { progress ->
                Text(progress, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.primary)
                LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
            }
            Button(
                onClick = { vm.openFieldPrepPicker() },
                enabled = !vm.busy,
                modifier = Modifier.fillMaxWidth(),
            ) {
                Text(stringResource(R.string.offline_fw_download_btn))
            }
        }
    }

    // 2. Build custom firmware on GitHub for any device role (collapsed by default to stay truly offline-safe)
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.fillMaxWidth()) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                modifier = Modifier
                    .fillMaxWidth()
                    .clickable { buildExpanded = !buildExpanded }
                    .padding(12.dp),
            ) {
                Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                    Text(stringResource(R.string.offline_fw_build_title), style = MaterialTheme.typography.titleMedium)
                    Text(
                        stringResource(R.string.offline_fw_build_desc),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
                Icon(
                    if (buildExpanded) Icons.Default.KeyboardArrowUp else Icons.Default.KeyboardArrowDown,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            if (buildExpanded || vm.buildJob != null) {
                HorizontalDivider()
                Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    val roles = listOf(
                        "repeater" to R.string.role_repeater,
                        "companion" to R.string.role_companion,
                        "room_server" to R.string.role_room_server,
                    )
                    val buildRunning = vm.buildJob?.finished == false
                    FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                        for ((roleKey, labelRes) in roles) {
                            FilterChip(
                                selected = vm.offlineBuildRole == roleKey,
                                onClick = { vm.selectOfflineBuildRole(roleKey) },
                                enabled = !vm.busy && !buildRunning,
                                label = { Text(stringResource(labelRes), style = MaterialTheme.typography.labelMedium) },
                            )
                        }
                    }
                    BuildPanel(vm)
                }
            }
        }
    }

    // 3. Cached / built firmwares ready on this phone
    Text(
        stringResource(R.string.offline_fw_cached_title, vm.offlineFirmwares.size),
        style = MaterialTheme.typography.titleSmall,
        color = MaterialTheme.colorScheme.primary,
    )
    if (vm.offlineFirmwares.isEmpty()) {
        InfoCard(stringResource(R.string.offline_fw_none))
    } else {
        OutlinedTextField(
            value = searchQuery,
            onValueChange = { searchQuery = it },
            placeholder = { Text(stringResource(R.string.field_prep_search)) },
            singleLine = true,
            modifier = Modifier.fillMaxWidth(),
        )
        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            val roles = listOf(
                null to R.string.filter_all,
                Role.REPEATER to R.string.role_repeater,
                Role.COMPANION to R.string.role_companion,
                Role.ROOM_SERVER to R.string.role_room_server,
            )
            for ((role, labelRes) in roles) {
                FilterChip(
                    selected = roleFilter == role,
                    onClick = { roleFilter = role },
                    label = { Text(stringResource(labelRes), style = MaterialTheme.typography.labelSmall) },
                )
            }
        }
        val filtered = vm.offlineFirmwares.filter { item ->
            val f = item.firmware
            val q = searchQuery.trim()
            (roleFilter == null || BoardCheck.role(f.env) == roleFilter) &&
                (q.isEmpty() ||
                    f.env.replace('_', ' ').contains(q, ignoreCase = true) ||
                    f.env.contains(q, ignoreCase = true) ||
                    f.version.contains(q, ignoreCase = true) ||
                    f.release.contains(q, ignoreCase = true))
        }
        for (item in filtered) {
            CachedFirmwareCard(
                item = item,
                enabled = !vm.busy,
                onFlashDirect = { onFlashDirect(item.firmware) },
                onExport = { dfu -> onExportFirmware(item.firmware, dfu) },
                onDelete = { onDeleteFirmware(item.firmware) },
            )
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun CachedFirmwareCard(
    item: CachedFirmwareItem,
    enabled: Boolean,
    onFlashDirect: () -> Unit,
    onExport: (dfu: Boolean) -> Unit,
    onDelete: () -> Unit,
) {
    val f = item.firmware
    val roleLabelRes = when (BoardCheck.role(f.env)) {
        Role.REPEATER -> R.string.role_repeater
        Role.COMPANION -> R.string.role_companion
        Role.ROOM_SERVER -> R.string.role_room_server
        Role.OTHER -> null
    }
    // ESP32 repeaters use the same .bin for both LoRa OTA and Wi-Fi DFU (sha256 == dfuSha256): avoid double-counting
    val distinctDfu = item.hasCachedDfu && (!item.hasCachedOta || f.dfuSha256 != f.sha256)
    val totalBytes = (if (item.hasCachedOta) f.size.toLong() else 0L) +
        (if (distinctDfu) f.dfuSize.toLong() else 0L)
    val canFlashDirect = item.hasCachedDfu || (!f.dfuIsZip && item.hasCachedOta)

    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Text(f.env.replace('_', ' '), style = MaterialTheme.typography.titleMedium)
                val meta = buildList {
                    val row = f.rowLabel()
                    if (row.isNotEmpty()) add(row)
                    if (totalBytes > 0L) add(formatSize(totalBytes))
                }.joinToString(" · ")
                if (meta.isNotEmpty()) {
                    Text(meta, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
                FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    roleLabelRes?.let { res ->
                        Surface(
                            color = MaterialTheme.colorScheme.secondaryContainer,
                            shape = MaterialTheme.shapes.small,
                        ) {
                            Text(
                                stringResource(res),
                                modifier = Modifier.padding(horizontal = 8.dp, vertical = 2.dp),
                                style = MaterialTheme.typography.labelSmall,
                                color = MaterialTheme.colorScheme.onSecondaryContainer,
                            )
                        }
                    }
                    if (item.hasCachedOta) {
                        Surface(
                            color = MaterialTheme.colorScheme.primaryContainer,
                            shape = MaterialTheme.shapes.small,
                        ) {
                            Text(
                                stringResource(R.string.offline_fw_badge_ota),
                                modifier = Modifier.padding(horizontal = 8.dp, vertical = 2.dp),
                                style = MaterialTheme.typography.labelSmall,
                                color = MaterialTheme.colorScheme.onPrimaryContainer,
                            )
                        }
                    }
                    if (item.hasCachedDfu) {
                        Surface(
                            color = MaterialTheme.colorScheme.tertiaryContainer,
                            shape = MaterialTheme.shapes.small,
                        ) {
                            Text(
                                stringResource(R.string.offline_fw_badge_dfu),
                                modifier = Modifier.padding(horizontal = 8.dp, vertical = 2.dp),
                                style = MaterialTheme.typography.labelSmall,
                                color = MaterialTheme.colorScheme.onTertiaryContainer,
                            )
                        }
                    }
                }
            }
            HorizontalDivider()
            FlowRow(
                modifier = Modifier.fillMaxWidth().padding(horizontal = 8.dp, vertical = 2.dp),
                horizontalArrangement = Arrangement.spacedBy(4.dp),
            ) {
                if (canFlashDirect) {
                    TextButton(onClick = onFlashDirect, enabled = enabled) {
                        Text(stringResource(R.string.offline_fw_flash_direct))
                    }
                }
                if (item.hasCachedOta) {
                    TextButton(onClick = { onExport(false) }, enabled = enabled) {
                        Text(stringResource(R.string.offline_fw_export_ota))
                    }
                }
                if (distinctDfu) {
                    TextButton(onClick = { onExport(true) }, enabled = enabled) {
                        Text(stringResource(R.string.offline_fw_export_dfu))
                    }
                }
                Spacer(Modifier.weight(1f))
                TextButton(onClick = onDelete, enabled = enabled) {
                    Icon(Icons.Default.Delete, null, modifier = Modifier.size(16.dp), tint = MaterialTheme.colorScheme.error)
                    Spacer(Modifier.width(4.dp))
                    Text(stringResource(R.string.backup_delete), color = MaterialTheme.colorScheme.error)
                }
            }
        }
    }
}

/**
 * Offline Audit & Mesh Intelligence Tab:
 * 1) Offline LoRa OTA Delta Readiness Inspector (checks known repeaters against cached base & target .bin files)
 * 2) Automated Fleet Configuration & Security Audit (radio mismatch, lat/lon=0, repeat=off, weak guest access, low battery)
 * 3) Mesh Link Topology & SNR Asymmetry Matrix (Haversine distance + >= 6 dB bidirectional SNR delta flag).
 */
@Composable
private fun OfflineAuditAndMeshTab(
    vm: AppViewModel,
    auditFindings: List<io.github.n3tk0.loraota.tools.FleetAuditFinding>,
) {
    val dark = MaterialTheme.colorScheme.background.luminance() < 0.5f
    val warnTint = if (dark) Color(0xFFFFB74D) else Color(0xFF9A5B00)

    val deltaReadiness = remember(vm.offlineBackups, vm.offlineFirmwares) {
        val otaFirmwares = vm.offlineFirmwares.filter { it.hasCachedOta }.map { it.firmware }
        OfflineDeltaInspector.inspect(vm.offlineBackups, otaFirmwares) { true }
    }

    val topologyEdges = remember(vm.observedNeighbors, vm.offlineBackups, vm.knownDeviceNames) {
        val coords = MeshGeoTopology.extractCoordsFromBackups(vm.offlineBackups)
        MeshGeoTopology.buildTopologyEdges(vm.observedNeighbors, vm.knownDeviceNames, coords)
    }

    // 1. Offline LoRa OTA Delta Readiness
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(
                stringResource(R.string.audit_section_readiness, deltaReadiness.size),
                style = MaterialTheme.typography.titleMedium,
            )
            if (deltaReadiness.isEmpty()) {
                Text(
                    stringResource(R.string.audit_readiness_none),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            } else {
                for (r in deltaReadiness) {
                    val (icon, tint, detailText) = when (r.status) {
                        DeltaReadinessStatus.READY -> Triple(
                            Icons.Default.CheckCircle,
                            MaterialTheme.colorScheme.primary,
                            stringResource(R.string.audit_status_ready, r.currentVersion, r.targetVersion),
                        )
                        DeltaReadinessStatus.UP_TO_DATE -> Triple(
                            Icons.Default.CheckCircle,
                            MaterialTheme.colorScheme.primary,
                            stringResource(R.string.audit_status_uptodate, r.currentVersion),
                        )
                        DeltaReadinessStatus.MISSING_BASE -> Triple(
                            Icons.Default.Warning,
                            MaterialTheme.colorScheme.error,
                            stringResource(R.string.audit_status_missing_base, r.currentVersion),
                        )
                        DeltaReadinessStatus.MISSING_TARGET -> Triple(
                            Icons.Default.Warning,
                            warnTint,
                            stringResource(R.string.audit_status_missing_target),
                        )
                    }
                    Row(verticalAlignment = Alignment.Top, modifier = Modifier.fillMaxWidth()) {
                        Icon(icon, null, tint = tint, modifier = Modifier.size(18.dp))
                        Spacer(Modifier.width(8.dp))
                        Column(Modifier.weight(1f)) {
                            Text(r.deviceName, style = MaterialTheme.typography.bodyMedium)
                            Text(detailText, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                        }
                    }
                }
            }
        }
    }

    // 2. Fleet Configuration & Security Audit
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(
                stringResource(R.string.audit_section_findings, auditFindings.size),
                style = MaterialTheme.typography.titleMedium,
            )
            if (auditFindings.isEmpty()) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Icon(Icons.Default.CheckCircle, null, tint = MaterialTheme.colorScheme.primary, modifier = Modifier.size(18.dp))
                    Spacer(Modifier.width(8.dp))
                    Text(stringResource(R.string.audit_findings_ok), style = MaterialTheme.typography.bodySmall)
                }
            } else {
                for (f in auditFindings) {
                    val issueText = when (f.kind) {
                        AuditIssueKind.RADIO_MISMATCH -> stringResource(R.string.audit_issue_radio, f.detail)
                        AuditIssueKind.MISSING_COORDS -> stringResource(R.string.audit_issue_coords, f.detail)
                        AuditIssueKind.REPEAT_DISABLED -> stringResource(R.string.audit_issue_repeat, f.detail)
                        AuditIssueKind.WEAK_GUEST_ACCESS -> stringResource(R.string.audit_issue_guest, f.detail)
                        AuditIssueKind.LOW_BATTERY -> stringResource(R.string.audit_issue_battery, f.detail)
                    }
                    val isDanger = f.kind == AuditIssueKind.RADIO_MISMATCH ||
                        f.kind == AuditIssueKind.WEAK_GUEST_ACCESS ||
                        f.kind == AuditIssueKind.LOW_BATTERY
                    val tint = if (isDanger) MaterialTheme.colorScheme.error else warnTint
                    Row(verticalAlignment = Alignment.Top, modifier = Modifier.fillMaxWidth()) {
                        Icon(Icons.Default.Warning, null, tint = tint, modifier = Modifier.size(18.dp))
                        Spacer(Modifier.width(8.dp))
                        Column(Modifier.weight(1f)) {
                            Text(f.deviceName, style = MaterialTheme.typography.bodyMedium)
                            Text(issueText, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                        }
                    }
                }
            }
        }
    }

    // 3. Mesh Link Topology & SNR Asymmetry Matrix
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(
                stringResource(R.string.audit_section_topology, topologyEdges.size),
                style = MaterialTheme.typography.titleMedium,
            )
            if (topologyEdges.isEmpty()) {
                Text(
                    stringResource(R.string.audit_topology_none),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            } else {
                for (e in topologyEdges) {
                    Column(Modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                        Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth()) {
                            Text(
                                "${e.nodeAName} ↔ ${e.nodeBName}",
                                style = MaterialTheme.typography.bodyMedium,
                                modifier = Modifier.weight(1f),
                            )
                            e.distanceKm?.let { km ->
                                Text(
                                    String.format(Locale.ROOT, "%.1f km", km),
                                    style = MaterialTheme.typography.labelSmall,
                                    color = MaterialTheme.colorScheme.primary,
                                )
                            }
                        }
                        val snrLine = buildString {
                            append("A→B: ")
                            append(e.snrAtoBDb?.let { String.format(Locale.ROOT, "%+.1f dB", it) } ?: "—")
                            append("   ·   B→A: ")
                            append(e.snrBtoADb?.let { String.format(Locale.ROOT, "%+.1f dB", it) } ?: "—")
                        }
                        Text(
                            snrLine,
                            style = MaterialTheme.typography.bodySmall,
                            fontFamily = FontFamily.Monospace,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                        if (e.isAsymmetric) {
                            Text(
                                stringResource(R.string.audit_topology_asym),
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.error,
                            )
                        }
                        HorizontalDivider(Modifier.padding(top = 4.dp))
                    }
                }
            }
        }
    }
}


