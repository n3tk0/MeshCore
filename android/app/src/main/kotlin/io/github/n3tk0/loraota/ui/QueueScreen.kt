package io.github.n3tk0.loraota.ui

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
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.automirrored.filled.ArrowForward
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.Checkbox
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import io.github.n3tk0.loraota.R
import io.github.n3tk0.loraota.ota.OtaQueueState
import io.github.n3tk0.loraota.ota.QueueEntry
import io.github.n3tk0.loraota.ota.QueueResult
import io.github.n3tk0.loraota.ota.hex

/**
 * Several repeaters updated over LoRa one after another, to one release: pick them (those with a kept
 * password, next to you first) and the release; while it runs, the one being updated; at the end, a report.
 */
@Composable
fun QueueScreen(vm: AppViewModel) {
    val q = vm.queue
    if (vm.busy && vm.queueCurrent != null) {
        if (q != null) {
            val i = q.entries.indexOf(vm.queueCurrent) + 1
            Text(stringResource(R.string.q_running, i, q.entries.size, q.label), style = MaterialTheme.typography.titleMedium)
        }
        RunningView(vm)
        if (q != null) QueueList(q, vm.queueCurrent)
        return
    }
    if (q != null) {
        QueueReport(vm, q)
        return
    }
    QueuePick(vm)
}

@Composable
private fun QueuePick(vm: AppViewModel) {
    Hint(stringResource(R.string.q_hint_short), stringResource(R.string.q_hint))
    Text(stringResource(R.string.q_pick_release), style = MaterialTheme.typography.titleSmall, color = MaterialTheme.colorScheme.primary)
    val rels = vm.queueReleases()
    if (rels.isEmpty()) Text(stringResource(R.string.q_no_releases), style = MaterialTheme.typography.bodyMedium)
    for ((rel, label) in rels.take(8)) {
        Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier
            .fillMaxWidth()
            .clickable(enabled = !vm.busy) { vm.queueRelease = rel }) {
            RadioButton(selected = vm.queueRelease == rel, onClick = { vm.queueRelease = rel }, enabled = !vm.busy)
            Text(label)
        }
    }
    Text(stringResource(R.string.q_pick_repeaters), style = MaterialTheme.typography.titleSmall, color = MaterialTheme.colorScheme.primary)
    val list = vm.queueCandidates()
    val noPassword = vm.queueNoPassword()
    if (list.isEmpty()) InfoCard(stringResource(R.string.q_no_candidates))
    for (c in list) {
        val key = hex(c.publicKey)
        val on = vm.queuePicked[key] == true
        Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier
            .fillMaxWidth()
            .clickable(enabled = !vm.busy) { vm.queuePicked[key] = !on }) {
            Checkbox(checked = on, onCheckedChange = { vm.queuePicked[key] = it }, enabled = !vm.busy)
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
                // only a direct route can take an update over LoRa; the route may still change by then
                if (c.hops != 0) Text(stringResource(R.string.q_skipped_unless_direct), style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant)
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
    val picked = list.filter { vm.queuePicked[hex(it.publicKey)] == true }
    val count = picked.size
    Button(onClick = { vm.queueAsk = true }, enabled = !vm.busy && count > 0 && vm.queueRelease != null) {
        Text(pluralStringResource(R.plurals.q_start, count, count))
    }
    if (vm.queueAsk) {
        val label = rels.firstOrNull { it.first == vm.queueRelease }?.second ?: vm.queueRelease ?: ""
        AlertDialog(
            onDismissRequest = { vm.queueAsk = false },
            title = { Text(pluralStringResource(R.plurals.q_start, count, count)) },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text(stringResource(R.string.q_confirm_release, label))
                    Text(picked.joinToString("\n") { "• " + it.name })
                    Text(stringResource(R.string.q_confirm_text))
                }
            },
            confirmButton = { TextButton(onClick = { vm.startQueue() }) { Text(stringResource(R.string.q_confirm_go)) } },
            dismissButton = { TextButton(onClick = { vm.queueAsk = false }) { Text(stringResource(R.string.cancel)) } },
        )
    }
}

@Composable
private fun QueueReport(vm: AppViewModel, q: OtaQueueState) {
    Text(q.label, style = MaterialTheme.typography.titleMedium)
    if (!q.finished) InfoCard(stringResource(R.string.q_cut_short))
    QueueList(q, null)
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        if (!q.finished) Button(onClick = { vm.runQueue() }, enabled = !vm.busy) { Text(stringResource(R.string.q_continue)) }
        if (q.finished && (q.count(QueueResult.FAILED) + q.count(QueueResult.SKIPPED)) > 0) {
            OutlinedButton(onClick = { vm.retryQueue() }, enabled = !vm.busy) { Text(stringResource(R.string.q_retry)) }
        }
        TextButton(onClick = { vm.clearQueue() }, enabled = !vm.busy) {
            Text(stringResource(if (q.finished) R.string.q_new else R.string.q_drop))
        }
    }
}

@Composable
private fun QueueList(q: OtaQueueState, current: QueueEntry?) {
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            for (e in q.entries) {
                Row(verticalAlignment = Alignment.Top) {
                    val (icon, tint) = when {
                        e === current -> Icons.Default.PlayArrow to MaterialTheme.colorScheme.primary
                        e.result == QueueResult.DONE || e.result == QueueResult.CURRENT -> Icons.Default.Check to MaterialTheme.colorScheme.primary
                        e.result == QueueResult.FAILED -> Icons.Default.Close to MaterialTheme.colorScheme.error
                        e.result == QueueResult.SKIPPED -> Icons.Default.Info to MaterialTheme.colorScheme.onSurfaceVariant
                        else -> Icons.AutoMirrored.Filled.ArrowForward to MaterialTheme.colorScheme.outline
                    }
                    Icon(icon, null, tint = tint, modifier = Modifier.size(20.dp))
                    Spacer(Modifier.width(8.dp))
                    Column(Modifier.weight(1f)) {
                        Text(e.name, style = MaterialTheme.typography.bodyLarge)
                        val what = stringResource(when (e.result) {
                            QueueResult.PENDING -> if (e === current) R.string.q_r_running else R.string.q_r_pending
                            QueueResult.DONE -> R.string.q_r_done
                            QueueResult.CURRENT -> R.string.q_r_current
                            QueueResult.SKIPPED -> R.string.q_r_skipped
                            QueueResult.FAILED -> R.string.q_r_failed
                        })
                        Text(if (e.note.isEmpty()) what else "$what: ${e.note}", style = MaterialTheme.typography.bodySmall,
                            color = if (e.result == QueueResult.FAILED) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                }
            }
        }
    }
}
