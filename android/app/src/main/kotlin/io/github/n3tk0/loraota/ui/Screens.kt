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
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import io.github.n3tk0.loraota.R
import io.github.n3tk0.loraota.ota.OtaProtocol
import io.github.n3tk0.loraota.ota.hex

@Composable
fun AppScreen(vm: AppViewModel) {
    Surface(modifier = Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
        Column(
            modifier = Modifier
                .safeDrawingPadding()
                .imePadding()
                .verticalScroll(rememberScrollState())
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Text(stringResource(R.string.app_name), style = MaterialTheme.typography.headlineSmall)
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
                })
                Text(stringResource(R.string.image) + ": ${info.imageSize} B, SHA-256 ${hex(info.imageSha).take(12)}…",
                    fontFamily = FontFamily.Monospace, style = MaterialTheme.typography.bodySmall)
            }
        }
    }

    HorizontalDivider()
    Text(stringResource(R.string.running_firmware), style = MaterialTheme.typography.titleMedium)
    val old = vm.oldImage
    Text(old?.name ?: stringResource(R.string.running_unknown))
    OutlinedButton(onClick = { pickOld.launch(arrayOf("*/*")) }, enabled = !vm.busy) { Text(stringResource(R.string.pick_file)) }

    Text(stringResource(R.string.new_firmware), style = MaterialTheme.typography.titleMedium)
    for (f in vm.newCandidates()) {
        Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier
            .fillMaxWidth()
            .clickable(enabled = !vm.busy) { vm.pickRelease(f) }) {
            RadioButton(selected = vm.newImage?.sha == f.sha256, onClick = { vm.pickRelease(f) }, enabled = !vm.busy)
            Column {
                Text("${f.release}  (${f.env})")
                Text(f.file, style = MaterialTheme.typography.bodySmall)
            }
        }
    }
    vm.newImage?.let { Text(stringResource(R.string.chosen, it.name)) }
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        OutlinedButton(onClick = { pickNew.launch(arrayOf("*/*")) }, enabled = !vm.busy) { Text(stringResource(R.string.pick_file)) }
        OutlinedButton(onClick = { vm.loadReleases() }, enabled = !vm.busy) { Text(stringResource(R.string.reload_releases)) }
    }

    HorizontalDivider()
    Text(stringResource(R.string.side_channel), style = MaterialTheme.typography.titleMedium)
    Text(stringResource(R.string.side_channel_hint), style = MaterialTheme.typography.bodySmall)
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        NumberField(vm.freqMhz, { vm.freqMhz = it }, "MHz", Modifier.weight(1.4f))
        NumberField(vm.bwKhz, { vm.bwKhz = it }, "BW kHz", Modifier.weight(1f))
    }
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        NumberField(vm.sf, { vm.sf = it }, "SF", Modifier.weight(1f))
        NumberField(vm.cr, { vm.cr = it }, "CR", Modifier.weight(1f))
        NumberField(vm.dutyPercent, { vm.dutyPercent = it }, stringResource(R.string.duty), Modifier.weight(1f))
    }

    HorizontalDivider()
    Button(onClick = { vm.makeDelta() }, enabled = !vm.busy && vm.oldImage != null && vm.newImage != null) {
        Text(stringResource(R.string.make_delta))
    }
    vm.patch?.let { p ->
        val minutes = vm.estimateMinutes() ?: 0.0
        Text(stringResource(R.string.delta_size, p.size / 1024.0, vm.newImage?.data?.size?.div(1024.0) ?: 0.0, minutes))
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Button(onClick = { vm.send() }, enabled = !vm.busy) { Text(stringResource(R.string.send)) }
            if (vm.busy) OutlinedButton(onClick = { vm.stop() }) { Text(stringResource(R.string.stop)) }
        }
    }
    if (vm.total > 0) {
        LinearProgressIndicator(progress = { vm.sent.toFloat() / vm.total }, modifier = Modifier.fillMaxWidth())
        Text("${vm.sent} / ${vm.total}")
    }
    if (vm.done) {
        Text(stringResource(R.string.verify_hint))
        Button(onClick = { vm.verify() }, enabled = !vm.busy) { Text(stringResource(R.string.verify)) }
    }
    for (line in vm.log.takeLast(8)) Text(line, style = MaterialTheme.typography.bodySmall, fontFamily = FontFamily.Monospace)
    Spacer(Modifier.height(24.dp))
}

@Composable
private fun NumberField(value: String, onChange: (String) -> Unit, label: String, modifier: Modifier) {
    OutlinedTextField(
        value = value, onValueChange = onChange, label = { Text(label) }, singleLine = true,
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal), modifier = modifier.width(80.dp)
    )
}
