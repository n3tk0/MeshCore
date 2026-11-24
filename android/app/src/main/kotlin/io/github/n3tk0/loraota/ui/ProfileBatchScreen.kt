package io.github.n3tk0.loraota.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowForward
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.Checkbox
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.luminance
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import io.github.n3tk0.loraota.R
import io.github.n3tk0.loraota.ota.hex
import io.github.n3tk0.loraota.tools.BatchResult
import io.github.n3tk0.loraota.tools.ProfileBatchState
import io.github.n3tk0.loraota.tools.ProfilePlanner

/**
 * Apply one settings profile to several repeaters: pick them (the ones next to you are ticked), confirm,
 * then they go one by one (login, backup, read, write only what differs) with a report at the end.
 */
@Composable
fun ProfileBatchHost(vm: AppViewModel) {
    if (!vm.profileBatchOpen) return
    Dialog(onDismissRequest = { vm.closeProfileBatch() }, properties = DialogProperties(usePlatformDefaultWidth = false)) {
        Surface(Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
            Column(Modifier.statusBarsPadding().padding(horizontal = 16.dp)) {
                Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                    Text(stringResource(R.string.pbatch_title), style = MaterialTheme.typography.titleLarge, modifier = Modifier.weight(1f))
                    TextButton(onClick = { vm.closeProfileBatch() }, enabled = vm.profileBatchCurrent == null) {
                        Text(stringResource(R.string.build_close))
                    }
                }
                Column(
                    Modifier.verticalScroll(rememberScrollState()).padding(bottom = 24.dp),
                    verticalArrangement = Arrangement.spacedBy(10.dp),
                ) {
                    val b = vm.profileBatch
                    if (b == null) BatchPicker(vm) else BatchReport(vm, b)
                }
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun BatchPicker(vm: AppViewModel) {
    val profiles = vm.configProfiles
    if (profiles.isEmpty()) {
        InfoCard(stringResource(R.string.pbatch_no_profiles))
        return
    }
    Text(stringResource(R.string.pbatch_hint), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
    Text(stringResource(R.string.pbatch_profile), style = MaterialTheme.typography.titleSmall, color = MaterialTheme.colorScheme.primary)
    FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
        for (p in profiles) {
            FilterChip(
                selected = vm.profileBatchProfile?.name == p.name,
                onClick = { vm.profileBatchProfile = p },
                label = { Text("${p.name} (${p.settings.size})") },
            )
        }
    }
    Text(stringResource(R.string.q_pick_repeaters), style = MaterialTheme.typography.titleSmall, color = MaterialTheme.colorScheme.primary)
    val candidates = vm.profileBatchCandidates()
    val noPassword = vm.repeaters.filter { !vm.hasSavedPassword(it) }
    if (candidates.isEmpty()) {
        InfoCard(stringResource(R.string.q_no_candidates))
    } else {
        FlowRow(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            TextButton(onClick = { for (c in candidates) vm.profileBatchPicked[hex(c.publicKey)] = c.hops == 0 }, enabled = !vm.busy) {
                Text(stringResource(R.string.q_select_direct))
            }
            TextButton(onClick = { for (c in candidates) vm.profileBatchPicked[hex(c.publicKey)] = true }, enabled = !vm.busy) {
                Text(stringResource(R.string.q_select_all))
            }
            TextButton(onClick = { vm.profileBatchPicked.clear() }, enabled = !vm.busy) { Text(stringResource(R.string.q_select_none)) }
        }
    }
    for (c in candidates) {
        val key = hex(c.publicKey)
        val checked = vm.profileBatchPicked[key] == true
        Row(
            verticalAlignment = Alignment.CenterVertically,
            modifier = Modifier.fillMaxWidth().clickable(enabled = !vm.busy) { vm.profileBatchPicked[key] = !checked },
        ) {
            Checkbox(checked = checked, onCheckedChange = { vm.profileBatchPicked[key] = it }, enabled = !vm.busy)
            Column(Modifier.weight(1f)) {
                Text(c.name)
                Text(
                    when (val h = c.hops) {
                        null -> stringResource(R.string.route_unknown)
                        0 -> stringResource(R.string.route_direct)
                        else -> pluralStringResource(R.plurals.route_hops, h, h)
                    },
                    style = MaterialTheme.typography.bodySmall,
                    color = if (c.hops == 0) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }
    for (c in noPassword) {
        Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth()) {
            Checkbox(checked = false, onCheckedChange = null, enabled = false, modifier = Modifier.padding(12.dp))
            Column(Modifier.weight(1f)) {
                val grey = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f)
                Text(c.name, color = grey)
                Text(stringResource(R.string.q_login_once), style = MaterialTheme.typography.bodySmall, color = grey)
            }
        }
    }
    val picked = candidates.count { vm.profileBatchPicked[hex(it.publicKey)] == true }
    Button(
        onClick = { vm.profileBatchAsk = true },
        enabled = !vm.busy && picked > 0 && vm.profileBatchProfile != null,
        modifier = Modifier.fillMaxWidth(),
    ) {
        Text(pluralStringResource(R.plurals.pbatch_start, picked, picked))
    }
    if (vm.profileBatchAsk) {
        val p = vm.profileBatchProfile
        AlertDialog(
            onDismissRequest = { vm.profileBatchAsk = false },
            title = { Text(stringResource(R.string.pbatch_confirm_title, p?.name ?: "")) },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text(
                        stringResource(R.string.pbatch_confirm_count, picked, ProfilePlanner.estimateMinutes(picked)),
                        style = MaterialTheme.typography.bodyMedium,
                    )
                    Text(stringResource(R.string.pbatch_confirm_text), style = MaterialTheme.typography.bodySmall)
                }
            },
            confirmButton = { TextButton(onClick = { vm.startProfileBatch() }) { Text(stringResource(R.string.pbatch_go)) } },
            dismissButton = { TextButton(onClick = { vm.profileBatchAsk = false }) { Text(stringResource(R.string.cancel)) } },
        )
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun BatchReport(vm: AppViewModel, b: ProfileBatchState) {
    val running = vm.profileBatchCurrent != null
    val dark = MaterialTheme.colorScheme.background.luminance() < 0.5f
    val warnTint = if (dark) Color(0xFFFFB74D) else Color(0xFF9A5B00)
    Text(b.profileName, style = MaterialTheme.typography.titleMedium)
    if (running || vm.busy) {
        Text(stringResource(R.string.pbatch_progress, b.done, b.entries.size), style = MaterialTheme.typography.titleSmall)
        LinearProgressIndicator(progress = { b.done.toFloat() / b.entries.size.coerceAtLeast(1) }, modifier = Modifier.fillMaxWidth())
        if (vm.working.isNotEmpty()) {
            Text(vm.working, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        OutlinedButton(onClick = { vm.stop() }, enabled = vm.canStop) { Text(stringResource(R.string.stop)) }
    } else {
        Card(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text(stringResource(R.string.pbatch_summary_title), style = MaterialTheme.typography.titleMedium)
                Text(
                    stringResource(
                        R.string.pbatch_summary_counts,
                        b.count(BatchResult.APPLIED), b.appliedKeys, b.count(BatchResult.UNCHANGED),
                        b.count(BatchResult.FAILED) + b.count(BatchResult.SKIPPED), b.count(BatchResult.PENDING),
                    ),
                    style = MaterialTheme.typography.bodyMedium,
                )
            }
        }
    }
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            for (e in b.entries) {
                val current = vm.profileBatchCurrent === e
                val (icon, tint) = when {
                    current -> Icons.Default.PlayArrow to MaterialTheme.colorScheme.primary
                    e.result == BatchResult.APPLIED -> Icons.Default.CheckCircle to MaterialTheme.colorScheme.primary
                    e.result == BatchResult.UNCHANGED -> Icons.Default.CheckCircle to MaterialTheme.colorScheme.outline
                    e.result == BatchResult.SKIPPED -> Icons.Default.Warning to warnTint
                    e.result == BatchResult.FAILED -> Icons.Default.Close to MaterialTheme.colorScheme.error
                    else -> Icons.AutoMirrored.Filled.ArrowForward to MaterialTheme.colorScheme.outline
                }
                val status = when (e.result) {
                    BatchResult.APPLIED -> stringResource(R.string.pbatch_st_applied, e.keys)
                    BatchResult.UNCHANGED -> stringResource(R.string.pbatch_st_unchanged)
                    BatchResult.SKIPPED -> stringResource(R.string.pbatch_st_skipped)
                    BatchResult.FAILED -> stringResource(R.string.pbatch_st_failed)
                    BatchResult.PENDING -> if (current) stringResource(R.string.pbatch_st_running) else stringResource(R.string.pbatch_st_pending)
                }
                Row(verticalAlignment = Alignment.Top, modifier = Modifier.fillMaxWidth()) {
                    Icon(icon, null, tint = tint, modifier = Modifier.size(20.dp))
                    Spacer(Modifier.width(8.dp))
                    Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                        Text(e.name, style = MaterialTheme.typography.bodyLarge)
                        Text(status, style = MaterialTheme.typography.labelMedium, color = tint)
                        if (e.note.isNotEmpty()) {
                            Text(e.note, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                        }
                    }
                }
            }
        }
    }
    if (!running && !vm.busy) {
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            if (b.retryable > 0) {
                Button(onClick = { vm.retryProfileBatch() }) { Text(stringResource(R.string.pbatch_retry, b.retryable)) }
            }
            OutlinedButton(onClick = { vm.newProfileBatch() }) { Text(stringResource(R.string.pbatch_new)) }
        }
    }
}
