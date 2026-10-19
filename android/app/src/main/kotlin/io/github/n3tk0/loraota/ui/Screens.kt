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
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Surface
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
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
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
    Surface(modifier = Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
        Column(
            modifier = Modifier
                .safeDrawingPadding()
                .imePadding()
                .verticalScroll(rememberScrollState())
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
            }
            if (vm.busy) LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
            if (vm.message.isNotEmpty()) {
                Text(vm.message, color = MaterialTheme.colorScheme.primary)
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
    Text(stringResource(R.string.repeater_hint))
    if (vm.repeaters.isEmpty()) Text(stringResource(R.string.no_repeaters))
    vm.repeaters.forEachIndexed { i, c ->
        Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier
            .fillMaxWidth()
            .clickable { selected = i }) {
            RadioButton(selected = selected == i, onClick = { selected = i })
            Text(c.name)
        }
    }
    OutlinedTextField(
        value = password, onValueChange = { password = it },
        label = { Text(stringResource(R.string.admin_password)) },
        visualTransformation = PasswordVisualTransformation(),
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password),
        singleLine = true, modifier = Modifier.fillMaxWidth()
    )
    Button(
        onClick = { selected?.let { vm.login(vm.repeaters[it], password) } },
        enabled = selected != null && !vm.busy
    ) { Text(stringResource(R.string.login)) }
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
                Text(info.ident.replace('|', '\n'))
                Text(stringResource(R.string.platform) + ": " + when (info.platform) {
                    OtaProtocol.PLATFORM_NRF52 -> "nRF52"
                    OtaProtocol.PLATFORM_ESP32 -> "ESP32"
                    else -> "?"
                }, style = MaterialTheme.typography.bodySmall)
            }
        }
        if (!info.enabled) Warning(stringResource(R.string.ota_off))
        if (info.state == OtaProtocol.STATE_PAUSED || vm.resumable) Text(stringResource(R.string.paused_hint))
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
                Text(f.release)
                Text(f.env, style = MaterialTheme.typography.bodySmall)
            }
        }
    }
    if (vm.newImage != null && candidates.none { it.sha256 == vm.newImage?.sha }) {
        Text(stringResource(R.string.chosen, vm.newImage!!.name))
    }
    if (vm.oldImage == null && info != null) Warning(stringResource(R.string.running_unknown))

    val ready = vm.oldImage != null && vm.newImage != null && info?.enabled == true
    val continuing = info?.state == OtaProtocol.STATE_PAUSED || vm.resumable
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
        Button(onClick = { vm.update() }, enabled = ready && !vm.busy) {
            Text(stringResource(if (continuing) R.string.resume else R.string.update))
        }
        if (vm.busy && vm.phase != null) OutlinedButton(onClick = { vm.stop() }) { Text(stringResource(R.string.stop)) }
    }

    vm.phase?.let { PhaseView(vm, it) }
    if (vm.done) Text(stringResource(R.string.done_hint))

    if (vm.confirmUpdate) {
        val p = vm.patch
        AlertDialog(
            onDismissRequest = { vm.confirmUpdate = false },
            title = { Text(stringResource(if (continuing) R.string.resume else R.string.update)) },
            text = {
                Text(stringResource(R.string.confirm_text, vm.newImage?.name ?: "", (p?.size ?: 0) / 1024.0,
                    vm.estimateMinutes() ?: 0.0, vm.sideRadio().cliArgs()))
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
            NumberField(vm.dutyPercent, { vm.dutyPercent = it }, stringResource(R.string.duty), Modifier.weight(1f), !vm.busy)
        }
        for (line in vm.log.takeLast(10)) Text(line, style = MaterialTheme.typography.bodySmall, fontFamily = FontFamily.Monospace)
    }
    Spacer(Modifier.height(24.dp))
}

@Composable
private fun Warning(text: String) {
    Card(Modifier.fillMaxWidth()) {
        Text(text, color = MaterialTheme.colorScheme.error, modifier = Modifier.padding(12.dp))
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
