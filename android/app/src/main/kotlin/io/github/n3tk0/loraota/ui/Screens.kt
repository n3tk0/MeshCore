package io.github.n3tk0.loraota.ui

import android.annotation.SuppressLint
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.delay
import io.github.n3tk0.loraota.Lang
import io.github.n3tk0.loraota.R
import io.github.n3tk0.loraota.ota.Phase
import io.github.n3tk0.loraota.ota.OtaProtocol
import io.github.n3tk0.loraota.ota.hex

@Composable
fun AppScreen(vm: AppViewModel, setLang: (String) -> Unit) {
    val ctx = LocalContext.current
    val scroll = rememberScrollState()
    // the lists are long: the progress and the result are below them, out of sight
    LaunchedEffect(vm.busy, vm.message) {
        if ((vm.step == Step.REPEATER || vm.step == Step.FLASH) && (vm.busy || vm.message.isNotEmpty())) {
            scroll.animateScrollTo(scroll.maxValue)
        }
    }
    Surface(modifier = Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
        Column(
            modifier = Modifier
                .safeDrawingPadding()
                .imePadding()
                .verticalScroll(scroll)
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(stringResource(R.string.app_name), style = MaterialTheme.typography.headlineSmall, modifier = Modifier.weight(1f))
                // the other language, in its own words
                val other = if (Lang.current(ctx) == "en") "bg" else "en"
                TextButton(onClick = { setLang(other) }, enabled = !vm.busy) { Text(if (other == "en") "English" else "Български") }
            }
            when (vm.step) {
                Step.COMPANION -> CompanionStep(vm)
                Step.REPEATER -> RepeaterStep(vm)
                Step.UPDATE -> UpdateStep(vm)
                Step.FLASH -> FlashStep(vm)
            }
            if (vm.step != Step.UPDATE && vm.step != Step.FLASH) {
                if (vm.busy) Busy(vm)
                Message(vm)
            }
        }
    }
}

@SuppressLint("MissingPermission")
@Composable
private fun CompanionStep(vm: AppViewModel) {
    Text(stringResource(R.string.companion_hint))
    Button(onClick = { vm.scan() }, enabled = !vm.scanning && !vm.busy) {
        Text(stringResource(if (vm.scanning) R.string.scanning else R.string.scan))
    }
    for (d in vm.devices) {
        Card(modifier = Modifier
            .fillMaxWidth()
            .clickable(enabled = !vm.busy) { vm.connect(d) }) {
            Column(Modifier.padding(12.dp)) {
                Text(d.name ?: "?", style = MaterialTheme.typography.titleMedium)
                Text(d.address, style = MaterialTheme.typography.bodySmall)
            }
        }
    }
}

@Composable
private fun RepeaterStep(vm: AppViewModel) {
    var selected by remember { mutableStateOf<Int?>(null) }
    var password by remember { mutableStateOf("") }
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(stringResource(R.string.connected_to, vm.companionName), modifier = Modifier.weight(1f))
        TextButton(onClick = { vm.disconnect() }) { Text(stringResource(R.string.disconnect)) }
    }
    OutlinedButton(onClick = { vm.openFlash(FlashTarget.COMPANION) }, enabled = !vm.busy) {
        Text(stringResource(R.string.flash_companion_open))
    }
    Text(stringResource(R.string.repeater_hint))
    if (vm.repeaters.isEmpty()) Text(stringResource(R.string.no_repeaters))
    fun choose(i: Int) {
        selected = i
        password = vm.savedPassword(vm.repeaters[i]) ?: ""
    }
    vm.repeaters.forEachIndexed { i, c ->
        Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier
            .fillMaxWidth()
            .clickable { choose(i) }) {
            RadioButton(selected = selected == i, onClick = { choose(i) })
            Column {
                Text(c.name)
                val hops = c.hops
                Text(
                    when (hops) {
                        null -> stringResource(R.string.route_unknown)
                        0 -> stringResource(R.string.route_direct)
                        else -> pluralStringResource(R.plurals.route_hops, hops, hops)
                    },
                    style = MaterialTheme.typography.bodySmall,
                    color = if (hops == 0) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }
    OutlinedTextField(
        value = password, onValueChange = { password = it },
        label = { Text(stringResource(R.string.admin_password)) },
        visualTransformation = PasswordVisualTransformation(),
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password),
        singleLine = true, modifier = Modifier.fillMaxWidth()
    )
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
        Button(
            onClick = { selected?.let { vm.login(vm.repeaters[it], password) } },
            enabled = selected != null && !vm.busy
        ) { Text(stringResource(R.string.login)) }
        val chosen = selected?.let { vm.repeaters.getOrNull(it) }
        if (chosen != null && vm.savedPassword(chosen) != null) {
            TextButton(onClick = { vm.forgetPassword(chosen); password = "" }, enabled = !vm.busy) {
                Text(stringResource(R.string.forget_password))
            }
        }
    }
}

@Composable
private fun UpdateStep(vm: AppViewModel) {
    val info = vm.info
    var advanced by rememberSaveable { mutableStateOf(false) }
    val pickOld = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) vm.pickFile(uri, true)
    }
    val pickNew = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) vm.pickFile(uri, false)
    }

    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(vm.repeater?.name ?: "", style = MaterialTheme.typography.titleLarge, modifier = Modifier.weight(1f))
        TextButton(onClick = { vm.backToRepeaters() }, enabled = !vm.busy) { Text(stringResource(R.string.back)) }
    }
    if (info != null) {
        Card(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(12.dp)) {
                Text(stringResource(R.string.running_now), style = MaterialTheme.typography.labelMedium)
                Text(info.ident.replace('|', '\n'))
                Text(stringResource(R.string.platform) + ": " + when (info.platform) {
                    OtaProtocol.PLATFORM_NRF52 -> "nRF52"
                    OtaProtocol.PLATFORM_ESP32 -> "ESP32"
                    else -> "?"
                }, style = MaterialTheme.typography.bodySmall)
            }
        }
        if (!info.enabled) {
            Warning(stringResource(R.string.ota_off))
            Button(onClick = { vm.enableOta() }, enabled = !vm.busy) { Text(stringResource(R.string.enable_ota)) }
        }
        // a message about the cut-short update already says what to do
        if ((info.state == OtaProtocol.STATE_PAUSED || vm.resumable) && vm.message.isEmpty()) Text(stringResource(R.string.paused_hint))
        if (!vm.companionOta) Warning(stringResource(R.string.companion_no_ota))
        vm.routeHops.let { hops ->
            if (hops == null) Warning(stringResource(R.string.msg_no_route))
            else if (hops > 0) Warning(stringResource(R.string.msg_not_direct, hops))
        }
    }
    StatusCard(vm)
    OutlinedButton(onClick = { vm.openFlash(FlashTarget.REPEATER) }, enabled = !vm.busy) { Text(stringResource(R.string.flash_open)) }
    if (info == null) {
        // a stock repeater (or the answer got lost): login and status only
        if (!vm.busy) {
            if (vm.otaChecked) Warning(stringResource(R.string.no_ota))
            OutlinedButton(onClick = { vm.recheck() }) { Text(stringResource(R.string.recheck)) }
        }
        if (vm.busy) Busy(vm)
        Message(vm)
        return
    }

    Text(stringResource(R.string.new_firmware), style = MaterialTheme.typography.titleMedium)
    val candidates = vm.newCandidates()
    if (candidates.isEmpty() && vm.newImage == null) Text(stringResource(R.string.no_new_firmware))
    for (f in candidates) {
        Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier
            .fillMaxWidth()
            .clickable(enabled = !vm.busy) { vm.pickRelease(f) }) {
            RadioButton(selected = vm.newImage?.sha == f.sha256, onClick = { vm.pickRelease(f) }, enabled = !vm.busy)
            Column {
                Text(f.version.ifEmpty { f.release }, style = MaterialTheme.typography.titleMedium)
                Text(f.env.replace('_', ' '), style = MaterialTheme.typography.bodySmall)
            }
        }
    }
    if (vm.newImage != null && candidates.none { it.sha256 == vm.newImage?.sha }) {
        Text(stringResource(R.string.chosen, vm.newImage!!.name))
    }
    if (vm.oldImage == null && info != null) Warning(stringResource(R.string.running_unknown))
    if (vm.inPlace()) InPlaceWarning(detail = true)
    if (vm.nrfCopy()) CopyNote()

    val ready = vm.oldImage != null && vm.newImage != null && info.enabled && vm.direct && vm.companionOta
    val continuing = info?.state == OtaProtocol.STATE_PAUSED || vm.resumable
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
        Button(onClick = { vm.update() }, enabled = ready && !vm.busy) {
            Text(stringResource(if (continuing) R.string.resume else R.string.update))
        }
        if (vm.busy && vm.phase != null) OutlinedButton(onClick = { vm.stop() }) { Text(stringResource(R.string.stop)) }
    }

    vm.phase?.let { PhaseView(vm, it) }
    if (vm.busy && vm.phase == null) Busy(vm)
    Message(vm)
    if (vm.done) Text(stringResource(R.string.done_hint))

    if (vm.confirmUpdate) {
        val p = vm.patch
        AlertDialog(
            onDismissRequest = { vm.confirmUpdate = false },
            title = { Text(stringResource(if (continuing) R.string.resume else R.string.update)) },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text(stringResource(R.string.confirm_text, vm.newImage?.name ?: "", (p?.size ?: 0) / 1024.0,
                        vm.estimateMinutes() ?: 0.0, vm.sideRadio().cliArgs()))
                    if (vm.inPlace()) InPlaceWarning(detail = false)
                    if (vm.nrfCopy()) CopyNote()
                    if (vm.ignoreAirLimit) Text(stringResource(R.string.ignore_air_limit_warning), color = MaterialTheme.colorScheme.error)
                }
            },
            confirmButton = { TextButton(onClick = { vm.send() }) { Text(stringResource(R.string.start)) } },
            dismissButton = { TextButton(onClick = { vm.confirmUpdate = false }) { Text(stringResource(R.string.cancel)) } },
        )
    }

    HorizontalDivider()
    TextButton(onClick = { advanced = !advanced }) {
        Text(stringResource(if (advanced) R.string.advanced_hide else R.string.advanced))
    }
    if (advanced) {
        if (info != null) {
            Text(stringResource(R.string.image) + ": ${info.imageSize} B, SHA-256 ${hex(info.imageSha).take(12)}…",
                fontFamily = FontFamily.Monospace, style = MaterialTheme.typography.bodySmall)
        }
        Text(stringResource(R.string.running_firmware), style = MaterialTheme.typography.titleSmall)
        Text(vm.oldImage?.name ?: "—", style = MaterialTheme.typography.bodySmall)
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedButton(onClick = { pickOld.launch(arrayOf("*/*")) }, enabled = !vm.busy) { Text(stringResource(R.string.pick_running)) }
            OutlinedButton(onClick = { pickNew.launch(arrayOf("*/*")) }, enabled = !vm.busy) { Text(stringResource(R.string.pick_new)) }
        }
        OutlinedButton(onClick = { vm.loadReleases() }, enabled = !vm.busy) { Text(stringResource(R.string.reload_releases)) }

        Text(stringResource(R.string.side_channel), style = MaterialTheme.typography.titleSmall)
        Text(stringResource(R.string.side_channel_hint), style = MaterialTheme.typography.bodySmall)
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            NumberField(vm.freqMhz, { vm.freqMhz = it }, "MHz", Modifier.weight(1.4f), !vm.busy)
            NumberField(vm.bwKhz, { vm.bwKhz = it }, "BW kHz", Modifier.weight(1f), !vm.busy)
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            NumberField(vm.sf, { vm.sf = it }, "SF", Modifier.weight(1f), !vm.busy)
            NumberField(vm.cr, { vm.cr = it }, "CR", Modifier.weight(1f), !vm.busy)
            NumberField(vm.dutyPercent, { vm.dutyPercent = it }, stringResource(R.string.duty), Modifier.weight(1f), !vm.busy && !vm.ignoreAirLimit)
        }
        Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier
            .fillMaxWidth()
            .clickable(enabled = !vm.busy) { vm.ignoreAirLimit = !vm.ignoreAirLimit }) {
            Text(stringResource(R.string.ignore_air_limit), modifier = Modifier.weight(1f))
            Switch(checked = vm.ignoreAirLimit, onCheckedChange = { vm.ignoreAirLimit = it }, enabled = !vm.busy)
        }
        if (vm.ignoreAirLimit) Warning(stringResource(R.string.ignore_air_limit_warning))
        for (line in vm.log.takeLast(10)) Text(line, style = MaterialTheme.typography.bodySmall, fontFamily = FontFamily.Monospace)
    }
    Spacer(Modifier.height(24.dp))
}

@Composable
private fun FlashStep(vm: AppViewModel) {
    val pick = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) vm.pickFlashFile(uri)
    }
    val toCompanion = vm.flashTarget == FlashTarget.COMPANION
    val writing = vm.busy && vm.flashPercent >= 0
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(stringResource(R.string.flash_title, if (toCompanion) vm.companionName else vm.repeater?.name ?: ""),
            style = MaterialTheme.typography.titleLarge, modifier = Modifier.weight(1f))
        TextButton(onClick = { vm.closeFlash() }, enabled = !vm.busy) { Text(stringResource(R.string.back)) }
    }
    Text(stringResource(if (toCompanion) R.string.flash_hint_companion else R.string.flash_hint_repeater))
    if (toCompanion && vm.companionModel.isNotEmpty()) {
        Text(stringResource(R.string.flash_board, vm.companionModel, vm.companionVersion), style = MaterialTheme.typography.bodySmall)
    }
    if (!vm.flashDone) {
        val candidates = vm.flashCandidates()
        if (candidates.isEmpty()) Text(stringResource(R.string.flash_none))
        for (f in candidates) {
            Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier
                .fillMaxWidth()
                .clickable(enabled = !vm.busy) { vm.pickFlashRelease(f) }) {
                RadioButton(selected = vm.flashImage?.name == f.dfu && vm.flashImage?.env == f.env,
                    onClick = { vm.pickFlashRelease(f) }, enabled = !vm.busy)
                Column {
                    Text(f.version.ifEmpty { f.release }, style = MaterialTheme.typography.titleMedium)
                    Text(f.env.replace('_', ' ') + if (f.dfuIsZip) " · Bluetooth" else " · Wi-Fi", style = MaterialTheme.typography.bodySmall)
                }
            }
        }
        OutlinedButton(onClick = { pick.launch(arrayOf("*/*")) }, enabled = !vm.busy) { Text(stringResource(R.string.flash_from_file)) }
        vm.flashImage?.let { if (it.env == null) Text(stringResource(R.string.chosen, it.name)) }
        // no Stop while it writes: a cut-short DFU leaves the device in its bootloader
        Button(onClick = { vm.flashCheck() }, enabled = vm.flashImage != null && !vm.busy) { Text(stringResource(R.string.flash_start)) }
    }
    if (writing) {
        Text(stringResource(R.string.flash_progress, vm.flashPercent), style = MaterialTheme.typography.titleMedium)
        LinearProgressIndicator(progress = { vm.flashPercent / 100f }, modifier = Modifier.fillMaxWidth())
        if (vm.working.isNotEmpty()) Text(vm.working, style = MaterialTheme.typography.bodySmall)
    } else if (vm.busy) Busy(vm)
    Message(vm)
    if (vm.flashDone && toCompanion) Button(onClick = { vm.closeFlash() }) { Text(stringResource(R.string.reconnect)) }

    vm.flashPlan?.let { p ->
        AlertDialog(
            onDismissRequest = { vm.flashCancel() },
            title = { Text(stringResource(R.string.flash_start)) },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text(stringResource(R.string.flash_confirm_text, p.image.name, p.device))
                    for (w in p.warnings) Text(w, color = MaterialTheme.colorScheme.error)
                }
            },
            confirmButton = { TextButton(onClick = { vm.flashStart() }) { Text(stringResource(R.string.flash_start)) } },
            dismissButton = { TextButton(onClick = { vm.flashCancel() }) { Text(stringResource(R.string.cancel)) } },
        )
    }
    Spacer(Modifier.height(24.dp))
}

@Composable
private fun Busy(vm: AppViewModel) {
    LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
    if (vm.working.isNotEmpty()) Text(vm.working, style = MaterialTheme.typography.bodyMedium)
}

/** What every repeater tells an admin, also one with stock firmware. */
@Composable
private fun StatusCard(vm: AppViewModel) {
    val s = vm.status
    if (s == null) {
        if (!vm.busy) Text(stringResource(R.string.status_none), color = MaterialTheme.colorScheme.onSurfaceVariant)
        return
    }
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(2.dp)) {
            Text(stringResource(R.string.status_title), style = MaterialTheme.typography.labelMedium)
            Text(stringResource(R.string.status_batt_uptime, s.battMv / 1000.0, duration(s.upTimeSecs)))
            Text(stringResource(R.string.status_radio, s.noiseFloor, s.lastRssi) +
                (s.lastSnr?.let { ", SNR %.1f dB".format(it) } ?: ""), style = MaterialTheme.typography.bodySmall)
            Text(stringResource(R.string.status_packets, s.packetsRecv, s.packetsSent, duration(s.airTimeSecs)), style = MaterialTheme.typography.bodySmall)
        }
    }
}

@Composable
private fun duration(secs: Long): String {
    val mins = secs / 60
    return when {
        mins >= 24 * 60 -> stringResource(R.string.dur_days, (mins / (24 * 60)).toInt(), (mins / 60 % 24).toInt())
        mins >= 60 -> stringResource(R.string.dur_hours, (mins / 60).toInt(), (mins % 60).toInt())
        else -> stringResource(R.string.dur_mins, mins.toInt())
    }
}

@Composable
private fun Message(vm: AppViewModel) {
    if (vm.message.isNotEmpty()) {
        Text(vm.message, color = if (vm.messageIsError) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.primary)
    }
}

@Composable
private fun Warning(text: String) {
    Card(Modifier.fillMaxWidth()) {
        Text(text, color = MaterialTheme.colorScheme.error, modifier = Modifier.padding(12.dp))
    }
}

/** Big and red: an update "in place" can leave the repeater with no firmware. */
@Composable
private fun InPlaceWarning(detail: Boolean) {
    Card(
        Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.errorContainer,
            contentColor = MaterialTheme.colorScheme.onErrorContainer,
        ),
    ) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Text(stringResource(R.string.in_place_title), style = MaterialTheme.typography.headlineSmall, fontWeight = FontWeight.Bold)
            Text(stringResource(R.string.in_place_text), style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold)
            if (detail) Text(stringResource(R.string.in_place_detail), style = MaterialTheme.typography.bodyMedium)
        }
    }
}

/** Smaller: every nRF52 update ends with the same copy over the running firmware. */
@Composable
private fun CopyNote() {
    Card(
        Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.tertiaryContainer,
            contentColor = MaterialTheme.colorScheme.onTertiaryContainer,
        ),
    ) {
        Text(stringResource(R.string.copy_note), style = MaterialTheme.typography.bodyMedium, modifier = Modifier.padding(12.dp))
    }
}

@Composable
private fun PhaseView(vm: AppViewModel, phase: Phase) {
    var now by remember { mutableStateOf(System.currentTimeMillis()) }
    LaunchedEffect(phase, vm.waitUntil) {
        while (true) {
            now = System.currentTimeMillis()
            delay(1000)
        }
    }
    val minsLeft = ((vm.waitUntil - now).coerceAtLeast(0) + 59_999) / 60_000
    val text = when (phase) {
        Phase.START -> stringResource(R.string.phase_start)
        Phase.SWITCH -> stringResource(R.string.phase_switch)
        Phase.SEND -> {
            val percent = if (vm.total > 0) vm.sent * 100 / vm.total else 0
            stringResource(R.string.phase_send, percent, vm.remainingMinutes())
        }
        Phase.WAIT_AIR -> stringResource(R.string.phase_wait_air, minsLeft)
        Phase.FINISH -> stringResource(R.string.phase_finish)
        Phase.RESTART -> stringResource(R.string.phase_restart)
    }
    Text(text, style = MaterialTheme.typography.titleMedium)
    if (vm.total > 0 && phase != Phase.RESTART) {
        LinearProgressIndicator(progress = { vm.sent.toFloat() / vm.total }, modifier = Modifier.fillMaxWidth())
    }
}

@Composable
private fun NumberField(value: String, onChange: (String) -> Unit, label: String, modifier: Modifier, enabled: Boolean) {
    OutlinedTextField(
        value = value, onValueChange = onChange, label = { Text(label) }, singleLine = true, enabled = enabled,
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal), modifier = modifier.width(80.dp)
    )
}
