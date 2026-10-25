package io.github.n3tk0.loraota.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.material3.Card
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import io.github.n3tk0.loraota.R
import io.github.n3tk0.loraota.backup.DeviceKind
import io.github.n3tk0.loraota.ota.HealthWarning
import io.github.n3tk0.loraota.ota.HistorySeries
import io.github.n3tk0.loraota.ota.SelfTest
import java.text.DateFormat
import java.util.Date
import java.util.Locale

/** A device's health: the repeater's self test and its past ones, and graphs of the telemetry history. */
@Composable
fun HealthScreen(vm: AppViewModel) {
    val repeater = vm.healthKind == DeviceKind.REPEATER
    Text(stringResource(if (repeater) R.string.health_hint_repeater else R.string.health_hint_companion),
        style = MaterialTheme.typography.bodyMedium)
    OutlinedButton(onClick = { vm.refreshHealth() }, enabled = !vm.busy) { Text(stringResource(R.string.health_refresh)) }

    for (w in vm.healthWarnings) {
        val text = stringResource(warningText(w))
        if (w == HealthWarning.NEW_FIRMWARE) InfoCard(text) else DangerCard(text)
    }
    if (repeater) {
        if (vm.canReadRepeater == false) InfoCard(stringResource(R.string.health_no_capture))
        if (vm.selfTestMissing) InfoCard(stringResource(R.string.health_no_selftest))
        vm.selfTest?.let { SelfTestCard(it) }
    }
    HistoryCard(vm)
    if (repeater && vm.healthRecords.size > 1) PastChecks(vm)
}

private fun warningText(w: HealthWarning) = when (w) {
    HealthWarning.RADIO -> R.string.hw_radio
    HealthWarning.I2C_OFF -> R.string.hw_i2c_off
    HealthWarning.I2C_STUCK -> R.string.hw_i2c_stuck
    HealthWarning.QSPI -> R.string.hw_qspi
    HealthWarning.BATTERY_STEP -> R.string.hw_battery_step
    HealthWarning.NEW_FIRMWARE -> R.string.hw_new_firmware
    HealthWarning.I2C_NEW_OFF -> R.string.hw_i2c_new_off
    HealthWarning.FAST_DRAIN -> R.string.hw_fast_drain
}

@Composable
private fun SelfTestCard(t: SelfTest) {
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text(stringResource(if (t.ok) R.string.health_ok else R.string.health_warn),
                style = MaterialTheme.typography.titleMedium,
                color = if (t.ok) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.error)
            val v = t.values
            HealthRow(stringResource(R.string.health_radio), listOfNotNull(
                v["radio"], v["rx"]?.let { "rx $it" }, v["tx"]?.let { "tx $it" }, v["nf"]?.let { "$it dBm" }).joinToString(" · "))
            v["i2c"]?.let { HealthRow("I2C", it.replace(':', ' ')) }
            v["qspi"]?.let { HealthRow("QSPI", it) }
            val batt = buildString {
                t.batteryMv?.let { append(String.format(Locale.ROOT, "%.2f V", it / 1000.0)) }
                t.batteryPercent?.let { append(" · $it%") }
                when (val h = t.hoursLeft) {
                    null -> {}
                    -2 -> append(" · ").append(stringResource(R.string.health_charging))
                    else -> append(" · ").append(stringResource(R.string.health_left, h))
                }
            }
            if (batt.isNotEmpty()) HealthRow(stringResource(R.string.health_battery), batt)
            v["day"]?.let { HealthRow(stringResource(R.string.health_today), "$it mAh") }
            v["mail"]?.let { HealthRow(stringResource(R.string.health_mailbox), if (it == "off") stringResource(R.string.health_off) else it) }
            v["ota"]?.let { HealthRow("OTA", it.replace(",new", " · " + stringResource(R.string.health_unconfirmed))) }
            t.powerStep?.let { HealthRow(stringResource(R.string.health_power), powerStep(it)) }
            v["up"]?.let { HealthRow(stringResource(R.string.health_uptime), it) }
        }
    }
}

@Composable
private fun powerStep(s: String) = stringResource(when (s) {
    "low" -> R.string.ps_low
    "critical" -> R.string.ps_critical
    "off" -> R.string.ps_off
    else -> R.string.ps_normal
})

@Composable
private fun HealthRow(label: String, value: String) {
    Row(Modifier.fillMaxWidth()) {
        Text(label, modifier = Modifier.weight(0.4f), style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant)
        Text(value, modifier = Modifier.weight(0.6f), style = MaterialTheme.typography.bodyMedium)
    }
}

/** The fields a graph can show, with their unit. */
private val FIELDS = listOf(
    HistorySeries.F_BATT_PCT to R.string.hf_batt_pct,
    HistorySeries.F_BATT_MV to R.string.hf_batt_v,
    HistorySeries.F_CURRENT_MA to R.string.hf_current,
    HistorySeries.F_TEMP_DC to R.string.hf_temp,
    HistorySeries.F_HUMIDITY to R.string.hf_humidity,
    HistorySeries.F_PRESSURE to R.string.hf_pressure,
    HistorySeries.F_NOISE_DBM to R.string.hf_noise,
    HistorySeries.F_RX to R.string.hf_rx,
    HistorySeries.F_TX to R.string.hf_tx,
    HistorySeries.F_AIR_TX_S to R.string.hf_air,
)

private fun unit(field: Int) = when (field) {
    HistorySeries.F_BATT_PCT, HistorySeries.F_HUMIDITY -> "%"
    HistorySeries.F_BATT_MV -> "V"
    HistorySeries.F_CURRENT_MA -> "mA"
    HistorySeries.F_TEMP_DC -> "°C"
    HistorySeries.F_PRESSURE -> "hPa"
    HistorySeries.F_NOISE_DBM -> "dBm"
    HistorySeries.F_AIR_TX_S -> "s"
    else -> ""
}

private fun format(field: Int, v: Int): String {
    val d = HistorySeries.display(field, v)
    val text = when (field) {
        HistorySeries.F_BATT_MV -> String.format(Locale.ROOT, "%.2f", d)
        HistorySeries.F_TEMP_DC, HistorySeries.F_PRESSURE -> String.format(Locale.ROOT, "%.1f", d)
        else -> d.toInt().toString()
    }
    return "$text ${unit(field)}".trim()
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun HistoryCard(vm: AppViewModel) {
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(stringResource(R.string.health_history), style = MaterialTheme.typography.titleMedium)
            if (vm.historyMissing) {
                Text(stringResource(R.string.health_no_history), style = MaterialTheme.typography.bodyMedium)
                return@Column
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                FilterChip(selected = vm.historyRing == HistorySeries.RING_DAY, enabled = !vm.busy,
                    onClick = { vm.showHistory(HistorySeries.RING_DAY, vm.historyField) },
                    label = { Text(stringResource(R.string.health_24h)) })
                FilterChip(selected = vm.historyRing == HistorySeries.RING_WEEK, enabled = !vm.busy,
                    onClick = { vm.showHistory(HistorySeries.RING_WEEK, vm.historyField) },
                    label = { Text(stringResource(R.string.health_week)) })
            }
            Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                for ((f, name) in FIELDS) {
                    FilterChip(selected = vm.historyField == f, enabled = !vm.busy,
                        onClick = { vm.showHistory(vm.historyRing, f) }, label = { Text(stringResource(name)) })
                }
            }
            val s = vm.historyCache[vm.historyRing to vm.historyField]
            when {
                s == null -> Text(stringResource(if (vm.busy) R.string.health_loading else R.string.health_not_loaded),
                    style = MaterialTheme.typography.bodySmall)
                s.values.none { it != null } -> Text(stringResource(R.string.health_no_values), style = MaterialTheme.typography.bodyMedium)
                else -> Chart(s)
            }
        }
    }
}

@Composable
private fun Chart(s: HistorySeries) {
    val known = s.values.filterNotNull()
    val lo = known.min()
    val hi = known.max()
    val line = MaterialTheme.colorScheme.primary
    val grid = MaterialTheme.colorScheme.outlineVariant
    Row {
        Text(stringResource(R.string.health_range, format(s.field, lo), format(s.field, hi)),
            style = MaterialTheme.typography.bodySmall, modifier = Modifier.weight(1f))
        Text(stringResource(R.string.health_last, format(s.field, s.values.lastOrNull { it != null } ?: lo)),
            style = MaterialTheme.typography.bodySmall)
    }
    Canvas(Modifier.fillMaxWidth().height(160.dp)) {
        val n = s.values.size
        val span = (hi - lo).coerceAtLeast(1).toFloat()
        val pad = 4.dp.toPx()
        fun x(i: Int) = if (n <= 1) size.width / 2 else i * size.width / (n - 1)
        fun y(v: Int) = pad + (size.height - 2 * pad) * (1 - (v - lo) / span)
        for (k in 0..2) {
            val gy = pad + (size.height - 2 * pad) * k / 2
            drawLine(grid, Offset(0f, gy), Offset(size.width, gy), strokeWidth = 1f)
        }
        // a gap (the node was off, or the value was not measured) breaks the line
        val path = Path()
        var open = false
        for ((i, v) in s.values.withIndex()) {
            if (v == null) { open = false; continue }
            if (open) path.lineTo(x(i), y(v)) else path.moveTo(x(i), y(v))
            open = true
        }
        drawPath(path, line, style = Stroke(width = 2.dp.toPx()))
        // a single point between gaps would not show as a line
        for ((i, v) in s.values.withIndex()) {
            if (v != null && s.values.getOrNull(i - 1) == null && s.values.getOrNull(i + 1) == null) {
                drawCircle(line, 2.5.dp.toPx(), Offset(x(i), y(v)))
            }
        }
    }
    val first = s.timeOf(0)
    val last = s.timeOf(s.values.size - 1)
    Row {
        val fmt = DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT)
        Text(if (first != null) fmt.format(Date(first * 1000)) else stringResource(
            if (s.ring == HistorySeries.RING_DAY) R.string.health_ago_day else R.string.health_ago_week),
            style = MaterialTheme.typography.labelSmall, modifier = Modifier.weight(1f))
        Text(if (last != null) fmt.format(Date(last * 1000)) else stringResource(R.string.health_now),
            style = MaterialTheme.typography.labelSmall)
    }
}

/** The self tests kept on this phone, newest first: what changed between visits. */
@Composable
private fun PastChecks(vm: AppViewModel) {
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text(stringResource(R.string.health_past), style = MaterialTheme.typography.titleMedium)
            val fmt = DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT)
            for (r in vm.healthRecords.asReversed().take(10)) {
                val t = SelfTest.parse(r.line) ?: continue
                val what = buildString {
                    append(if (t.ok) "ok" else t.problems.joinToString(", ").ifEmpty { "WARN" })
                    t.batteryMv?.let { append(String.format(Locale.ROOT, " · %.2f V", it / 1000.0)) }
                    t.batteryPercent?.let { append(" $it%") }
                    t.mailbox?.let { append(" · ✉ $it") }
                }
                HealthRow(fmt.format(Date(r.time)), what)
            }
        }
    }
}
