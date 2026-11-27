package io.github.n3tk0.loraota.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Star
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.luminance
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import io.github.n3tk0.loraota.R
import io.github.n3tk0.loraota.tools.KnownHealth
import io.github.n3tk0.loraota.tools.KnownRepeaterRow
import java.util.Locale

private const val COLLAPSED_ROWS = 3

@Composable
private fun checkedText(ms: Long?): String {
    if (ms == null) return stringResource(R.string.known_never_checked)
    val age = (System.currentTimeMillis() - ms).coerceAtLeast(0L)
    val mins = (age / 60_000L).toInt()
    val hours = (age / 3_600_000L).toInt()
    val days = (age / 86_400_000L).toInt()
    val ago = when {
        mins < 1 -> stringResource(R.string.ago_just_now)
        mins < 60 -> stringResource(R.string.ago_mins, mins)
        hours < 24 -> stringResource(R.string.ago_hours, hours)
        else -> stringResource(R.string.ago_days, days)
    }
    return stringResource(R.string.known_checked, ago)
}

/**
 * "My repeaters" on the start screen (no companion connected): what the phone knows about each repeater
 * from earlier health checks and backups. A row opens its offline details; "Connect and open" connects
 * the last companion and logs in to that repeater once its contacts are loaded.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun KnownRepeatersSection(vm: AppViewModel) {
    val rows = vm.knownRepeaters
    var expanded by rememberSaveable { mutableStateOf(false) }
    val dark = MaterialTheme.colorScheme.background.luminance() < 0.5f
    val warnTint = if (dark) Color(0xFFFFB74D) else Color(0xFF9A5B00)

    Text(stringResource(R.string.known_title), style = MaterialTheme.typography.titleSmall, color = MaterialTheme.colorScheme.primary)
    val pending = vm.pendingOpenKey
    if (pending != null) {
        Card(Modifier.fillMaxWidth(), colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.secondaryContainer)) {
            Row(Modifier.padding(start = 12.dp, end = 4.dp, top = 4.dp, bottom = 4.dp), verticalAlignment = Alignment.CenterVertically) {
                Text(
                    stringResource(R.string.known_pending, vm.pendingOpenName),
                    style = MaterialTheme.typography.bodySmall,
                    modifier = Modifier.weight(1f),
                )
                TextButton(onClick = { vm.cancelPendingOpen() }) { Text(stringResource(R.string.cancel)) }
            }
        }
    }
    if (rows.isEmpty()) {
        Text(
            stringResource(R.string.known_empty),
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        return
    }
    val shown = if (expanded) rows else rows.take(COLLAPSED_ROWS)
    for (row in shown) {
        KnownRepeaterCard(vm, row, warnTint)
    }
    if (rows.size > COLLAPSED_ROWS) {
        TextButton(onClick = { expanded = !expanded }) {
            Text(if (expanded) stringResource(R.string.known_show_less) else stringResource(R.string.known_show_all, rows.size))
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun KnownRepeaterCard(vm: AppViewModel, row: KnownRepeaterRow, warnTint: Color) {
    val (dot, statusRes) = when (row.health) {
        KnownHealth.OK -> MaterialTheme.colorScheme.primary to R.string.known_health_ok
        KnownHealth.WARN -> warnTint to R.string.known_health_warn
        KnownHealth.DANGER -> MaterialTheme.colorScheme.error to R.string.known_health_bad
        KnownHealth.UNKNOWN -> MaterialTheme.colorScheme.outline to R.string.known_health_unknown
    }
    val battery = row.batteryV?.let {
        String.format(Locale.ROOT, "%.2f V", it) + (row.batteryPercent?.let { p -> " · $p%" } ?: "")
    }
    Card(Modifier.fillMaxWidth().clickable(enabled = !vm.busy) { vm.openKnownOffline(row) }) {
        Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(row.name, style = MaterialTheme.typography.titleMedium, modifier = Modifier.weight(1f))
                if (row.pinned) {
                    Icon(Icons.Default.Star, null, modifier = Modifier.size(18.dp), tint = MaterialTheme.colorScheme.primary)
                }
            }
            FlowRow(horizontalArrangement = Arrangement.spacedBy(10.dp), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Box(Modifier.size(8.dp).clip(CircleShape).background(dot))
                    Spacer(Modifier.width(4.dp))
                    Text(stringResource(statusRes), style = MaterialTheme.typography.labelMedium)
                }
                if (battery != null) Text(battery, style = MaterialTheme.typography.labelMedium)
                vm.mailboxSummaries[row.key]?.let { MailboxBadge(it) }
                if (row.firmware.isNotEmpty()) {
                    Text(row.firmware.replace("|", " · "), style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
            }
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(
                    checkedText(row.lastCheckedMs),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.weight(1f),
                )
                Button(onClick = { vm.connectAndOpen(row) }, enabled = !vm.busy) {
                    Text(stringResource(R.string.known_connect_open))
                }
            }
        }
    }
}
