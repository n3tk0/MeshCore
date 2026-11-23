package io.github.n3tk0.loraota.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.KeyboardArrowDown
import androidx.compose.material.icons.filled.KeyboardArrowUp
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
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
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import io.github.n3tk0.loraota.R
import io.github.n3tk0.loraota.backup.DeviceKind
import java.util.Locale

/** Quick one-tap repeater maintenance actions on the Overview tab. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun QuickActionsCard(vm: AppViewModel) {
    var confirmReboot by remember { mutableStateOf(false) }

    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(stringResource(R.string.qa_title), style = MaterialTheme.typography.titleMedium)
            FlowRow(
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                OutlinedButton(onClick = { vm.syncRepeaterClock() }, enabled = !vm.busy) {
                    Text(stringResource(R.string.qa_clock_sync))
                }
                OutlinedButton(onClick = { vm.sendRepeaterAdvert(flood = true) }, enabled = !vm.busy) {
                    Text(stringResource(R.string.qa_advert))
                }
                OutlinedButton(onClick = { vm.recheck() }, enabled = !vm.busy) {
                    Text(stringResource(R.string.qa_refresh_status))
                }
                OutlinedButton(onClick = { confirmReboot = true }, enabled = !vm.busy) {
                    Text(stringResource(R.string.qa_reboot), color = MaterialTheme.colorScheme.error)
                }
            }
            vm.lastClockReply?.let { reply ->
                Text(
                    reply,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }

    if (confirmReboot) {
        AlertDialog(
            onDismissRequest = { confirmReboot = false },
            title = { Text(stringResource(R.string.qa_reboot_title)) },
            text = { Text(stringResource(R.string.qa_reboot_text, vm.repeater?.name ?: "")) },
            confirmButton = {
                TextButton(onClick = {
                    confirmReboot = false
                    vm.rebootRepeater()
                }) {
                    Text(stringResource(R.string.qa_reboot), color = MaterialTheme.colorScheme.error)
                }
            },
            dismissButton = {
                TextButton(onClick = { confirmReboot = false }) { Text(stringResource(R.string.cancel)) }
            },
        )
    }
}

/** Expandable neighbors list on the Overview tab (loads via `neighbors` CLI on tap/refresh). */
@Composable
fun NeighborsCard(vm: AppViewModel) {
    var expanded by rememberSaveable(vm.repeater?.name) { mutableStateOf(false) }
    val dark = MaterialTheme.colorScheme.background.luminance() < 0.5f

    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.fillMaxWidth()) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                modifier = Modifier
                    .fillMaxWidth()
                    .clickable {
                        val next = !expanded
                        expanded = next
                        if (next && !vm.neighborsLoaded && !vm.busy) vm.loadNeighbors()
                    }
                    .padding(horizontal = 12.dp, vertical = 10.dp),
            ) {
                Column(Modifier.weight(1f)) {
                    Text(
                        if (vm.neighborsLoaded) "${stringResource(R.string.neighbors_title)} (${vm.neighbors.size})"
                        else stringResource(R.string.neighbors_title),
                        style = MaterialTheme.typography.titleMedium,
                    )
                    Text(
                        if (vm.neighborsLoaded) stringResource(R.string.neighbors_subtitle_loaded)
                        else stringResource(R.string.neighbors_subtitle_tap),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
                if (expanded) {
                    IconButton(onClick = { vm.loadNeighbors() }, enabled = !vm.busy) {
                        Icon(Icons.Default.Refresh, stringResource(R.string.refresh))
                    }
                }
                Icon(
                    if (expanded) Icons.Default.KeyboardArrowUp else Icons.Default.KeyboardArrowDown,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            if (expanded) {
                HorizontalDivider()
                Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    val err = vm.neighborsError
                    when {
                        err != null -> {
                            Row(verticalAlignment = Alignment.Top) {
                                Icon(Icons.Default.Warning, null, modifier = Modifier.size(18.dp),
                                    tint = MaterialTheme.colorScheme.error)
                                Spacer(Modifier.width(8.dp))
                                Text(err, style = MaterialTheme.typography.bodySmall)
                            }
                        }
                        !vm.neighborsLoaded -> {
                            Text(
                                stringResource(R.string.working_neighbors),
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                        vm.neighbors.isEmpty() -> {
                            Text(
                                stringResource(R.string.neighbors_none),
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                        else -> {
                            for (n in vm.neighbors) {
                                val snrColor = when {
                                    n.snrDb >= 0f -> MaterialTheme.colorScheme.primary
                                    n.snrDb >= -8f -> if (dark) Color(0xFFFFB74D) else Color(0xFF9A5B00)
                                    else -> MaterialTheme.colorScheme.error
                                }
                                Row(
                                    modifier = Modifier.fillMaxWidth(),
                                    verticalAlignment = Alignment.CenterVertically,
                                ) {
                                    Column(Modifier.weight(1f)) {
                                        Text(
                                            n.resolvedName ?: n.prefixHex.uppercase(Locale.ROOT),
                                            style = MaterialTheme.typography.bodyMedium,
                                        )
                                        val sub = buildString {
                                            if (n.resolvedName != null) append("${n.prefixHex} · ")
                                            append(formatSecsAgo(n.secsAgo))
                                        }
                                        Text(
                                            sub,
                                            style = MaterialTheme.typography.bodySmall,
                                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                                        )
                                    }
                                    Surface(
                                        color = snrColor.copy(alpha = 0.14f),
                                        shape = MaterialTheme.shapes.small,
                                    ) {
                                        Text(
                                            String.format(Locale.ROOT, "SNR %+.1f dB", n.snrDb),
                                            modifier = Modifier.padding(horizontal = 8.dp, vertical = 3.dp),
                                            style = MaterialTheme.typography.labelSmall,
                                            color = snrColor,
                                        )
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun formatSecsAgo(secs: Long): String = when {
    secs < 60 -> stringResource(R.string.ago_secs, secs.toInt())
    secs < 3600 -> stringResource(R.string.ago_mins, (secs / 60).toInt())
    secs < 86400 -> stringResource(R.string.ago_hours, (secs / 3600).toInt())
    else -> stringResource(R.string.ago_days, (secs / 86400).toInt())
}

/** Tools tab inside a logged-in repeater: CLI Console | Visual Settings | Backup/Restore. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun RepeaterToolsTab(vm: AppViewModel) {
    var toolSection by rememberSaveable(vm.repeater?.name) { mutableIntStateOf(0) }
    val sections = listOf(
        R.string.tools_cli,
        R.string.tools_settings,
        R.string.backup_open,
    )

    SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth()) {
        for ((i, labelRes) in sections.withIndex()) {
            SegmentedButton(
                selected = toolSection == i,
                onClick = { toolSection = i },
                shape = SegmentedButtonDefaults.itemShape(i, sections.size),
            ) {
                Text(stringResource(labelRes), maxLines = 1)
            }
        }
    }

    when (toolSection) {
        0 -> CliConsole(vm, isCompanion = false)
        1 -> RepeaterSettingsEditor(vm)
        2 -> {
            Card(Modifier.fillMaxWidth()) {
                Column(Modifier.padding(14.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    Text(stringResource(R.string.tools_backup_title), style = MaterialTheme.typography.titleMedium)
                    Text(
                        stringResource(R.string.tools_backup_desc),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                    Button(
                        onClick = { vm.openBackup(DeviceKind.REPEATER) },
                        enabled = !vm.busy,
                        modifier = Modifier.fillMaxWidth(),
                    ) {
                        Text(stringResource(R.string.backup_open))
                    }
                }
            }
        }
    }
}

private data class SettingFieldSpec(val key: String, val labelRes: Int, val isBool: Boolean = false)

private val CORE_SETTING_SPECS = listOf(
    SettingFieldSpec("name", R.string.rset_name),
    SettingFieldSpec("owner.info", R.string.rset_owner),
    SettingFieldSpec("repeat", R.string.rset_repeat, isBool = true),
    SettingFieldSpec("tx", R.string.rset_tx),
    SettingFieldSpec("advert.interval", R.string.rset_advert_int),
    SettingFieldSpec("flood.advert.interval", R.string.rset_flood_advert_int),
    SettingFieldSpec("lat", R.string.rset_lat),
    SettingFieldSpec("lon", R.string.rset_lon),
    SettingFieldSpec("guest.password", R.string.rset_guest_pw),
    SettingFieldSpec("allow.read.only", R.string.rset_allow_ro, isBool = true),
    SettingFieldSpec("multi.acks", R.string.rset_multi_acks, isBool = true),
    SettingFieldSpec("cad", R.string.rset_cad, isBool = true),
)

@Composable
private fun RepeaterSettingsEditor(vm: AppViewModel) {
    var showAdvanced by rememberSaveable { mutableStateOf(false) }

    if (vm.canReadRepeater == false) {
        InfoCard(stringResource(R.string.health_no_capture))
        return
    }

    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth()) {
                Column(Modifier.weight(1f)) {
                    Text(stringResource(R.string.rset_title), style = MaterialTheme.typography.titleMedium)
                    Text(
                        stringResource(R.string.rset_subtitle),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
                OutlinedButton(onClick = { vm.loadRepeaterSettings() }, enabled = !vm.busy) {
                    Text(stringResource(if (vm.repeaterSettingsLoaded) R.string.refresh else R.string.rset_load))
                }
            }
            vm.repeaterSettingsProgress?.let { (cur, total) ->
                if (total > 0) {
                    LinearProgressIndicator(
                        progress = { cur.toFloat() / total.toFloat() },
                        modifier = Modifier.fillMaxWidth(),
                    )
                }
            }
        }
    }

    if (!vm.repeaterSettingsLoaded) return

    val changed = vm.changedRepeaterSettings()
    val coreKeys = remember { CORE_SETTING_SPECS.map { it.key }.toSet() }

    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            for (spec in CORE_SETTING_SPECS) {
                if (spec.key !in vm.repeaterSettings) continue
                val current = vm.repeaterSettingsDraft[spec.key] ?: ""
                val dirty = vm.repeaterSettings[spec.key] != current
                if (spec.isBool) {
                    val on = current.equals("on", ignoreCase = true) || current == "1" || current.equals("true", ignoreCase = true)
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Column(Modifier.weight(1f)) {
                            Text(
                                stringResource(spec.labelRes) + if (dirty) " *" else "",
                                style = MaterialTheme.typography.bodyMedium,
                                color = if (dirty) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurface,
                            )
                            Text(spec.key, style = MaterialTheme.typography.labelSmall,
                                fontFamily = FontFamily.Monospace, color = MaterialTheme.colorScheme.onSurfaceVariant)
                        }
                        Switch(
                            checked = on,
                            onCheckedChange = { checked ->
                                val orig = vm.repeaterSettings[spec.key] ?: ""
                                val nextVal = if (orig == "0" || orig == "1") {
                                    if (checked) "1" else "0"
                                } else {
                                    if (checked) "on" else "off"
                                }
                                vm.updateRepeaterSettingDraft(spec.key, nextVal)
                            },
                            enabled = !vm.busy,
                        )
                    }
                } else {
                    OutlinedTextField(
                        value = current,
                        onValueChange = { vm.updateRepeaterSettingDraft(spec.key, it) },
                        label = {
                            Text(stringResource(spec.labelRes) + " (${spec.key})" + if (dirty) " *" else "")
                        },
                        singleLine = true,
                        enabled = !vm.busy,
                        modifier = Modifier.fillMaxWidth(),
                    )
                }
            }
        }
    }

    // Other loaded keys (radio, af, flood.max, delays, etc.)
    val otherKeys = vm.repeaterSettings.keys.filter { it !in coreKeys }.sorted()
    if (otherKeys.isNotEmpty()) {
        Card(Modifier.fillMaxWidth()) {
            Column(Modifier.fillMaxWidth()) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier
                        .fillMaxWidth()
                        .clickable { showAdvanced = !showAdvanced }
                        .padding(12.dp),
                ) {
                    Text(
                        stringResource(R.string.rset_advanced, otherKeys.size),
                        style = MaterialTheme.typography.titleSmall,
                        modifier = Modifier.weight(1f),
                    )
                    Icon(
                        if (showAdvanced) Icons.Default.KeyboardArrowUp else Icons.Default.KeyboardArrowDown,
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
                if (showAdvanced) {
                    HorizontalDivider()
                    Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                        for (k in otherKeys) {
                            val current = vm.repeaterSettingsDraft[k] ?: ""
                            val dirty = vm.repeaterSettings[k] != current
                            OutlinedTextField(
                                value = current,
                                onValueChange = { vm.updateRepeaterSettingDraft(k, it) },
                                label = { Text(if (dirty) "$k *" else k, fontFamily = FontFamily.Monospace) },
                                singleLine = true,
                                enabled = !vm.busy,
                                modifier = Modifier.fillMaxWidth(),
                            )
                        }
                    }
                }
            }
        }
    }

    if (changed.isNotEmpty()) {
        Card(
            Modifier.fillMaxWidth(),
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.primaryContainer),
        ) {
            Row(
                modifier = Modifier.fillMaxWidth().padding(12.dp),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Button(
                    onClick = { vm.saveRepeaterSettings() },
                    enabled = !vm.busy,
                    modifier = Modifier.weight(1f),
                ) {
                    Text(stringResource(R.string.rset_save_changes, changed.size))
                }
                TextButton(onClick = { vm.resetRepeaterSettingsDraft() }, enabled = !vm.busy) {
                    Text(stringResource(R.string.rset_reset))
                }
            }
        }
    }
}
