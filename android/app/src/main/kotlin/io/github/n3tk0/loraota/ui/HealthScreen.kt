package io.github.n3tk0.loraota.ui

import androidx.compose.foundation.Canvas
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
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.KeyboardArrowDown
import androidx.compose.material.icons.filled.KeyboardArrowUp
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.Checkbox
import androidx.compose.material3.ExperimentalMaterial3Api
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
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.luminance
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import io.github.n3tk0.loraota.R
import io.github.n3tk0.loraota.backup.DeviceKind
import io.github.n3tk0.loraota.ota.BattSave
import io.github.n3tk0.loraota.ota.HealthCheck
import io.github.n3tk0.loraota.ota.HealthProblem
import io.github.n3tk0.loraota.ota.HealthSeverity
import io.github.n3tk0.loraota.ota.HealthWarning
import io.github.n3tk0.loraota.ota.HistorySeries
import io.github.n3tk0.loraota.ota.SelfTest
import java.text.DateFormat
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * A device's health:
 * 1) Summary (all good or problems + self test in words)
 * 2) Expandable History section with lazy per-metric chart cards (fetched from firmware only when expanded)
 * 3) Expandable Past Checks kept on this phone
 * 4) Collapsible Battery & power-saving settings (`battsave` / capacity) at the bottom
 */
@Composable
fun HealthScreen(vm: AppViewModel) {
    val repeater = vm.healthKind == DeviceKind.REPEATER
    // an older firmware has neither: one card that says so, and the way to the update
    if (repeater && vm.selfTestMissing && vm.historyMissing) {
        OldFirmwareCard(vm)
        if (vm.healthRecords.size > 1) PastChecks(vm)
        return
    }
    val problems = HealthCheck.problems(vm.healthWarnings)
    if (repeater) {
        if (vm.canReadRepeater == false) InfoCard(stringResource(R.string.health_no_capture))
        if (vm.selfTestMissing) InfoCard(stringResource(R.string.health_no_selftest))
    }
    if (vm.selfTest != null || problems.isNotEmpty()) SummaryCard(vm.selfTest, problems)
    HistorySection(vm, defaultExpanded = !repeater || vm.canReadRepeater == false)
    if (repeater && vm.healthRecords.size > 1) PastChecks(vm)
    if (repeater) {
        val battIssue = HealthWarning.BATTERY_STEP in vm.healthWarnings || HealthWarning.FAST_DRAIN in vm.healthWarnings
        vm.battSave?.let { BatteryCard(vm, it, defaultExpanded = battIssue) }
    }
    Text(
        stringResource(if (repeater) R.string.health_hint_repeater else R.string.health_hint_companion),
        style = MaterialTheme.typography.bodySmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
}

private fun volts(mv: Int) = String.format(Locale.ROOT, "%.2f V", mv / 1000.0)

/**
 * Collapsible battery & power-saving settings card at the bottom of HealthScreen.
 */
@Composable
private fun BatteryCard(vm: AppViewModel, b: BattSave, defaultExpanded: Boolean) {
    var expanded by rememberSaveable(vm.healthDeviceName, defaultExpanded) { mutableStateOf(defaultExpanded) }
    val rec = BattSave.recommended(vm.battChecked)
    @Composable fun steps(low: Int, crit: Int, off: Int) = stringResource(
        R.string.batt_steps,
        volts(low),
        volts(crit),
        if (off == 0) stringResource(R.string.batt_off_unused) else volts(off),
    )
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
                    Text(stringResource(R.string.batt_title), style = MaterialTheme.typography.titleMedium)
                    Text(
                        if (b.enabled) stringResource(R.string.batt_on_step, powerStep(b.step))
                        else stringResource(R.string.hv_off),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
                Icon(
                    if (expanded) Icons.Default.KeyboardArrowUp else Icons.Default.KeyboardArrowDown,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            if (expanded) {
                HorizontalDivider()
                Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                    HealthRow(
                        stringResource(R.string.batt_now),
                        if (b.enabled) stringResource(R.string.batt_on_step, powerStep(b.step)) + "\n" + steps(b.low, b.crit, b.off)
                        else stringResource(R.string.hv_off),
                    )
                    HealthRow(stringResource(R.string.batt_suggested), steps(rec.first, rec.second, rec.third))
                    if (b.external) {
                        Text(
                            stringResource(R.string.batt_external),
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        modifier = Modifier
                            .fillMaxWidth()
                            .clickable(enabled = !vm.busy && !vm.battGauge) { vm.battChecked = !vm.battChecked },
                    ) {
                        Checkbox(checked = vm.battChecked, onCheckedChange = { vm.battChecked = it }, enabled = !vm.busy && !vm.battGauge)
                        Text(
                            stringResource(if (vm.battGauge) R.string.batt_checked_ina else R.string.batt_checked, volts(b.mv)),
                            style = MaterialTheme.typography.bodyMedium,
                        )
                    }
                    if (!vm.battChecked) {
                        Text(
                            stringResource(R.string.batt_why_no_off),
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        Button(onClick = { vm.applyBattSave() }, enabled = !vm.busy && !b.matches(rec)) {
                            Text(stringResource(R.string.batt_apply))
                        }
                        TextButton(onClick = { vm.setBattSave(!b.enabled) }, enabled = !vm.busy) {
                            Text(stringResource(if (b.enabled) R.string.batt_turn_off else R.string.batt_turn_on))
                        }
                    }
                    if (vm.battGauge) {
                        HorizontalDivider(Modifier.padding(vertical = 4.dp))
                        val c = vm.battCapacity
                        HealthRow(
                            stringResource(R.string.batt_capacity),
                            when {
                                c == null || c.mah == 0 -> stringResource(R.string.batt_cap_unknown)
                                c.learnt -> stringResource(R.string.batt_cap_learnt, c.mah)
                                else -> stringResource(R.string.batt_cap_set, c.mah)
                            },
                        )
                        var text by rememberSaveable(c?.mah) { mutableStateOf(if (c == null || c.mah == 0) "" else c.mah.toString()) }
                        Text(
                            stringResource(R.string.batt_cap_hint),
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            OutlinedTextField(
                                text, { text = it.filter(Char::isDigit).take(5) }, Modifier.weight(1f), singleLine = true,
                                label = { Text("mAh") }, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                            )
                            val mah = text.toIntOrNull()
                            OutlinedButton(
                                onClick = { vm.setBattCapacity(mah ?: 0) },
                                enabled = !vm.busy && (mah == null || mah <= 60000) && (mah ?: 0) != (c?.mah ?: 0),
                            ) {
                                Text(stringResource(if (mah == null || mah == 0) R.string.batt_cap_learn else R.string.batt_cap_save))
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun OldFirmwareCard(vm: AppViewModel) {
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(stringResource(R.string.health_old_firmware), style = MaterialTheme.typography.titleMedium)
            Text(stringResource(R.string.health_old_firmware_more), style = MaterialTheme.typography.bodyMedium)
            Button(onClick = { vm.closeHealth() }, enabled = !vm.busy) { Text(stringResource(R.string.health_to_update)) }
        }
    }
}

private fun warningText(p: HealthProblem) = when (p.warning) {
    HealthWarning.RADIO -> R.string.hw_radio
    HealthWarning.I2C_OFF -> if (p.isNew) R.string.hw_i2c_off_new else R.string.hw_i2c_off
    HealthWarning.I2C_STUCK -> R.string.hw_i2c_stuck
    HealthWarning.QSPI -> R.string.hw_qspi
    HealthWarning.BATTERY_STEP -> R.string.hw_battery_step
    HealthWarning.NEW_FIRMWARE -> R.string.hw_new_firmware
    HealthWarning.FRESH_FIRMWARE -> R.string.hw_fresh_firmware
    HealthWarning.I2C_NEW_OFF -> R.string.hw_i2c_off_new
    HealthWarning.FAST_DRAIN -> R.string.hw_fast_drain
}

/** What the summary's title names: "Има проблем: I2C, батерия". */
private fun shortName(w: HealthWarning) = when (w) {
    HealthWarning.RADIO -> R.string.hs_radio
    HealthWarning.I2C_OFF, HealthWarning.I2C_STUCK, HealthWarning.I2C_NEW_OFF -> R.string.hs_i2c
    HealthWarning.QSPI -> R.string.hs_qspi
    HealthWarning.BATTERY_STEP, HealthWarning.FAST_DRAIN -> R.string.hs_battery
    HealthWarning.NEW_FIRMWARE, HealthWarning.FRESH_FIRMWARE -> R.string.hs_firmware
}

/** Amber for "degraded, it tries again": not in the Material scheme. */
@Composable
private fun warnColor() = if (MaterialTheme.colorScheme.background.luminance() < 0.5f) Color(0xFFFFB74D) else Color(0xFF9A5B00)

@Composable
private fun SummaryCard(t: SelfTest?, problems: List<HealthProblem>) {
    val danger = problems.filter { it.severity == HealthSeverity.DANGER }
    val warn = problems.filter { it.severity == HealthSeverity.WARN }
    val worst = if (danger.isNotEmpty()) danger else warn
    val names = worst.map { stringResource(shortName(it.warning)) }.distinct().joinToString(", ")
    val (title, color) = when {
        danger.isNotEmpty() -> stringResource(R.string.health_problem, names) to MaterialTheme.colorScheme.error
        warn.isNotEmpty() -> stringResource(R.string.health_attention, names) to warnColor()
        else -> stringResource(R.string.health_ok) to MaterialTheme.colorScheme.primary
    }
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Icon(
                    if (worst.isEmpty()) Icons.Default.CheckCircle else Icons.Default.Warning, null, tint = color,
                    modifier = Modifier.size(24.dp),
                )
                Spacer(Modifier.width(8.dp))
                Text(title, style = MaterialTheme.typography.titleMedium, color = color)
            }
            for (p in problems) {
                val tint = when (p.severity) {
                    HealthSeverity.DANGER -> MaterialTheme.colorScheme.error
                    HealthSeverity.WARN -> warnColor()
                    HealthSeverity.INFO -> MaterialTheme.colorScheme.onSurfaceVariant
                }
                Row(verticalAlignment = Alignment.Top) {
                    Icon(
                        if (p.severity == HealthSeverity.INFO) Icons.Default.Info else Icons.Default.Warning, null,
                        tint = tint, modifier = Modifier.size(18.dp),
                    )
                    Spacer(Modifier.width(8.dp))
                    Text(stringResource(warningText(p)), style = MaterialTheme.typography.bodyMedium)
                }
            }
            if (t != null) {
                HorizontalDivider(Modifier.padding(vertical = 4.dp))
                SelfTestRows(t)
            }
        }
    }
}

/** The self test in words, e.g. "works · received 1240 · sent 310 · noise −112 dBm". */
@Composable
private fun SelfTestRows(t: SelfTest) {
    val v = t.values
    val radio = buildList {
        v["radio"]?.let { add(stringResource(if (it == "bad") R.string.hv_not_working else R.string.hv_working)) }
        v["rx"]?.let { add(stringResource(R.string.hv_rx, it)) }
        v["tx"]?.let { add(stringResource(R.string.hv_tx, it)) }
        v["nf"]?.toIntOrNull()?.let { add(stringResource(R.string.hv_noise, it.toString().replace('-', '−'))) }
    }
    if (radio.isNotEmpty()) HealthRow(stringResource(R.string.health_radio), radio.joinToString(" · "))
    v["i2c"]?.let { i2c ->
        HealthRow(
            stringResource(R.string.health_sensors),
            when {
                i2c == "stuck" -> stringResource(R.string.hv_bus_stuck)
                i2c.startsWith("off") -> (i2c.substringAfter(':', "1").toIntOrNull() ?: 1).let {
                    pluralStringResource(R.plurals.hv_sensors_off, it, it)
                }
                else -> stringResource(R.string.hv_fine)
            },
        )
    }
    v["qspi"]?.let { HealthRow(stringResource(R.string.health_flash), stringResource(if (it == "ok") R.string.hv_fine else R.string.hv_no_answer)) }
    val batt = buildList {
        t.batteryMv?.let { add(String.format(Locale.ROOT, "%.2f V", it / 1000.0)) }
        t.batteryPercent?.let { add("$it%") }
        when (val h = t.hoursLeft) {
            null -> {}
            -2 -> add(stringResource(R.string.health_charging))
            else -> add(stringResource(R.string.health_left, h))
        }
    }
    if (batt.isNotEmpty()) HealthRow(stringResource(R.string.health_battery), batt.joinToString(" · "))
    v["day"]?.let { d ->
        val inMah = d.substringBefore('/').removePrefix("+")
        val outMah = d.substringAfter('/', "").removePrefix("-")
        HealthRow(stringResource(R.string.health_today), stringResource(R.string.hv_day, inMah, outMah))
    }
    v["mail"]?.let { HealthRow(stringResource(R.string.health_mailbox), if (it == "off") stringResource(R.string.health_off) else it) }
    v["ota"]?.let { o ->
        val on = stringResource(if (o.startsWith("on")) R.string.hv_on else R.string.hv_off)
        HealthRow(
            "LoRa OTA",
            when {
                o.endsWith(",new") -> on + " · " + stringResource(R.string.health_unconfirmed)
                o.endsWith(",fresh") -> on + " · " + stringResource(R.string.health_fresh)
                else -> on
            },
        )
    }
    t.powerStep?.let { HealthRow(stringResource(R.string.health_power), powerStep(it)) }
    v["up"]?.let { up ->
        val m = Regex("(\\d+)d(\\d+)h").matchEntire(up)
        HealthRow(
            stringResource(R.string.health_uptime),
            if (m != null) stringResource(R.string.hv_uptime, m.groupValues[1].toInt(), m.groupValues[2].toInt()) else up,
        )
    }
}

@Composable
private fun powerStep(s: String) = stringResource(
    when (s) {
        "low" -> R.string.ps_low
        "critical" -> R.string.ps_critical
        "off" -> R.string.ps_off
        else -> R.string.ps_normal
    }
)

@Composable
private fun HealthRow(label: String, value: String) {
    Row(Modifier.fillMaxWidth()) {
        Text(
            label, modifier = Modifier.weight(0.35f), style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        Text(value, modifier = Modifier.weight(0.65f), style = MaterialTheme.typography.bodyMedium)
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

/**
 * Expandable "History" section with individual collapsible chart cards.
 * Each chart stays closed by default and fetches its telemetry series from firmware only when opened.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun HistorySection(vm: AppViewModel, defaultExpanded: Boolean) {
    var historyOpen by rememberSaveable(vm.healthDeviceName, defaultExpanded) { mutableStateOf(defaultExpanded) }
    val expandedFields = remember(vm.healthDeviceName, vm.healthGeneration) { mutableStateMapOf<Int, Boolean>() }

    Card(Modifier.fillMaxWidth()) {
        Row(
            verticalAlignment = Alignment.CenterVertically,
            modifier = Modifier
                .fillMaxWidth()
                .clickable { historyOpen = !historyOpen }
                .padding(12.dp),
        ) {
            Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                Text(stringResource(R.string.health_history), style = MaterialTheme.typography.titleMedium)
                Text(
                    if (vm.historyMissing) stringResource(R.string.health_no_history)
                    else stringResource(R.string.health_history_subtitle),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            Icon(
                if (historyOpen) Icons.Default.KeyboardArrowUp else Icons.Default.KeyboardArrowDown,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }

    if (!historyOpen || vm.historyMissing) return

    val rings = listOf(
        HistorySeries.RING_DAY to R.string.health_24h,
        HistorySeries.RING_WEEK to R.string.health_week,
    )
    SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth()) {
        for ((i, pair) in rings.withIndex()) {
            val (ring, labelRes) = pair
            SegmentedButton(
                selected = vm.historyRing == ring,
                onClick = {
                    val open = expandedFields.filterValues { it }.keys
                    vm.setHistoryRing(ring, open)
                },
                enabled = !vm.busy,
                shape = SegmentedButtonDefaults.itemShape(i, rings.size),
            ) {
                Text(stringResource(labelRes), maxLines = 1)
            }
        }
    }

    // Filter to metrics supported by the device once any loaded series tells us its field bitmask
    val masks = vm.historyCache.values.mapNotNull { it.fields }
    val hasMask = masks.fold(0) { a, m -> a or m }.takeIf { it != 0 }

    for ((field, nameRes) in FIELDS) {
        val isExpanded = expandedFields[field] == true
        if (hasMask != null && hasMask and (1 shl field) == 0 && !isExpanded) continue
        val key = vm.historyRing to field
        val series = vm.historyCache[key]
        val isLoading = vm.loadingHistoryKey == key || key in vm.queuedHistoryKeys
        val lastVal = series?.values?.lastOrNull { it != null }

        Card(Modifier.fillMaxWidth()) {
            Column(Modifier.fillMaxWidth()) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier
                        .fillMaxWidth()
                        .clickable {
                            val next = !isExpanded
                            expandedFields[field] = next
                            if (next && series == null) {
                                vm.loadHistoryField(vm.historyRing, field)
                            }
                        }
                        .padding(horizontal = 12.dp, vertical = 10.dp),
                ) {
                    Text(
                        stringResource(nameRes),
                        style = MaterialTheme.typography.titleSmall,
                        modifier = Modifier.weight(1f),
                    )
                    if (lastVal != null) {
                        Surface(
                            color = MaterialTheme.colorScheme.secondaryContainer,
                            shape = MaterialTheme.shapes.small,
                        ) {
                            Text(
                                format(field, lastVal),
                                modifier = Modifier.padding(horizontal = 8.dp, vertical = 2.dp),
                                style = MaterialTheme.typography.labelSmall,
                                color = MaterialTheme.colorScheme.onSecondaryContainer,
                            )
                        }
                        Spacer(Modifier.width(6.dp))
                    }
                    Icon(
                        if (isExpanded) Icons.Default.KeyboardArrowUp else Icons.Default.KeyboardArrowDown,
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
                if (isExpanded) {
                    HorizontalDivider()
                    Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                        when {
                            isLoading -> {
                                LinearProgressIndicator(Modifier.fillMaxWidth())
                                Text(
                                    stringResource(R.string.health_loading),
                                    style = MaterialTheme.typography.bodySmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                                )
                            }
                            series == null -> {
                                Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth()) {
                                    Text(
                                        stringResource(if (vm.busy) R.string.health_loading else R.string.health_not_loaded),
                                        style = MaterialTheme.typography.bodySmall,
                                        modifier = Modifier.weight(1f),
                                    )
                                    TextButton(
                                        onClick = { vm.loadHistoryField(vm.historyRing, field) },
                                        enabled = !vm.busy,
                                    ) {
                                        Text(stringResource(R.string.releases_retry))
                                    }
                                }
                            }
                            series.values.none { it != null } -> {
                                Text(stringResource(R.string.health_no_values), style = MaterialTheme.typography.bodyMedium)
                            }
                            else -> Chart(series)
                        }
                    }
                }
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
    val fill = MaterialTheme.colorScheme.primary.copy(alpha = 0.16f)
    val grid = MaterialTheme.colorScheme.outlineVariant
    val locale = LocalConfiguration.current.locales[0]
    val axisFmt = remember(locale) { SimpleDateFormat("dd.MM HH:mm", locale) }
    val inspectFmt = remember(locale) { DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT, locale) }
    var selectedIdx by remember(s) { mutableStateOf<Int?>(null) }

    val inspectedIdx = selectedIdx?.takeIf { it in s.values.indices && s.values[it] != null }
    Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth()) {
        if (inspectedIdx != null) {
            val v = s.values[inspectedIdx]!!
            val t = s.timeOf(inspectedIdx)
            val stamp = if (t != null) inspectFmt.format(Date(t * 1000)) else "#$inspectedIdx"
            Text(
                "$stamp · ${format(s.field, v)}",
                style = MaterialTheme.typography.titleSmall,
                color = MaterialTheme.colorScheme.primary,
                modifier = Modifier.weight(1f),
            )
            TextButton(onClick = { selectedIdx = null }) { Text(stringResource(R.string.build_close)) }
        } else {
            Text(
                stringResource(R.string.health_range, format(s.field, lo), format(s.field, hi)),
                style = MaterialTheme.typography.bodySmall,
                modifier = Modifier.weight(1f),
            )
            Text(
                stringResource(R.string.health_last, format(s.field, s.values.lastOrNull { it != null } ?: lo)),
                style = MaterialTheme.typography.bodySmall,
            )
        }
    }
    Canvas(
        Modifier
            .fillMaxWidth()
            .height(170.dp)
            .pointerInput(s) {
                awaitPointerEventScope {
                    while (true) {
                        val event = awaitPointerEvent()
                        val change = event.changes.firstOrNull() ?: continue
                        if (change.pressed && size.width > 0 && s.values.isNotEmpty()) {
                            val rawIdx = ((change.position.x / size.width) * (s.values.size - 1)).toInt()
                                .coerceIn(0, s.values.size - 1)
                            // snap to nearest non-null sample
                            val nearest = s.values.indices.filter { s.values[it] != null }
                                .minByOrNull { kotlin.math.abs(it - rawIdx) }
                            selectedIdx = nearest
                        }
                    }
                }
            }
    ) {
        val n = s.values.size
        val span = (hi - lo).coerceAtLeast(1).toFloat()
        val pad = 6.dp.toPx()
        fun x(i: Int) = if (n <= 1) size.width / 2 else i * size.width / (n - 1)
        fun y(v: Int) = pad + (size.height - 2 * pad) * (1 - (v - lo) / span)
        for (k in 0..2) {
            val gy = pad + (size.height - 2 * pad) * k / 2
            drawLine(grid, Offset(0f, gy), Offset(size.width, gy), strokeWidth = 1f)
        }
        // a gap (the node was off, or the value was not measured) breaks the line
        val path = Path()
        val fillPath = Path()
        var open = false
        var segStartX = 0f
        var lastX = 0f
        val bottomY = size.height - pad
        for ((i, v) in s.values.withIndex()) {
            if (v == null) {
                if (open) {
                    fillPath.lineTo(lastX, bottomY)
                    fillPath.lineTo(segStartX, bottomY)
                    fillPath.close()
                }
                open = false
                continue
            }
            val px = x(i)
            val py = y(v)
            if (open) {
                path.lineTo(px, py)
                fillPath.lineTo(px, py)
            } else {
                path.moveTo(px, py)
                fillPath.moveTo(px, py)
                segStartX = px
            }
            lastX = px
            open = true
        }
        if (open) {
            fillPath.lineTo(lastX, bottomY)
            fillPath.lineTo(segStartX, bottomY)
            fillPath.close()
        }
        drawPath(fillPath, fill)
        drawPath(path, line, style = Stroke(width = 2.dp.toPx()))
        // a single point between gaps would not show as a line
        for ((i, v) in s.values.withIndex()) {
            if (v != null && s.values.getOrNull(i - 1) == null && s.values.getOrNull(i + 1) == null) {
                drawCircle(line, 2.5.dp.toPx(), Offset(x(i), y(v)))
            }
        }
        if (inspectedIdx != null) {
            val ix = x(inspectedIdx)
            val iy = y(s.values[inspectedIdx]!!)
            drawLine(line, Offset(ix, 0f), Offset(ix, size.height), strokeWidth = 1.5.dp.toPx())
            drawCircle(line, 4.5.dp.toPx(), Offset(ix, iy))
        }
    }
    val first = s.timeOf(0)
    val last = s.timeOf(s.values.size - 1)
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
        Text(
            if (first != null) axisFmt.format(Date(first * 1000))
            else stringResource(if (s.ring == HistorySeries.RING_DAY) R.string.health_ago_day else R.string.health_ago_week),
            style = MaterialTheme.typography.labelSmall,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
        Text(
            if (last != null) axisFmt.format(Date(last * 1000)) else stringResource(R.string.health_now),
            style = MaterialTheme.typography.labelSmall,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
    }
    if (inspectedIdx == null) {
        Text(
            stringResource(R.string.chart_touch_hint),
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

/** The self tests kept on this phone, newest first: collapsible card. */
@Composable
private fun PastChecks(vm: AppViewModel) {
    var expanded by rememberSaveable(vm.healthDeviceName) { mutableStateOf(false) }
    val locale = LocalConfiguration.current.locales[0]
    val fmt = remember(locale) { DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT, locale) }
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.fillMaxWidth()) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                modifier = Modifier
                    .fillMaxWidth()
                    .clickable { expanded = !expanded }
                    .padding(12.dp),
            ) {
                Text(
                    "${stringResource(R.string.health_past)} (${vm.healthRecords.size})",
                    style = MaterialTheme.typography.titleMedium,
                    modifier = Modifier.weight(1f),
                )
                Icon(
                    if (expanded) Icons.Default.KeyboardArrowUp else Icons.Default.KeyboardArrowDown,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            if (expanded) {
                HorizontalDivider()
                Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    for (r in vm.healthRecords.asReversed().take(10)) {
                        val t = SelfTest.parse(r.line) ?: continue
                        val what = buildString {
                            append(if (t.ok) stringResource(R.string.hv_fine) else t.problems.joinToString(", ").ifEmpty { "WARN" })
                            t.batteryMv?.let { append(String.format(Locale.ROOT, " · %.2f V", it / 1000.0)) }
                            t.batteryPercent?.let { append(" $it%") }
                            t.mailbox?.let { append(" · ✉ $it") }
                        }
                        HealthRow(fmt.format(Date(r.time)), what)
                    }
                }
            }
        }
    }
}
