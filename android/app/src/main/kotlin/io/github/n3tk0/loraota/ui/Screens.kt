package io.github.n3tk0.loraota.ui

import android.annotation.SuppressLint
import androidx.activity.compose.BackHandler
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
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.Lock
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.RadioButton
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Tab
import androidx.compose.material3.TabRow
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import io.github.n3tk0.loraota.Lang
import io.github.n3tk0.loraota.R
import io.github.n3tk0.loraota.backup.DeviceKind
import io.github.n3tk0.loraota.ble.Contact
import io.github.n3tk0.loraota.github.BuildPhase
import io.github.n3tk0.loraota.github.BuildService
import io.github.n3tk0.loraota.github.GitHubApi
import io.github.n3tk0.loraota.github.WorkflowInput
import io.github.n3tk0.loraota.ota.FirmwareFile
import io.github.n3tk0.loraota.ota.OtaProtocol
import io.github.n3tk0.loraota.ota.Phase
import io.github.n3tk0.loraota.ota.hex
import kotlinx.coroutines.delay

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AppScreen(vm: AppViewModel, setLang: (String) -> Unit) {
    var askStop by remember { mutableStateOf(false) }      // Back during an update
    var flashNoStop by remember { mutableStateOf(false) }  // Back during a flash
    fun back() = when {
        vm.running && vm.flashPercent >= 0 -> flashNoStop = true
        vm.running -> askStop = true
        vm.canStop -> vm.stop()   // a login, a scan for the device...: just stop waiting
        vm.busy -> {}             // a disconnect's clean-up: it ends on its own
        else -> vm.back()
    }
    BackHandler(enabled = vm.step != Step.COMPANION || vm.canStop) { back() }

    Scaffold(
        modifier = Modifier.imePadding(),
        topBar = {
            TopAppBar(
                title = {
                    Text(
                        when (vm.step) {
                            Step.COMPANION -> stringResource(R.string.app_name)
                            Step.REPEATER -> vm.companionName
                            Step.UPDATE -> vm.repeater?.name ?: ""
                            Step.FLASH -> stringResource(R.string.flash_title, vm.companionName)
                            Step.BACKUP -> stringResource(R.string.backup_title, vm.backupDeviceName)
                            Step.HEALTH -> stringResource(R.string.health_title, vm.healthDeviceName)
                            Step.QUEUE -> stringResource(R.string.q_title)
                        },
                        maxLines = 1, overflow = TextOverflow.Ellipsis,
                    )
                },
                navigationIcon = {
                    if (vm.step != Step.COMPANION) IconButton(onClick = { back() }) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, stringResource(R.string.back))
                    }
                },
                actions = {
                    if (vm.step == Step.REPEATER) IconButton(onClick = { vm.refreshRepeaters() }, enabled = !vm.busy) {
                        Icon(Icons.Default.Refresh, stringResource(R.string.refresh))
                    }
                    LanguageMenu(enabled = !vm.busy, setLang)
                },
            )
        },
        bottomBar = { BottomStatus(vm) },
    ) { pad ->
        Surface(modifier = Modifier.fillMaxSize().padding(pad), color = MaterialTheme.colorScheme.background) {
            Column(
                modifier = Modifier.verticalScroll(rememberScrollState()).padding(16.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                when (vm.step) {
                    Step.COMPANION -> CompanionStep(vm)
                    Step.REPEATER -> RepeaterStep(vm)
                    Step.UPDATE -> if (vm.running) RunningView(vm) else RepeaterHome(vm)
                    Step.FLASH -> if (vm.running) RunningView(vm) else FlashContent(vm)
                    Step.BACKUP -> BackupScreen(vm)
                    Step.HEALTH -> HealthScreen(vm)
                    Step.QUEUE -> QueueScreen(vm)
                }
                Spacer(Modifier.height(24.dp))
            }
        }
    }

    if (askStop) {
        AlertDialog(
            onDismissRequest = { askStop = false },
            title = { Text(stringResource(R.string.back_stop_title)) },
            text = { Text(stringResource(R.string.back_stop_text)) },
            confirmButton = { TextButton(onClick = { askStop = false; vm.stop() }) { Text(stringResource(R.string.stop)) } },
            dismissButton = { TextButton(onClick = { askStop = false }) { Text(stringResource(R.string.cancel)) } },
        )
    }
    // on every screen: a tab change or a step must not hide it while the repeater waits in DFU mode
    vm.flashPlan?.let { p ->
        AlertDialog(
            onDismissRequest = { vm.flashCancel() },
            title = { Text(stringResource(R.string.flash_start)) },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text(stringResource(R.string.flash_confirm_text, p.image.label, p.device))
                    for (w in p.warnings) Text(w, color = MaterialTheme.colorScheme.error)
                }
            },
            confirmButton = { TextButton(onClick = { vm.flashStart() }) { Text(stringResource(R.string.flash_start)) } },
            dismissButton = { TextButton(onClick = { vm.flashCancel() }) { Text(stringResource(R.string.cancel)) } },
        )
    }
    if (flashNoStop) {
        AlertDialog(
            onDismissRequest = { flashNoStop = false },
            title = { Text(stringResource(R.string.back_flash_title)) },
            text = { Text(stringResource(R.string.back_flash_text)) },
            confirmButton = { TextButton(onClick = { flashNoStop = false }) { Text(stringResource(R.string.ok)) } },
        )
    }
}

@Composable
private fun LanguageMenu(enabled: Boolean, setLang: (String) -> Unit) {
    val ctx = LocalContext.current
    var open by remember { mutableStateOf(false) }
    IconButton(onClick = { open = true }, enabled = enabled) { Icon(Icons.Default.MoreVert, stringResource(R.string.menu)) }
    DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
        val now = Lang.current(ctx)
        // each language in its own words
        for ((code, name) in listOf("bg" to "Български", "en" to "English")) {
            DropdownMenuItem(
                text = { Text(name) },
                leadingIcon = { if (code == now) Icon(Icons.Default.Check, null) },
                onClick = { open = false; if (code != now) setLang(code) },
            )
        }
    }
}

/** Progress and the last result, always in sight at the bottom (a long list cannot hide them). */
@Composable
private fun BottomStatus(vm: AppViewModel) {
    val showBusy = vm.busy && !vm.running
    // the login sheet shows them itself, not twice
    if (vm.step == Step.REPEATER && vm.loginSheet != null) return
    // the restore's button stays in sight however long the list of differences
    val restoreBar = vm.step == Step.BACKUP && vm.restoreFrom != null && vm.restoreRows.isNotEmpty()
    if (!showBusy && vm.message.isEmpty() && !restoreBar) return
    Surface(tonalElevation = 3.dp, modifier = Modifier.navigationBarsPadding()) {
        Column(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 10.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            if (showBusy) Busy(vm)
            Message(vm)
            if (restoreBar) RestoreBar(vm)
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

// ---- repeaters ----

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun RepeaterStep(vm: AppViewModel) {
    var query by rememberSaveable { mutableStateOf("") }

    CompanionCard(vm)
    // a queue cut short: it goes on from where it was
    val q = vm.queue
    if (q != null && !q.finished) {
        InfoCard(stringResource(R.string.q_unfinished, q.label, q.entries.count { it.result != io.github.n3tk0.loraota.ota.QueueResult.PENDING }, q.entries.size))
    }
    if (vm.companionOta) OutlinedButton(onClick = { vm.openQueue() }, enabled = !vm.busy) { Text(stringResource(R.string.q_open)) }
    OutlinedTextField(
        value = query, onValueChange = { query = it }, singleLine = true,
        leadingIcon = { Icon(Icons.Default.Search, null) },
        placeholder = { Text(stringResource(R.string.search_repeater)) },
        modifier = Modifier.fillMaxWidth(),
    )
    if (vm.repeaters.isEmpty()) Text(stringResource(R.string.no_repeaters))
    val shown = vm.repeaters.filter { query.isBlank() || it.name.contains(query.trim(), ignoreCase = true) }
    // next to you first: only those can be updated over LoRa
    val groups = listOf(
        R.string.section_direct to shown.filter { it.hops == 0 },
        R.string.section_hops to shown.filter { (it.hops ?: 0) > 0 },
        R.string.section_unknown to shown.filter { it.hops == null },
    )
    for ((title, list) in groups) {
        if (list.isEmpty()) continue
        Text(stringResource(title), style = MaterialTheme.typography.titleSmall, color = MaterialTheme.colorScheme.primary)
        for (c in list) RepeaterRow(vm, c) { vm.message = ""; vm.loginSheet = c }
    }

    vm.loginSheet?.let { c ->
        val sheet = rememberModalBottomSheetState(skipPartiallyExpanded = true)
        ModalBottomSheet(onDismissRequest = { vm.loginSheet = null }, sheetState = sheet) {
            LoginSheet(vm, c)
        }
    }
}

@Composable
private fun RepeaterRow(vm: AppViewModel, c: Contact, onClick: () -> Unit) {
    Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier
        .fillMaxWidth()
        .clickable(enabled = !vm.busy, onClick = onClick)
        .padding(vertical = 6.dp)) {
        Column(Modifier.weight(1f)) {
            Text(c.name, style = MaterialTheme.typography.bodyLarge)
            RouteText(c.hops)
        }
        if (vm.hasSavedPassword(c)) {
            Icon(Icons.Default.Lock, stringResource(R.string.password_saved), modifier = Modifier.size(18.dp),
                tint = MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
}

@Composable
private fun RouteText(hops: Int?) {
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

/** Name, route, password and Login together: the result shows here too, not under the list. */
@Composable
private fun LoginSheet(vm: AppViewModel, c: Contact) {
    var password by remember(c) { mutableStateOf(vm.savedPassword(c) ?: "") }
    Column(Modifier.padding(horizontal = 16.dp).padding(bottom = 24.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Text(c.name, style = MaterialTheme.typography.titleLarge)
        RouteText(c.hops)
        OutlinedTextField(
            value = password, onValueChange = { password = it },
            label = { Text(stringResource(R.string.admin_password)) },
            visualTransformation = PasswordVisualTransformation(),
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password),
            singleLine = true, modifier = Modifier.fillMaxWidth(), enabled = !vm.busy,
        )
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
            Button(onClick = { vm.login(c, password) }, enabled = !vm.busy) { Text(stringResource(R.string.login)) }
            if (vm.hasSavedPassword(c)) TextButton(onClick = { vm.forgetPassword(c); password = "" }, enabled = !vm.busy) {
                Text(stringResource(R.string.forget_password))
            }
        }
        if (vm.busy) Busy(vm)
        Message(vm)
    }
}

@Composable
private fun CompanionCard(vm: AppViewModel, withFlashButton: Boolean = true) {
    if (!withFlashButton && vm.companionModel.isEmpty()) return
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(2.dp)) {
            Text(stringResource(if (withFlashButton) R.string.companion else R.string.running_now),
                style = MaterialTheme.typography.labelMedium)
            if (vm.companionModel.isNotEmpty()) Text("${vm.companionModel} · ${vm.companionVersion}")
            if (!vm.companionOta) {
                Text(stringResource(R.string.companion_stock), style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
            if (withFlashButton) Row(Modifier.padding(top = 2.dp)) {
                TextButton(onClick = { vm.openFlash(FlashTarget.COMPANION) }, enabled = !vm.busy) { Text(stringResource(R.string.flash_open)) }
                TextButton(onClick = { vm.openBackup(DeviceKind.COMPANION) }, enabled = !vm.busy) { Text(stringResource(R.string.backup_open)) }
                TextButton(onClick = { vm.openHealth(DeviceKind.COMPANION) }, enabled = !vm.busy) { Text(stringResource(R.string.health_open)) }
            }
        }
    }
}

// ---- one repeater ----

@Composable
private fun RepeaterHome(vm: AppViewModel) {
    StatusCard(vm)
    vm.info?.let { RunningCard(it) }
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        OutlinedButton(onClick = { vm.openHealth(DeviceKind.REPEATER) }, enabled = !vm.busy) { Text(stringResource(R.string.health_open)) }
        OutlinedButton(onClick = { vm.openBackup(DeviceKind.REPEATER) }, enabled = !vm.busy) { Text(stringResource(R.string.backup_open)) }
    }
    TabRow(selectedTabIndex = vm.firmwareTab) {
        Tab(selected = vm.firmwareTab == 0, onClick = { vm.firmwareTab = 0 }, enabled = !vm.busy, text = { Text(stringResource(R.string.tab_lora)) })
        Tab(selected = vm.firmwareTab == 1, onClick = { vm.showFlashTab() }, enabled = !vm.busy, text = { Text(stringResource(R.string.tab_flash)) })
    }
    if (vm.firmwareTab == 0) LoraTab(vm) else FlashContent(vm)
}

@Composable
private fun RunningCard(info: OtaProtocol.Info) {
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
}

/** One condition for an update over LoRa: ✓ or ✗, and what to do about a ✗. */
@Composable
private fun ReadyRow(ok: Boolean, text: String, action: String? = null, enabled: Boolean = true, onAction: () -> Unit = {}) {
    Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth()) {
        Icon(if (ok) Icons.Default.Check else Icons.Default.Close, null, modifier = Modifier.size(20.dp),
            tint = if (ok) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant)
        Spacer(Modifier.width(8.dp))
        Text(text, modifier = Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium)
        if (!ok && action != null) TextButton(onClick = onAction, enabled = enabled) { Text(action) }
    }
}

@Composable
private fun LoraTab(vm: AppViewModel) {
    val info = vm.info
    var advanced by rememberSaveable { mutableStateOf(false) }
    val pickOld = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) vm.pickFile(uri, true)
    }
    val pickNew = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) vm.pickFile(uri, false)
    }

    if (info == null) {
        // a stock repeater (or the answer got lost): login and status only
        if (vm.busy && !vm.otaChecked) return
        ReadyRow(false, stringResource(R.string.ready_supported), stringResource(R.string.recheck), !vm.busy) { vm.recheck() }
        if (vm.otaChecked) {
            Hint(stringResource(R.string.no_ota_short), stringResource(R.string.no_ota))
            OutlinedButton(onClick = { vm.showFlashTab() }, enabled = !vm.busy) {
                Text(stringResource(R.string.tab_flash))
            }
        }
        return
    }

    Text(stringResource(R.string.ready_title), style = MaterialTheme.typography.titleSmall)
    ReadyRow(info.enabled, stringResource(R.string.ready_ota_on), stringResource(R.string.enable_ota), !vm.busy) { vm.enableOta() }
    val hops = vm.routeHops
    // short lines: the action next to a ✗ says what to do
    ReadyRow(vm.direct, when (hops) {
        null -> stringResource(R.string.ready_no_route)
        0 -> stringResource(R.string.ready_direct)
        else -> pluralStringResource(R.plurals.ready_hops, hops, hops)
    }, stringResource(R.string.recheck), !vm.busy) { vm.recheck() }
    ReadyRow(vm.companionOta, stringResource(R.string.ready_companion),
        stringResource(R.string.flash_open), !vm.busy) { vm.openFlash(FlashTarget.COMPANION) }
    ReadyRow(vm.oldImage != null, stringResource(R.string.ready_running),
        stringResource(R.string.pick_running_short), !vm.busy) { pickOld.launch(arrayOf("*/*")) }
    ReadyRow(vm.newImage != null, stringResource(R.string.ready_new))

    val continuing = info.state == OtaProtocol.STATE_PAUSED || vm.resumable
    // a message about the cut-short update already says what to do
    if (continuing && vm.message.isEmpty()) InfoCard(stringResource(R.string.paused_hint))

    Text(stringResource(R.string.new_firmware), style = MaterialTheme.typography.titleMedium)
    SourceRow(vm.loraSource, !vm.busy) { vm.loraSource = it }
    val candidates = vm.newCandidates()
    when (vm.loraSource) {
        Source.RELEASES -> {
            if (candidates.isEmpty()) Text(stringResource(R.string.no_new_firmware))
            for (f in candidates) {
                FirmwareRow(f, vm.newImage?.sha == f.sha256, !vm.busy, f.env.replace('_', ' ')) { vm.pickRelease(f) }
            }
        }
        Source.BUILD -> BuildPanel(vm)
        Source.FILE -> OutlinedButton(onClick = { pickNew.launch(arrayOf("*/*")) }, enabled = !vm.busy) { Text(stringResource(R.string.pick_new)) }
    }
    if (vm.newImage != null && candidates.none { it.sha256 == vm.newImage?.sha }) {
        Text(stringResource(R.string.chosen, vm.newImage!!.name))
    }
    if (vm.inPlace()) InPlaceWarning(detail = true)
    if (vm.nrfCopy()) CopyNote()

    val ready = vm.oldImage != null && vm.newImage != null && info.enabled && vm.direct && vm.companionOta
    Button(onClick = { vm.update() }, enabled = ready && !vm.busy) {
        Text(stringResource(if (continuing) R.string.resume else R.string.update))
    }
    if (vm.done) InfoCard(stringResource(R.string.done_hint))

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
                    vm.updateBackupNote?.let { Text(it, color = MaterialTheme.colorScheme.error) }
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
        Text(stringResource(R.string.image) + ": ${info.imageSize} B, SHA-256 ${hex(info.imageSha).take(12)}…",
            fontFamily = FontFamily.Monospace, style = MaterialTheme.typography.bodySmall)
        Text(stringResource(R.string.running_firmware), style = MaterialTheme.typography.titleSmall)
        Text(vm.oldImage?.name ?: "—", style = MaterialTheme.typography.bodySmall)
        OutlinedButton(onClick = { pickOld.launch(arrayOf("*/*")) }, enabled = !vm.busy) { Text(stringResource(R.string.pick_running)) }
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
        if (vm.ignoreAirLimit) DangerCard(stringResource(R.string.ignore_air_limit_warning))
        for (line in vm.log.takeLast(10)) Text(line, style = MaterialTheme.typography.bodySmall, fontFamily = FontFamily.Monospace)
    }
}

// ---- flash over Bluetooth / Wi-Fi ----

@Composable
private fun FlashContent(vm: AppViewModel) {
    val pick = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) vm.pickFlashFile(uri)
    }
    val toCompanion = vm.flashTarget == FlashTarget.COMPANION
    if (toCompanion) CompanionCard(vm, withFlashButton = false)
    Hint(
        stringResource(if (toCompanion) R.string.flash_hint_companion_short else R.string.flash_hint_repeater_short),
        stringResource(if (toCompanion) R.string.flash_hint_companion else R.string.flash_hint_repeater),
    )
    if (vm.flashDone) {
        Button(onClick = { vm.closeFlash() }) {
            Text(stringResource(if (toCompanion) R.string.reconnect else R.string.back_to_list))
        }
        return
    }
    if (vm.flashLog.isNotEmpty() && !vm.busy) FlashLogCard(vm.flashLog)
    SourceRow(vm.flashSource, !vm.busy) { vm.flashSource = it }
    when (vm.flashSource) {
        Source.RELEASES -> {
            // ours (with mailbox and LoRa OTA) or stock MeshCore
            SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth()) {
                for ((i, up) in listOf(false, true).withIndex()) {
                    SegmentedButton(selected = vm.flashUpstream == up, onClick = { vm.showUpstream(up) }, enabled = !vm.busy,
                        shape = SegmentedButtonDefaults.itemShape(i, 2)) {
                        Text(stringResource(if (up) R.string.src_upstream else R.string.src_ours))
                    }
                }
            }
            if (vm.flashUpstream) Text(stringResource(R.string.upstream_hint), style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant)
            val candidates = vm.flashCandidates()
            if (candidates.isEmpty()) Text(stringResource(R.string.flash_none))
            for (f in candidates) {
                FirmwareRow(f, vm.flashImage?.name == f.dfu && vm.flashImage?.env == f.env, !vm.busy,
                    f.env.replace('_', ' ') + if (f.dfuIsZip) " · Bluetooth" else " · Wi-Fi") { vm.pickFlashRelease(f) }
            }
        }
        Source.BUILD -> BuildPanel(vm)
        Source.FILE -> OutlinedButton(onClick = { pick.launch(arrayOf("*/*")) }, enabled = !vm.busy) {
            Text(stringResource(R.string.flash_from_file))
        }
    }
    vm.flashImage?.let { if (it.env == null) Text(stringResource(R.string.chosen, it.name)) }
    Button(onClick = { vm.flashCheck() }, enabled = vm.flashImage != null && !vm.busy) { Text(stringResource(R.string.flash_start)) }
}

/** Releases (and builds made before) / a new build on GitHub / a file. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun SourceRow(selected: Source, enabled: Boolean, onSelect: (Source) -> Unit) {
    val labels = listOf(R.string.src_releases, R.string.src_build, R.string.src_file)
    SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth()) {
        for ((i, src) in Source.entries.withIndex()) {
            SegmentedButton(selected = selected == src, onClick = { onSelect(src) }, enabled = enabled,
                shape = SegmentedButtonDefaults.itemShape(i, Source.entries.size)) {
                Text(stringResource(labels[i]), maxLines = 1)
            }
        }
    }
}

@Composable
private fun FirmwareRow(f: FirmwareFile, selected: Boolean, enabled: Boolean, detail: String, onPick: () -> Unit) {
    Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier
        .fillMaxWidth()
        .clickable(enabled = enabled, onClick = onPick)) {
        RadioButton(selected = selected, onClick = onPick, enabled = enabled)
        Column {
            Text(f.label, style = MaterialTheme.typography.titleMedium)
            Text(detail, style = MaterialTheme.typography.bodySmall)
        }
    }
}

// ---- a new build on GitHub ----

/** "Build Firmware for Boards" from the app: the token once, then the board and the parts, then its progress. */
@Composable
private fun BuildPanel(vm: AppViewModel) {
    if (!vm.hasToken) {
        TokenForm(vm)
        return
    }
    vm.buildJob?.let { BuildCard(vm) }
    LaunchedEffect(vm.buildRole, vm.repeater, vm.oldImage, vm.companionModel) { vm.loadBuildForm() }
    if (vm.buildInputs.isEmpty()) {
        if (!vm.busy) OutlinedButton(onClick = { vm.reloadBuildForm() }) { Text(stringResource(R.string.build_reload)) }
        return
    }
    var picking by remember { mutableStateOf(false) }
    Card(Modifier.fillMaxWidth()) {
        Row(Modifier.padding(12.dp), verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text(stringResource(R.string.build_board), style = MaterialTheme.typography.labelMedium)
                Text(vm.buildBoard.ifEmpty { stringResource(R.string.build_no_board) }.replace('_', ' '),
                    style = MaterialTheme.typography.titleMedium)
                if (vm.buildBoardAuto) Text(stringResource(R.string.build_board_auto), style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.primary)
            }
            TextButton(onClick = { picking = true }, enabled = !vm.busy) { Text(stringResource(R.string.build_board_pick)) }
        }
    }
    Text(stringResource(R.string.build_options), style = MaterialTheme.typography.titleSmall)
    for (input in vm.buildInputs.filter { it.isBoolean && !(vm.groupToken && it.name == GROUP_HIDDEN) }) {
        val on = vm.buildOptions[input.name] ?: input.defaultOn
        Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier
            .fillMaxWidth()
            .clickable(enabled = !vm.busy) { vm.buildOptions[input.name] = !on }) {
            Text(optionLabel(input), modifier = Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium)
            Switch(checked = on, onCheckedChange = { vm.buildOptions[input.name] = it }, enabled = !vm.busy)
        }
    }
    if (vm.buildRole == "companion" && vm.buildOptions["display"] == false) {
        Text(stringResource(R.string.build_note_no_display), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.error)
    }
    if (vm.buildRole == "repeater" && vm.buildOptions["lora_ota"] == false) {
        Text(stringResource(R.string.build_note_no_ota), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.error)
    }
    Button(onClick = { vm.startBuild() }, enabled = vm.buildBoard.isNotEmpty() && !vm.busy && vm.buildJob?.finished != false) {
        Text(stringResource(R.string.build_start))
    }
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        TextButton(onClick = { vm.reloadBuildForm() }, enabled = !vm.busy) { Text(stringResource(R.string.build_reload)) }
        if (!vm.groupToken) TextButton(onClick = { vm.forgetToken() }, enabled = !vm.busy) { Text(stringResource(R.string.build_token_forget)) }
    }
    if (vm.groupToken) Text(stringResource(R.string.build_token_group), style = MaterialTheme.typography.bodySmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant)
    if (picking) BoardPicker(vm.boardChoices(), vm.buildBoard, { picking = false }) { vm.pickBuildBoard(it); picking = false }
}

/** The app's words for the workflow's boxes it knows; the workflow's own for any other. */
@Composable
private fun optionLabel(input: WorkflowInput): String {
    val res = when (input.name) {
        "rtc" -> R.string.opt_rtc
        "climate" -> R.string.opt_climate
        "power_meters" -> R.string.opt_power_meters
        "other_sensors" -> R.string.opt_other_sensors
        "gps" -> R.string.opt_gps
        "display" -> R.string.opt_display
        "mailbox" -> R.string.opt_mailbox
        "lora_ota" -> R.string.opt_lora_ota
        "debug" -> R.string.opt_debug
        "release" -> R.string.opt_release
        else -> null
    }
    return if (res != null) stringResource(res) else input.description.ifEmpty { input.name }
}

@Composable
private fun TokenForm(vm: AppViewModel) {
    val uri = LocalUriHandler.current
    var token by remember { mutableStateOf("") }
    Hint(stringResource(R.string.build_token_short), stringResource(R.string.build_token_long, vm.repo))
    TextButton(onClick = { runCatching { uri.openUri(GitHubApi.NEW_TOKEN_URL) } }) { Text(stringResource(R.string.build_token_make)) }
    OutlinedTextField(
        value = token, onValueChange = { token = it },
        label = { Text(stringResource(R.string.build_token_field)) },
        visualTransformation = PasswordVisualTransformation(),
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password),
        singleLine = true, modifier = Modifier.fillMaxWidth(), enabled = !vm.busy,
    )
    Button(onClick = { vm.saveToken(token) }, enabled = token.isNotBlank() && !vm.busy) { Text(stringResource(R.string.build_token_save)) }
}

/** Where the build is: queued, the step it runs, done or why not; its page on GitHub. */
@Composable
private fun BuildCard(vm: AppViewModel) {
    val j = vm.buildJob ?: return
    val ctx = LocalContext.current
    val uri = LocalUriHandler.current
    val failed = j.phase == BuildPhase.FAILED
    Card(
        Modifier.fillMaxWidth(),
        colors = if (failed) CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.errorContainer,
            contentColor = MaterialTheme.colorScheme.onErrorContainer) else CardDefaults.cardColors(),
    ) {
        Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Text(if (j.runNumber > 0) stringResource(R.string.build_card_title, j.runNumber, j.board.replace('_', ' '))
                else stringResource(R.string.build_card_title_new, j.board.replace('_', ' ')), style = MaterialTheme.typography.titleMedium)
            Text(BuildService.phaseText(Lang.wrap(ctx), j), style = MaterialTheme.typography.bodyMedium)
            if (!j.finished) LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
            if (j.made > 0) Text(stringResource(R.string.build_made, j.made), style = MaterialTheme.typography.bodySmall)
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                if (j.htmlUrl.isNotEmpty()) TextButton(onClick = { runCatching { uri.openUri(j.htmlUrl) } }) {
                    Text(stringResource(R.string.build_open_run))
                }
                if (j.finished) TextButton(onClick = { vm.closeBuild() }) { Text(stringResource(R.string.build_close)) }
                else TextButton(onClick = { vm.forgetBuild() }) { Text(stringResource(R.string.build_stop_follow)) }
            }
        }
    }
}

/** The workflow's board list with a search: some 100 boards. */
@Composable
private fun BoardPicker(choices: List<String>, current: String, onDismiss: () -> Unit, onPick: (String) -> Unit) {
    var query by remember { mutableStateOf("") }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.build_board_pick)) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                OutlinedTextField(
                    value = query, onValueChange = { query = it }, singleLine = true,
                    leadingIcon = { Icon(Icons.Default.Search, null) },
                    placeholder = { Text(stringResource(R.string.build_board_search)) },
                    modifier = Modifier.fillMaxWidth(),
                )
                // "xiao nrf" finds Xiao_nrf52_repeater: each word on its own
                val words = query.lowercase().split(Regex("[\\s_]+")).filter { it.isNotEmpty() }
                val shown = choices.filter { c -> words.all { it in c.lowercase() } }
                Column(Modifier.height(360.dp).verticalScroll(rememberScrollState())) {
                    for (c in shown) {
                        Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier
                            .fillMaxWidth()
                            .clickable { onPick(c) }) {
                            RadioButton(selected = c == current, onClick = { onPick(c) })
                            Text(c.replace('_', ' '), style = MaterialTheme.typography.bodyMedium)
                        }
                    }
                }
            }
        },
        confirmButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.cancel)) } },
    )
}

/** While an update or a flash writes: only its progress (a stray tap cannot start anything else). */
@Composable
internal fun RunningView(vm: AppViewModel) {
    val name = if (vm.step == Step.FLASH) vm.companionName else vm.repeater?.name ?: ""
    val flashing = vm.flashPercent >= 0
    Text(stringResource(if (flashing) R.string.running_flash else R.string.running_update, name),
        style = MaterialTheme.typography.headlineSmall)
    if (flashing) {
        Text(stringResource(R.string.flash_progress, vm.flashPercent), style = MaterialTheme.typography.titleMedium)
        LinearProgressIndicator(progress = { vm.flashPercent / 100f }, modifier = Modifier.fillMaxWidth())
        vm.flashImage?.let { Text(it.label, style = MaterialTheme.typography.bodySmall) }
        if (vm.flashLog.isNotEmpty()) LogLines(vm.flashLog.takeLast(8))
    } else {
        vm.phase?.let { PhaseView(vm, it) }
        vm.newImage?.let { Text(it.name, style = MaterialTheme.typography.bodySmall) }
    }
    InfoCard(stringResource(R.string.power_note))
    // no Stop for a flash: a cut-short DFU leaves the device in its bootloader
    if (!flashing) OutlinedButton(onClick = { vm.stop() }) { Text(stringResource(R.string.stop)) }
}

/** The steps of the last Bluetooth flash: what went wrong where, to copy and send on. */
@Composable
private fun FlashLogCard(lines: List<String>) {
    val ctx = LocalContext.current
    var open by rememberSaveable { mutableStateOf(false) }
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text(stringResource(R.string.flash_log_title), style = MaterialTheme.typography.labelMedium)
            LogLines(if (open) lines else lines.takeLast(6))
            Row {
                TextButton(onClick = { open = !open }) { Text(stringResource(if (open) R.string.less else R.string.more)) }
                TextButton(onClick = {
                    val cm = ctx.getSystemService(android.content.Context.CLIPBOARD_SERVICE) as android.content.ClipboardManager
                    cm.setPrimaryClip(android.content.ClipData.newPlainText("flash log", lines.joinToString("\n")))
                }) { Text(stringResource(R.string.flash_log_copy)) }
            }
        }
    }
}

@Composable
private fun LogLines(lines: List<String>) {
    Text(lines.joinToString("\n"), style = MaterialTheme.typography.bodySmall, fontFamily = FontFamily.Monospace)
}

// ---- small parts ----

@Composable
private fun Busy(vm: AppViewModel) {
    LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
    if (vm.working.isNotEmpty()) Text(vm.working, style = MaterialTheme.typography.bodyMedium)
}

/** One sentence, the rest on 'More'. */
@Composable
internal fun Hint(short: String, long: String) {
    var open by rememberSaveable(long) { mutableStateOf(false) }
    Column {
        Text(if (open) long else short)
        TextButton(onClick = { open = !open }) { Text(stringResource(if (open) R.string.less else R.string.more)) }
    }
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
            val sb = vm.sensorBattery
            Text(stringResource(R.string.status_batt_uptime, sb?.volts ?: (s.battMv / 1000.0), duration(s.upTimeSecs)))
            if (sb != null) {
                val ma = Math.round(Math.abs(sb.amps) * 1000).toInt()
                Text(when {
                    !sb.signed -> stringResource(R.string.status_batt_current, ma)
                    sb.amps < 0 -> stringResource(R.string.status_batt_charging, ma)
                    else -> stringResource(R.string.status_batt_discharging, ma)
                }, style = MaterialTheme.typography.bodySmall)
            }
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

/** The last result: red only for an error. */
@Composable
private fun Message(vm: AppViewModel) {
    if (vm.message.isNotEmpty()) {
        Text(vm.message, color = if (vm.messageIsError) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurface)
    }
}

/** Information, not danger: a neutral card with an icon. */
@Composable
internal fun InfoCard(text: String) = NoteCard(text, Icons.Default.Info, danger = false)

/** Danger (power cuts, the air time law): red. */
@Composable
internal fun DangerCard(text: String) = NoteCard(text, Icons.Default.Warning, danger = true)

@Composable
private fun NoteCard(text: String, icon: ImageVector, danger: Boolean) {
    Card(
        Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(
            containerColor = if (danger) MaterialTheme.colorScheme.errorContainer else MaterialTheme.colorScheme.secondaryContainer,
            contentColor = if (danger) MaterialTheme.colorScheme.onErrorContainer else MaterialTheme.colorScheme.onSecondaryContainer,
        ),
    ) {
        Row(Modifier.padding(12.dp), verticalAlignment = Alignment.Top) {
            Icon(icon, null, modifier = Modifier.size(20.dp))
            Spacer(Modifier.width(10.dp))
            Text(text, style = MaterialTheme.typography.bodyMedium)
        }
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
