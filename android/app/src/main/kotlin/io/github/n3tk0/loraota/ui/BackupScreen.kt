package io.github.n3tk0.loraota.ui

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.Checkbox
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import io.github.n3tk0.loraota.R
import io.github.n3tk0.loraota.backup.Backup
import io.github.n3tk0.loraota.backup.BackupItem
import io.github.n3tk0.loraota.backup.DeviceKind
import io.github.n3tk0.loraota.backup.DiffRow
import io.github.n3tk0.loraota.backup.RepeaterBackup
import java.text.DateFormat
import java.util.Date

/** The device's backups: make one, restore one (after its differences), save one as a file, import one. */
@Composable
fun BackupScreen(vm: AppViewModel) {
    val restoring = vm.restoreFrom
    if (restoring != null) {
        RestoreView(vm, restoring)
        return
    }
    var exporting by remember { mutableStateOf<Backup?>(null) }
    var exportPassword by remember { mutableStateOf("") }
    var askPassword by remember { mutableStateOf(false) }
    var deleting by remember { mutableStateOf<Backup?>(null) }
    val save = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("application/json")) { uri ->
        val b = exporting
        if (uri != null && b != null) vm.exportBackup(b, uri, exportPassword)
        exporting = null
        exportPassword = ""
    }
    val open = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) vm.pickImport(uri)
    }
    val companion = vm.backupKind == DeviceKind.COMPANION
    Hint(
        stringResource(if (companion) R.string.backup_hint_companion_short else R.string.backup_hint_repeater_short),
        stringResource(if (companion) R.string.backup_hint_companion else R.string.backup_hint_repeater),
    )
    // a limit of the companion, not a danger: the neutral card
    if (!companion && vm.canReadRepeater == false) InfoCard(stringResource(R.string.backup_no_capture))
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        Button(onClick = { vm.backupNow() }, enabled = !vm.busy && (companion || vm.canReadRepeater != false)) {
            Text(stringResource(R.string.backup_now))
        }
        OutlinedButton(onClick = { open.launch(arrayOf("application/json", "text/plain", "application/octet-stream")) }, enabled = !vm.busy) {
            Text(stringResource(R.string.backup_import))
        }
    }

    val dev = vm.backupDevice
    val mine = vm.backups.filter { it.deviceKey == dev }
    val others = vm.backups.filter { it.deviceKey != dev }
    Text(stringResource(R.string.backup_this_device), style = MaterialTheme.typography.titleSmall, color = MaterialTheme.colorScheme.primary)
    if (mine.isEmpty()) Text(stringResource(R.string.backup_none), color = MaterialTheme.colorScheme.onSurfaceVariant)
    fun export(b: Backup) {
        exporting = b
        exportPassword = ""
        // the password first (only for a private key), then where the file goes
        if (b.privateKey == null) save.launch(fileName(b)) else askPassword = true
    }
    for (b in mine) BackupRow(vm, b, false, { vm.startRestore(b) }, { export(b) }, { deleting = b })
    if (others.isNotEmpty()) {
        Text(stringResource(R.string.backup_other_devices), style = MaterialTheme.typography.titleSmall, color = MaterialTheme.colorScheme.primary)
        for (b in others) BackupRow(vm, b, true, { vm.startRestore(b) }, { export(b) }, { deleting = b })
    }

    exporting?.takeIf { askPassword }?.let { b ->
        run {
            AlertDialog(
                onDismissRequest = { exporting = null; askPassword = false },
                title = { Text(stringResource(R.string.export_title)) },
                text = {
                    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                        Text(stringResource(R.string.export_text))
                        PasswordField(exportPassword, { exportPassword = it }, stringResource(R.string.export_password))
                    }
                },
                confirmButton = { TextButton(onClick = { askPassword = false; save.launch(fileName(b)) }) { Text(stringResource(R.string.export_save)) } },
                dismissButton = { TextButton(onClick = { exporting = null; askPassword = false }) { Text(stringResource(R.string.cancel)) } },
            )
        }
    }
    deleting?.let { b ->
        AlertDialog(
            onDismissRequest = { deleting = null },
            title = { Text(stringResource(R.string.backup_delete_title)) },
            text = { Text(stamp(b)) },
            confirmButton = { TextButton(onClick = { vm.deleteBackup(b); deleting = null }) { Text(stringResource(R.string.backup_delete)) } },
            dismissButton = { TextButton(onClick = { deleting = null }) { Text(stringResource(R.string.cancel)) } },
        )
    }
    if (vm.importText != null) {
        var pw by remember { mutableStateOf("") }
        AlertDialog(
            onDismissRequest = { vm.importText = null },
            title = { Text(stringResource(R.string.import_title)) },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text(stringResource(R.string.import_text))
                    PasswordField(pw, { pw = it }, stringResource(R.string.import_password))
                }
            },
            confirmButton = { TextButton(onClick = { vm.importWithPassword(pw) }, enabled = pw.isNotEmpty() && !vm.busy) { Text(stringResource(R.string.import_ok)) } },
            dismissButton = { TextButton(onClick = { vm.importWithPassword("") }, enabled = !vm.busy) { Text(stringResource(R.string.import_without_key)) } },
        )
    }
}

private fun fileName(b: Backup): String {
    val name = b.deviceName.replace(Regex("[^\\p{L}\\p{N}_-]+"), "_").trim('_').ifEmpty { "device" }
    return "backup-${b.kind.name.lowercase()}-$name-${java.text.SimpleDateFormat("yyyyMMdd-HHmm", java.util.Locale.ROOT).format(Date(b.time))}.json"
}

@Composable
private fun stamp(b: Backup): String {
    // in the app's language, not the phone's
    val locale = LocalConfiguration.current.locales[0]
    return DateFormat.getDateTimeInstance(DateFormat.MEDIUM, DateFormat.SHORT, locale).format(Date(b.time))
}

@Composable
private fun BackupRow(vm: AppViewModel, b: Backup, other: Boolean, onRestore: () -> Unit, onExport: () -> Unit, onDelete: () -> Unit) {
    var menu by remember { mutableStateOf(false) }
    Card(Modifier.fillMaxWidth()) {
        Row(Modifier.padding(start = 12.dp, top = 8.dp, bottom = 8.dp), verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f).clickable(enabled = !vm.busy, onClick = onRestore), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                Text(stamp(b), style = MaterialTheme.typography.titleMedium)
                Text((if (other) b.deviceName + " · " else "") +
                    stringResource(if (b.auto) R.string.backup_auto else R.string.backup_manual), style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant)
                Text(summary(b), style = MaterialTheme.typography.bodySmall)
            }
            IconButton(onClick = { menu = true }, enabled = !vm.busy) { Icon(Icons.Default.MoreVert, stringResource(R.string.menu)) }
            DropdownMenu(expanded = menu, onDismissRequest = { menu = false }) {
                DropdownMenuItem(text = { Text(stringResource(R.string.backup_restore)) }, onClick = { menu = false; onRestore() })
                DropdownMenuItem(text = { Text(stringResource(R.string.backup_export)) }, onClick = { menu = false; onExport() })
                DropdownMenuItem(text = { Text(stringResource(R.string.backup_delete)) }, onClick = { menu = false; onDelete() })
            }
        }
    }
}

/** "12 settings · 3 channels · 40 contacts · with the key". */
@Composable
private fun summary(b: Backup): String {
    val parts = ArrayList<String>()
    val s = b.count(BackupItem.GROUP_SETTING)
    parts.add(pluralStringResource(R.plurals.backup_settings, s, s))
    if (b.kind == DeviceKind.COMPANION) {
        val ch = b.count(BackupItem.GROUP_CHANNEL)
        val ct = b.count(BackupItem.GROUP_CONTACT)
        parts.add(pluralStringResource(R.plurals.backup_channels, ch, ch))
        parts.add(pluralStringResource(R.plurals.backup_contacts, ct, ct))
        parts.add(stringResource(if (b.privateKey != null) R.string.backup_with_key else R.string.backup_no_key))
    }
    return parts.joinToString(" · ")
}

/** What a restore would change, each with a box; all ticked at first. */
@Composable
private fun RestoreView(vm: AppViewModel, b: Backup) {
    Text(stringResource(R.string.restore_title, stamp(b)), style = MaterialTheme.typography.titleMedium)
    if (b.deviceKey != vm.backupDevice) DangerCard(stringResource(R.string.restore_other_device, b.deviceName))
    if (vm.restoreRows.isEmpty()) {
        Text(stringResource(R.string.restore_same))
        OutlinedButton(onClick = { vm.closeRestore() }, enabled = !vm.busy) { Text(stringResource(R.string.back)) }
        return
    }
    Text(stringResource(R.string.restore_hint), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
    val groups = listOf(
        R.string.group_settings to vm.restoreRows.filter { it.item.group == BackupItem.GROUP_KEY || it.item.group == BackupItem.GROUP_SETTING },
        R.string.group_channels to vm.restoreRows.filter { it.item.group == BackupItem.GROUP_CHANNEL },
        R.string.group_contacts to vm.restoreRows.filter { it.item.group == BackupItem.GROUP_CONTACT },
    )
    for ((title, rows) in groups) {
        if (rows.isEmpty()) continue
        Text(stringResource(title), style = MaterialTheme.typography.titleSmall, color = MaterialTheme.colorScheme.primary)
        for (r in rows) DiffLine(vm, b, r)
    }
    val n = vm.restoreRows.count { vm.restoreChecked[it.item.ref] == true }
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        Button(onClick = { vm.applyRestore() }, enabled = n > 0 && !vm.busy) { Text(stringResource(R.string.restore_apply, n)) }
        TextButton(onClick = { vm.closeRestore() }, enabled = !vm.busy) { Text(stringResource(R.string.cancel)) }
    }
}

@Composable
private fun DiffLine(vm: AppViewModel, b: Backup, r: DiffRow) {
    val ref = r.item.ref
    val on = vm.restoreChecked[ref] == true
    Row(verticalAlignment = Alignment.Top, modifier = Modifier
        .fillMaxWidth()
        .clickable(enabled = !vm.busy) { vm.restoreChecked[ref] = !on }) {
        Checkbox(checked = on, onCheckedChange = { vm.restoreChecked[ref] = it }, enabled = !vm.busy)
        Column(Modifier.weight(1f).padding(top = 12.dp), verticalArrangement = Arrangement.spacedBy(2.dp)) {
            Text(itemLabel(r.item), style = MaterialTheme.typography.bodyLarge)
            if (r.item.group != BackupItem.GROUP_CONTACT) {
                // the backup's value, and what the device has now
                Text(itemValue(r.item, r.item.value), style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.primary)
                Text(stringResource(R.string.restore_now, r.current?.let { itemValue(r.item, it) } ?: stringResource(R.string.restore_now_missing)),
                    style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
            if (r.item.group == BackupItem.GROUP_KEY) {
                Text(stringResource(R.string.restore_key_warning), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.error)
            }
            if (b.kind == DeviceKind.REPEATER && r.item.id in RepeaterBackup.NEEDS_RESTART) {
                Text(stringResource(R.string.restore_restart_note), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.error)
            }
        }
    }
}

/** A setting's name for people; a channel or contact by its own name. */
@Composable
private fun itemLabel(i: BackupItem): String = when (i.group) {
    BackupItem.GROUP_KEY -> stringResource(R.string.item_identity)
    BackupItem.GROUP_CHANNEL -> i.label.ifEmpty { "#${i.id}" }
    BackupItem.GROUP_CONTACT -> i.label.ifEmpty { i.id.take(12) }
    else -> when (i.id) {
        "name" -> stringResource(R.string.set_name)
        "radio" -> stringResource(R.string.set_radio)
        "tx" -> stringResource(R.string.set_tx)
        "latlon" -> stringResource(R.string.set_latlon)
        "other" -> stringResource(R.string.set_other)
        "pin" -> stringResource(R.string.set_pin)
        "hash", "path.hash.mode" -> stringResource(R.string.set_hash)
        "tuning" -> stringResource(R.string.set_tuning)
        "autoadd" -> stringResource(R.string.set_autoadd)
        "scope" -> stringResource(R.string.set_scope)
        "af" -> stringResource(R.string.set_af)
        "lat" -> stringResource(R.string.set_lat)
        "lon" -> stringResource(R.string.set_lon)
        "repeat" -> stringResource(R.string.set_repeat)
        "advert.interval" -> stringResource(R.string.set_advert_interval)
        "flood.advert.interval" -> stringResource(R.string.set_flood_advert_interval)
        "guest.password" -> stringResource(R.string.set_guest_password)
        "owner.info" -> stringResource(R.string.set_owner_info)
        "flood.max" -> stringResource(R.string.set_flood_max)
        RepeaterBackup.OTA -> stringResource(R.string.set_ota)
        else -> i.id
    }
}

/** A value as people read it: MHz and kHz, degrees, a scope's name. */
@Composable
private fun itemValue(i: BackupItem, v: String): String {
    val n = v.split(',').map { it.trim().toLongOrNull() }
    return when {
        i.group == BackupItem.GROUP_KEY -> v.take(16) + "…"
        i.group == BackupItem.GROUP_CHANNEL -> {
            val name = runCatching { String(ByteArray(32) { v.substring(it * 2, it * 2 + 2).toInt(16).toByte() }, Charsets.UTF_8).substringBefore('\u0000') }.getOrDefault("")
            name.ifEmpty { "—" } + " · " + v.drop(64).take(8) + "…"
        }
        i.id == "radio" && i.group == BackupItem.GROUP_SETTING && n.size >= 4 && n.all { it != null } && ',' in v && n[0]!! > 10_000 ->
            "%.3f MHz · %s kHz · SF%d · CR%d".format(n[0]!! / 1000.0, (n[1]!! / 1000.0).toString().removeSuffix(".0"), n[2], n[3]) +
                if ((n.getOrNull(4) ?: 0L) != 0L) " · " + stringResource(R.string.repeat_on) else ""
        i.id == "latlon" && n.size == 2 && n.all { it != null } -> "%.6f, %.6f".format(n[0]!! / 1e6, n[1]!! / 1e6)
        i.id == "tuning" && n.size == 2 && n.all { it != null } -> "rx %.2f · af %.2f".format(n[0]!! / 1000.0, n[1]!! / 1000.0)
        i.id == "scope" -> if (v.isEmpty()) stringResource(R.string.none_value)
            else runCatching { String(ByteArray(minOf(31, v.length / 2)) { v.substring(it * 2, it * 2 + 2).toInt(16).toByte() }, Charsets.UTF_8).substringBefore('\u0000') }.getOrDefault(v)
        i.id == "pin" && v == "0" -> stringResource(R.string.none_value)
        i.id == "guest.password" -> if (v.isEmpty()) stringResource(R.string.none_value) else "•".repeat(v.length.coerceAtMost(8))
        v.isEmpty() -> stringResource(R.string.none_value)
        else -> v
    }
}

@Composable
private fun PasswordField(value: String, onChange: (String) -> Unit, label: String) {
    OutlinedTextField(
        value = value, onValueChange = onChange, label = { Text(label) },
        visualTransformation = PasswordVisualTransformation(),
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password),
        singleLine = true, modifier = Modifier.fillMaxWidth(),
    )
}
