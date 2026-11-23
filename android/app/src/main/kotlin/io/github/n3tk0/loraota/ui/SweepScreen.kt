package io.github.n3tk0.loraota.ui

import android.content.Intent
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
import androidx.compose.material.icons.automirrored.filled.ArrowForward
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Share
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.Checkbox
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
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import io.github.n3tk0.loraota.R
import io.github.n3tk0.loraota.ota.hex
import io.github.n3tk0.loraota.tools.SweepReportFormatter
import io.github.n3tk0.loraota.tools.SweepReportRow
import java.text.DateFormat
import java.util.Date

/**
 * Batch fleet health check: sequentially logs in to selected repeaters with saved passwords,
 * runs `selftest`, stores results in HealthStore, and shows an actionable color-coded summary report
 * with retry-failed, share report, and tap-to-open repeater health.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun SweepScreen(vm: AppViewModel) {
    if (vm.canReadRepeater == false) {
        InfoCard(stringResource(R.string.health_no_capture))
        return
    }

    if (vm.sweepRunning || vm.sweepFinished || vm.sweepEntries.isNotEmpty()) {
        SweepReport(vm)
        return
    }

    Hint(stringResource(R.string.sweep_hint_short), stringResource(R.string.sweep_hint))

    val candidates = vm.repeaters.filter { vm.hasSavedPassword(it) }
    val noPassword = vm.repeaters.filter { !vm.hasSavedPassword(it) }

    Text(
        stringResource(R.string.q_pick_repeaters),
        style = MaterialTheme.typography.titleSmall,
        color = MaterialTheme.colorScheme.primary,
    )

    if (candidates.isNotEmpty()) {
        FlowRow(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            TextButton(onClick = {
                for (c in candidates) vm.sweepPicked[hex(c.publicKey)] = (c.hops == 0)
            }, enabled = !vm.busy) { Text(stringResource(R.string.q_select_direct)) }
            TextButton(onClick = {
                for (c in candidates) vm.sweepPicked[hex(c.publicKey)] = true
            }, enabled = !vm.busy) { Text(stringResource(R.string.q_select_all)) }
            TextButton(onClick = {
                vm.sweepPicked.clear()
            }, enabled = !vm.busy) { Text(stringResource(R.string.q_select_none)) }
        }
    } else {
        InfoCard(stringResource(R.string.q_no_candidates))
    }

    for (c in candidates) {
        val key = hex(c.publicKey)
        val checked = vm.sweepPicked[key] == true
        Row(
            verticalAlignment = Alignment.CenterVertically,
            modifier = Modifier
                .fillMaxWidth()
                .clickable(enabled = !vm.busy) { vm.sweepPicked[key] = !checked },
        ) {
            Checkbox(checked = checked, onCheckedChange = { vm.sweepPicked[key] = it }, enabled = !vm.busy)
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

    val pickedCount = candidates.count { vm.sweepPicked[hex(it.publicKey)] == true }
    Button(
        onClick = { vm.startSweep() },
        enabled = !vm.busy && pickedCount > 0,
        modifier = Modifier.fillMaxWidth(),
    ) {
        Text(pluralStringResource(R.plurals.sweep_start, pickedCount, pickedCount))
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun SweepReport(vm: AppViewModel) {
    val ctx = LocalContext.current
    val entries = vm.sweepEntries
    val done = entries.count { it.status != SweepStatus.PENDING && it.status != SweepStatus.RUNNING }
    val failedCount = entries.count { it.status == SweepStatus.FAILED || it.status == SweepStatus.PENDING }
    val dark = MaterialTheme.colorScheme.background.luminance() < 0.5f
    val warnTint = if (dark) Color(0xFFFFB74D) else Color(0xFF9A5B00)

    if (vm.sweepRunning && entries.isNotEmpty()) {
        Text(
            stringResource(R.string.sweep_progress, done, entries.size),
            style = MaterialTheme.typography.titleMedium,
        )
        LinearProgressIndicator(
            progress = { done.toFloat() / entries.size.toFloat() },
            modifier = Modifier.fillMaxWidth(),
        )
    } else if (vm.sweepFinished) {
        val okCount = entries.count { it.status == SweepStatus.OK }
        val warnCount = entries.count { it.status == SweepStatus.WARN }
        val badCount = entries.count { it.status == SweepStatus.DANGER || it.status == SweepStatus.FAILED }
        Card(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text(stringResource(R.string.sweep_summary_title), style = MaterialTheme.typography.titleMedium)
                Text(
                    stringResource(R.string.sweep_summary_counts, okCount, warnCount, badCount),
                    style = MaterialTheme.typography.bodyMedium,
                )
                Text(
                    stringResource(R.string.sweep_tap_open_hint),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }

    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            for (e in entries) {
                val (icon, tint) = when (e.status) {
                    SweepStatus.RUNNING -> Icons.Default.PlayArrow to MaterialTheme.colorScheme.primary
                    SweepStatus.OK -> Icons.Default.CheckCircle to MaterialTheme.colorScheme.primary
                    SweepStatus.WARN -> Icons.Default.Warning to warnTint
                    SweepStatus.DANGER -> Icons.Default.Warning to MaterialTheme.colorScheme.error
                    SweepStatus.FAILED -> Icons.Default.Close to MaterialTheme.colorScheme.error
                    SweepStatus.PENDING -> Icons.AutoMirrored.Filled.ArrowForward to MaterialTheme.colorScheme.outline
                }
                val canOpen = !vm.sweepRunning && !vm.busy
                Row(
                    verticalAlignment = Alignment.Top,
                    modifier = Modifier
                        .fillMaxWidth()
                        .clickable(enabled = canOpen) { vm.openRepeaterFromSweep(e.key) }
                        .padding(vertical = 2.dp),
                ) {
                    Icon(icon, null, tint = tint, modifier = Modifier.size(20.dp))
                    Spacer(Modifier.width(8.dp))
                    Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                        Text(e.name, style = MaterialTheme.typography.bodyLarge)
                        if (e.detail.isNotEmpty()) {
                            Text(e.detail, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                        }
                    }
                    if (e.batteryText.isNotEmpty()) {
                        Surface(
                            color = MaterialTheme.colorScheme.secondaryContainer,
                            shape = MaterialTheme.shapes.small,
                        ) {
                            Text(
                                e.batteryText,
                                modifier = Modifier.padding(horizontal = 8.dp, vertical = 2.dp),
                                style = MaterialTheme.typography.labelSmall,
                                color = MaterialTheme.colorScheme.onSecondaryContainer,
                            )
                        }
                    }
                }
            }
        }
    }

    if (!vm.sweepRunning) {
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            if (failedCount > 0) {
                Button(onClick = { vm.retryFailedSweep() }, enabled = !vm.busy) {
                    Text(stringResource(R.string.sweep_retry_failed, failedCount))
                }
            }
            if (entries.isNotEmpty()) {
                OutlinedButton(
                    onClick = {
                        val stamp = DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT).format(Date())
                        val report = SweepReportFormatter.format(
                            entries.map { SweepReportRow(it.name, it.status.name, it.batteryText, it.detail) },
                            stamp,
                        )
                        val intent = Intent(Intent.ACTION_SEND).apply {
                            type = "text/plain"
                            putExtra(Intent.EXTRA_TEXT, report)
                        }
                        runCatching { ctx.startActivity(Intent.createChooser(intent, ctx.getString(R.string.sweep_share))) }
                    },
                ) {
                    Icon(Icons.Default.Share, null, modifier = Modifier.size(16.dp))
                    Spacer(Modifier.width(4.dp))
                    Text(stringResource(R.string.sweep_share))
                }
            }
            OutlinedButton(onClick = { vm.resetSweep() }, enabled = !vm.busy) {
                Text(stringResource(R.string.sweep_new))
            }
            TextButton(onClick = { vm.closeSweep() }, enabled = !vm.busy) {
                Text(stringResource(R.string.back))
            }
        }
    }
}
