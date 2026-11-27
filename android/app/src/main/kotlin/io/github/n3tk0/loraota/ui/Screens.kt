package io.github.n3tk0.loraota.ui

import android.annotation.SuppressLint
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.BoxWithConstraints
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
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.ArrowDropDown
import androidx.compose.material.icons.filled.Build
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Favorite
import androidx.compose.material.icons.filled.Home
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.KeyboardArrowDown
import androidx.compose.material.icons.filled.KeyboardArrowUp
import androidx.compose.material.icons.filled.Lock
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.Star
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
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
import androidx.compose.material3.pulltorefresh.PullToRefreshBox
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
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.luminance
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
import io.github.n3tk0.loraota.ThemeMode
import io.github.n3tk0.loraota.backup.DeviceKind
import io.github.n3tk0.loraota.ble.Contact
import io.github.n3tk0.loraota.github.BuildPhase
import io.github.n3tk0.loraota.github.BuildService
import io.github.n3tk0.loraota.github.GitHubApi
import io.github.n3tk0.loraota.github.WorkflowInput
import io.github.n3tk0.loraota.ota.FirmwareFile
import io.github.n3tk0.loraota.ota.HealthCheck
import io.github.n3tk0.loraota.ota.HealthSeverity
import io.github.n3tk0.loraota.ota.OtaProtocol
import io.github.n3tk0.loraota.ota.Phase
import io.github.n3tk0.loraota.ota.SelfTest
import io.github.n3tk0.loraota.ota.hex
import io.github.n3tk0.loraota.tools.FleetFilter
import io.github.n3tk0.loraota.tools.FleetFilterLogic
import kotlinx.coroutines.delay
import java.util.Locale

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AppScreen(
    vm: AppViewModel,
    themeMode: ThemeMode,
    setThemeMode: (ThemeMode) -> Unit,
    setLang: (String) -> Unit,
    dynamicColor: Boolean = false,
    setDynamicColor: (Boolean) -> Unit = {},
) {
    var askStop by remember { mutableStateOf(false) }      // Back during an update
    var flashNoStop by remember { mutableStateOf(false) }  // Back during a flash
    fun back() = when {
        vm.step == Step.SETTINGS && !vm.busy -> vm.closeSettings()
        vm.step == Step.OFFLINE && !vm.busy -> vm.closeOffline()
        vm.running && vm.flashPercent >= 0 -> flashNoStop = true
        vm.running -> askStop = true
        // an Add from the nearby screen finishes on its own: Back goes to the list, which it then refreshes
        vm.step == Step.NEARBY && vm.nearbyAddingKey != null -> vm.back()
        vm.canStop -> vm.stop()   // a login, a CLI command, a scan...: just stop waiting
        vm.busy -> {}             // a disconnect's clean-up: it ends on its own
        else -> vm.back()
    }
    BackHandler(enabled = vm.step != Step.COMPANION || vm.canStop) { back() }

    val showRepeaterNav = vm.step == Step.UPDATE && !vm.running
    val isRepeaterHealthTab = vm.step == Step.UPDATE && !vm.running && vm.repeaterTab == RepeaterTab.HEALTH

    Scaffold(
        modifier = Modifier.imePadding(),
        topBar = {
            TopAppBar(
                title = {
                    val titleText = when (vm.step) {
                        Step.COMPANION -> stringResource(R.string.app_name)
                        Step.REPEATER -> vm.companionName
                        Step.UPDATE -> vm.repeater?.name ?: ""
                        Step.FLASH -> stringResource(R.string.flash_title, vm.flashDeviceName)
                        Step.BACKUP -> stringResource(R.string.backup_title, vm.backupDeviceName)
                        Step.HEALTH -> stringResource(R.string.health_title, vm.healthDeviceName)
                        Step.QUEUE -> stringResource(R.string.q_title)
                        Step.SETTINGS -> stringResource(R.string.settings_title)
                        Step.CLI -> stringResource(R.string.cli_title_companion, vm.companionName)
                        Step.SWEEP -> stringResource(R.string.sweep_title)
                        Step.NEARBY -> stringResource(R.string.nearby_title)
                        Step.OFFLINE -> stringResource(R.string.offline_title)
                    }
                    if (showRepeaterNav && vm.repeaterTab != RepeaterTab.OVERVIEW) {
                        // the link next to the name instead of a row of its own; the name gives way first, then the numbers
                        BoxWithConstraints {
                            Row(verticalAlignment = Alignment.CenterVertically) {
                                Text(titleText, maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.weight(1f, fill = false))
                                Spacer(Modifier.width(8.dp))
                                LinkTitleIndicator(vm, showNumbers = this@BoxWithConstraints.maxWidth >= 220.dp)
                            }
                        }
                    } else {
                        Text(titleText, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    }
                },
                navigationIcon = {
                    if (vm.step != Step.COMPANION) IconButton(onClick = { back() }) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, stringResource(R.string.back))
                    }
                },
                actions = {
                    if (vm.step == Step.COMPANION) {
                        TextButton(onClick = { vm.openOffline() }, enabled = !vm.busy) {
                            Text(stringResource(R.string.offline_short))
                        }
                    }
                    if (vm.step == Step.REPEATER) IconButton(onClick = { vm.refreshRepeaters() }, enabled = !vm.busy) {
                        Icon(Icons.Default.Refresh, stringResource(R.string.refresh))
                    }
                    if (vm.step == Step.HEALTH || isRepeaterHealthTab) IconButton(onClick = { vm.refreshHealth() }, enabled = !vm.busy) {
                        Icon(Icons.Default.Refresh, stringResource(R.string.health_refresh))
                    }
                    if (vm.step == Step.OFFLINE) IconButton(onClick = { vm.loadOfflineData() }, enabled = !vm.busy && !vm.offlineLoading) {
                        Icon(Icons.Default.Refresh, stringResource(R.string.refresh))
                    }
                    if (vm.step != Step.SETTINGS) {
                        AppMenu(vm = vm, enabled = !vm.busy)
                    }
                },
            )
        },
        bottomBar = {
            Column {
                BottomStatus(vm, hasNavBarBelow = showRepeaterNav)
                if (showRepeaterNav) RepeaterBottomNav(vm)
            }
        },
    ) { pad ->
        // Keep PullToRefreshBox permanently mounted on pull-capable steps (never toggle on vm.busy),
        // otherwise changing vm.busy moves body() across composition slots and destroys screen state.
        val supportsPull = vm.step == Step.REPEATER || vm.step == Step.HEALTH || isRepeaterHealthTab || vm.step == Step.OFFLINE

        Surface(modifier = Modifier.fillMaxSize().padding(pad), color = MaterialTheme.colorScheme.background) {
            val body: @Composable () -> Unit = {
                Column(
                    modifier = Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
                    verticalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    if (vm.reconnecting) {
                        ReconnectingBanner(vm)
                    }
                    if ((vm.step == Step.COMPANION || vm.step == Step.REPEATER) && !vm.updateBannerDismissed) {
                        vm.availableUpdate?.let { rel ->
                            UpdateAvailableBanner(
                                version = rel.version,
                                enabled = !vm.busy,
                                onOpen = { vm.openSettings(checkNow = false) },
                                onDismiss = { vm.updateBannerDismissed = true },
                            )
                        }
                    }
                    when (vm.step) {
                        Step.COMPANION -> CompanionStep(vm)
                        Step.REPEATER -> RepeaterStep(vm)
                        Step.UPDATE -> if (vm.running) RunningView(vm) else RepeaterHome(vm)
                        Step.FLASH -> if (vm.running) RunningView(vm) else FlashContent(vm)
                        Step.BACKUP -> BackupScreen(vm)
                        Step.HEALTH -> HealthScreen(vm)
                        Step.QUEUE -> QueueScreen(vm)
                        Step.SETTINGS -> SettingsScreen(vm, themeMode, setThemeMode, setLang, dynamicColor, setDynamicColor)
                        Step.CLI -> CliConsole(vm, isCompanion = true)
                        Step.SWEEP -> SweepScreen(vm)
                        Step.NEARBY -> NearbyScreen(vm)
                        Step.OFFLINE -> OfflineScreen(vm)
                    }
                    Spacer(Modifier.height(24.dp))
                }
            }
            if (supportsPull) {
                val refreshing = when {
                    vm.step == Step.OFFLINE -> vm.offlineLoading
                    vm.step == Step.REPEATER || vm.step == Step.HEALTH || isRepeaterHealthTab ->
                        vm.busy && vm.loadingHistoryKey == null
                    else -> false
                }
                PullToRefreshBox(
                    isRefreshing = refreshing,
                    onRefresh = {
                        when {
                            vm.step == Step.OFFLINE && !vm.busy && !vm.offlineLoading -> vm.loadOfflineData()
                            vm.step == Step.REPEATER && !vm.busy -> vm.refreshRepeaters()
                            (vm.step == Step.HEALTH || isRepeaterHealthTab) && !vm.busy -> vm.refreshHealth()
                        }
                    },
                    modifier = Modifier.fillMaxSize(),
                ) {
                    body()
                }
            } else {
                body()
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
    ProfileBatchHost(vm)
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
private fun ReconnectingBanner(vm: AppViewModel) {
    Card(
        Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.secondaryContainer),
    ) {
        Row(
            Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            CircularProgressIndicator(Modifier.size(18.dp), strokeWidth = 2.dp)
            Text(
                stringResource(R.string.ble_reconnecting, vm.reconnectAttempt, 2),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSecondaryContainer,
                modifier = Modifier.weight(1f),
            )
            TextButton(onClick = { vm.cancelReconnect() }) {
                Text(stringResource(R.string.cancel))
            }
        }
    }
}

@Composable
private fun RepeaterBottomNav(vm: AppViewModel) {
    val tabs = listOf(
        Triple(RepeaterTab.OVERVIEW, R.string.rtab_overview, Icons.Default.Home),
        Triple(RepeaterTab.HEALTH, R.string.rtab_health, Icons.Default.Favorite),
        Triple(RepeaterTab.FIRMWARE, R.string.rtab_firmware, AppIcons.SystemUpdate),
        Triple(RepeaterTab.TOOLS, R.string.rtab_tools, AppIcons.Terminal),
    )
    NavigationBar {
        for ((tab, labelRes, icon) in tabs) {
            NavigationBarItem(
                selected = vm.repeaterTab == tab,
                onClick = { vm.selectRepeaterTab(tab) },
                enabled = (!vm.busy && !vm.reconnecting) || vm.repeaterTab == tab,
                icon = { Icon(icon, contentDescription = stringResource(labelRes)) },
                label = { Text(stringResource(labelRes), maxLines = 1, overflow = TextOverflow.Ellipsis) },
            )
        }
    }
}

@Composable
private fun AppMenu(vm: AppViewModel, enabled: Boolean) {
    var open by remember { mutableStateOf(false) }
    IconButton(onClick = { open = true }, enabled = enabled) {
        Icon(Icons.Default.MoreVert, stringResource(R.string.menu))
    }
    DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
        if (vm.step != Step.OFFLINE) {
            DropdownMenuItem(
                text = { Text(stringResource(R.string.offline_title)) },
                leadingIcon = { Icon(AppIcons.CloudOff, null) },
                onClick = { open = false; vm.openOffline() },
            )
        }
        DropdownMenuItem(
            text = { Text(stringResource(R.string.settings_title)) },
            leadingIcon = { Icon(Icons.Default.Settings, null) },
            onClick = { open = false; vm.openSettings(checkNow = false) },
        )
        DropdownMenuItem(
            text = {
                Text(
                    if (vm.availableUpdate != null) stringResource(R.string.menu_update_available, vm.availableUpdate!!.version)
                    else stringResource(R.string.settings_check_update),
                )
            },
            leadingIcon = { Icon(Icons.Default.Refresh, null) },
            onClick = { open = false; vm.openSettings(checkNow = true) },
        )
    }
}

@Composable
private fun UpdateAvailableBanner(version: String, enabled: Boolean, onOpen: () -> Unit, onDismiss: () -> Unit) {
    Card(
        Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.primaryContainer),
    ) {
        Row(
            Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(Modifier.weight(1f)) {
                Text(
                    stringResource(R.string.settings_update_available, version),
                    style = MaterialTheme.typography.titleSmall,
                    color = MaterialTheme.colorScheme.onPrimaryContainer,
                )
            }
            TextButton(onClick = onOpen, enabled = enabled) { Text(stringResource(R.string.update)) }
            IconButton(onClick = onDismiss, modifier = Modifier.size(32.dp)) {
                Icon(Icons.Default.Close, stringResource(R.string.build_close), modifier = Modifier.size(18.dp))
            }
        }
    }
}

/** Progress, last result, and primary sticky action button always in sight at the bottom. */
@Composable
private fun BottomStatus(vm: AppViewModel, hasNavBarBelow: Boolean = false) {
    val showBusy = vm.busy && !vm.running
    // the login sheet shows them itself, not twice
    if ((vm.step == Step.REPEATER || vm.step == Step.NEARBY) && vm.loginSheet != null) return
    // the restore's button stays in sight however long the list of differences
    val restoreBar = vm.step == Step.BACKUP && vm.restoreFrom != null && vm.restoreRows.isNotEmpty()
    val onFirmwareTab = vm.step == Step.UPDATE && !vm.running && vm.repeaterTab == RepeaterTab.FIRMWARE
    val loraInfo = vm.info?.takeIf {
        onFirmwareTab && vm.firmwareTab == 0 && (vm.loraSource != Source.BUILD || vm.newImage != null)
    }
    val flashBar = ((onFirmwareTab && vm.firmwareTab == 1) || vm.step == Step.FLASH) &&
        !vm.running && !vm.flashDone && (vm.flashSource != Source.BUILD || vm.flashImage != null)
    if (!showBusy && vm.message.isEmpty() && !restoreBar && loraInfo == null && !flashBar) return
    Surface(
        tonalElevation = 3.dp,
        modifier = if (hasNavBarBelow) Modifier else Modifier.navigationBarsPadding(),
    ) {
        Column(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 10.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            if (showBusy) Busy(vm)
            Message(vm)
            if (restoreBar) RestoreBar(vm)
            if (loraInfo != null) {
                val continuing = loraInfo.state == OtaProtocol.STATE_PAUSED || vm.resumable
                val ready = vm.oldImage != null && vm.newImage != null && loraInfo.enabled && vm.direct && vm.companionCanOta
                if (vm.companionStockSide) {
                    Text(stringResource(R.string.stock_side_warning), style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.error)
                }
                Button(onClick = { vm.update() }, enabled = ready && !vm.busy, modifier = Modifier.fillMaxWidth()) {
                    Text(stringResource(if (continuing) R.string.resume else R.string.update))
                }
            }
            if (flashBar) {
                Button(onClick = { vm.flashCheck() }, enabled = vm.flashImage != null && !vm.busy, modifier = Modifier.fillMaxWidth()) {
                    Text(stringResource(R.string.flash_start))
                }
            }
        }
    }
}

@SuppressLint("MissingPermission")
@Composable
private fun CompanionStep(vm: AppViewModel) {
    LaunchedEffect(Unit) {
        if (vm.devices.isEmpty() && !vm.scanning && !vm.busy) vm.scan()
        vm.loadKnownRepeaters()
    }
    Text(stringResource(R.string.companion_hint))
    val lastAddr = vm.lastCompanionAddress
    val lastName = vm.lastCompanionName
    if (lastAddr != null && !lastName.isNullOrEmpty() && vm.devices.none { it.address == lastAddr }) {
        Card(
            modifier = Modifier.fillMaxWidth().clickable(enabled = !vm.busy) { vm.connectByAddress(lastAddr) },
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.secondaryContainer),
        ) {
            Row(Modifier.padding(12.dp), verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text(stringResource(R.string.last_companion), style = MaterialTheme.typography.labelMedium,
                        color = MaterialTheme.colorScheme.primary)
                    Text(lastName, style = MaterialTheme.typography.titleMedium)
                    Text(lastAddr, style = MaterialTheme.typography.bodySmall)
                }
                Button(onClick = { vm.connectByAddress(lastAddr) }, enabled = !vm.busy) {
                    Text(stringResource(R.string.connect_now))
                }
            }
        }
    }
    KnownRepeatersSection(vm)
    Button(onClick = { vm.scan() }, enabled = !vm.scanning && !vm.busy) {
        Text(stringResource(if (vm.scanning) R.string.scanning else R.string.scan))
    }
    // in the order found while scanning (cards do not jump under the finger), by signal once it ends
    val shownDevices = if (vm.scanning) vm.devices.toList() else vm.devices.sortedByDescending { vm.deviceRssi[it.address] ?: -120 }
    if (!vm.scanning && !vm.busy && shownDevices.isEmpty()) {
        Text(
            stringResource(R.string.no_companions_found),
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
    for (d in shownDevices) {
        val rssi = vm.deviceRssi[d.address]
        val isLast = d.address == lastAddr
        Card(
            modifier = Modifier.fillMaxWidth().clickable(enabled = !vm.busy) { vm.connect(d) },
            colors = if (isLast) CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.primaryContainer) else CardDefaults.cardColors(),
        ) {
            Row(Modifier.padding(12.dp), verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    if (isLast) {
                        Text(stringResource(R.string.last_companion), style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.primary)
                    }
                    Text(d.name ?: "?", style = MaterialTheme.typography.titleMedium)
                    Text(d.address, style = MaterialTheme.typography.bodySmall)
                }
                if (rssi != null) {
                    Surface(
                        color = if (rssi >= -70) MaterialTheme.colorScheme.primary.copy(alpha = 0.15f)
                        else MaterialTheme.colorScheme.surfaceVariant,
                        shape = MaterialTheme.shapes.small,
                    ) {
                        Text(
                            stringResource(R.string.signal_dbm, rssi),
                            modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp),
                            style = MaterialTheme.typography.labelMedium,
                            color = if (rssi >= -70) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                }
            }
        }
    }
}

// ---- repeaters ----

@OptIn(ExperimentalMaterial3Api::class, ExperimentalLayoutApi::class)
@Composable
private fun RepeaterStep(vm: AppViewModel) {
    var query by rememberSaveable { mutableStateOf("") }
    var filter by rememberSaveable { mutableStateOf(FleetFilter.ALL) }

    CompanionCard(vm)
    // a queue cut short: it goes on from where it was
    val q = vm.queue
    if (q != null && !q.finished) {
        Card(
            Modifier.fillMaxWidth().clickable(enabled = !vm.busy) { vm.openQueue() },
            colors = CardDefaults.cardColors(
                containerColor = MaterialTheme.colorScheme.secondaryContainer,
                contentColor = MaterialTheme.colorScheme.onSecondaryContainer,
            ),
        ) {
            Row(Modifier.padding(12.dp), verticalAlignment = Alignment.Top) {
                Icon(Icons.Default.Info, null, modifier = Modifier.size(20.dp))
                Spacer(Modifier.width(10.dp))
                Text(
                    stringResource(
                        R.string.q_unfinished,
                        q.label,
                        q.entries.count { it.result != io.github.n3tk0.loraota.ota.QueueResult.PENDING },
                        q.entries.size,
                    ),
                    style = MaterialTheme.typography.bodyMedium,
                )
            }
        }
    }
    FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
        OutlinedButton(onClick = { vm.openNearby() }, enabled = !vm.busy) { Text(stringResource(R.string.nearby_open)) }
        if (vm.companionCanOta) {
            OutlinedButton(onClick = { vm.openQueue() }, enabled = !vm.busy) { Text(stringResource(R.string.q_open)) }
        }
        if (vm.repeaters.isNotEmpty()) {
            OutlinedButton(onClick = { vm.openSweep() }, enabled = !vm.busy) { Text(stringResource(R.string.sweep_open)) }
        }
    }
    if (vm.repeaters.size > 4 || query.isNotEmpty()) {
        OutlinedTextField(
            value = query, onValueChange = { query = it }, singleLine = true,
            leadingIcon = { Icon(Icons.Default.Search, null) },
            trailingIcon = {
                if (query.isNotEmpty()) IconButton(onClick = { query = "" }) { Icon(Icons.Default.Close, null) }
            },
            placeholder = { Text(stringResource(R.string.search_repeater)) },
            modifier = Modifier.fillMaxWidth(),
        )
    }
    if (vm.repeaters.size > 1) {
        val filters = listOf(
            FleetFilter.ALL to R.string.filter_all,
            FleetFilter.DIRECT to R.string.filter_direct,
            FleetFilter.ISSUES to R.string.filter_issues,
            FleetFilter.PINNED to R.string.filter_pinned,
        )
        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            for ((f, labelRes) in filters) {
                FilterChip(
                    selected = filter == f,
                    onClick = { filter = f },
                    label = { Text(stringResource(labelRes), style = MaterialTheme.typography.labelMedium) },
                )
            }
        }
    }
    if (vm.repeaters.isEmpty()) Text(stringResource(R.string.no_repeaters))
    val shown = vm.repeaters.filter { c ->
        (query.isBlank() || c.name.contains(query.trim(), ignoreCase = true)) &&
            FleetFilterLogic.matches(
                filter = filter,
                hops = c.hops,
                isPinned = vm.isPinned(c),
                latestHealthLine = vm.fleetHealth[hex(c.publicKey)]?.line,
            )
    }
    if (vm.repeaters.isNotEmpty() && shown.isEmpty()) {
        Text(
            stringResource(R.string.filter_empty),
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
    val pinned = shown.filter { vm.isPinned(it) }
    val unpinned = shown.filter { !vm.isPinned(it) }
    // pinned first, then direct (next to you: only those can be updated over LoRa), multi-hop, unknown
    val groups = listOf(
        R.string.section_pinned to pinned,
        R.string.section_direct to unpinned.filter { it.hops == 0 },
        R.string.section_hops to unpinned.filter { (it.hops ?: 0) > 0 },
        R.string.section_unknown to unpinned.filter { it.hops == null },
    )
    for ((title, list) in groups) {
        if (list.isEmpty()) continue
        Text(stringResource(title), style = MaterialTheme.typography.titleSmall, color = MaterialTheme.colorScheme.primary)
        for (c in list) {
            RepeaterRow(
                vm = vm,
                c = c,
                onClick = {
                    vm.message = ""
                    val saved = vm.savedPassword(c)
                    if (!saved.isNullOrEmpty()) vm.login(c, saved) else vm.loginSheet = c
                },
                onEditPassword = {
                    vm.message = ""
                    vm.loginSheet = c
                },
            )
        }
    }

    vm.loginSheet?.let { c ->
        val sheet = rememberModalBottomSheetState(skipPartiallyExpanded = true)
        ModalBottomSheet(onDismissRequest = { vm.loginSheet = null }, sheetState = sheet) {
            LoginSheet(vm, c)
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun RepeaterRow(vm: AppViewModel, c: Contact, onClick: () -> Unit, onEditPassword: () -> Unit) {
    val hasPw = vm.hasSavedPassword(c)
    val pinned = vm.isPinned(c)
    val healthRec = vm.fleetHealth[hex(c.publicKey)]
    val parsedHealth = remember(healthRec?.line) { healthRec?.let { SelfTest.parse(it.line) } }
    val dark = MaterialTheme.colorScheme.background.luminance() < 0.5f

    Card(
        modifier = Modifier.fillMaxWidth().clickable(enabled = !vm.busy, onClick = onClick),
    ) {
        Row(
            verticalAlignment = Alignment.CenterVertically,
            modifier = Modifier.fillMaxWidth().padding(start = 12.dp, end = 4.dp, top = 10.dp, bottom = 10.dp),
        ) {
            Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text(c.name, style = MaterialTheme.typography.titleMedium)
                FlowRow(
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalArrangement = Arrangement.spacedBy(4.dp),
                ) {
                    RouteBadge(c.hops)
                    if (vm.isUpdatePaused(c)) PausedBadge()
                    if (healthRec != null && parsedHealth != null) {
                        val problems = HealthCheck.problems(HealthCheck.warnings(parsedHealth, null))
                        val dotColor = when {
                            problems.any { it.severity == HealthSeverity.DANGER } -> MaterialTheme.colorScheme.error
                            problems.any { it.severity == HealthSeverity.WARN } || !parsedHealth.ok ->
                                if (dark) Color(0xFFFFB74D) else Color(0xFF9A5B00)
                            else -> MaterialTheme.colorScheme.primary
                        }
                        val ageMs = (System.currentTimeMillis() - healthRec.time).coerceAtLeast(0L)
                        val ageText = formatRelativeAge(ageMs)
                        val battText = parsedHealth.batteryPercent?.let { "$it%" }
                            ?: parsedHealth.batteryMv?.let { String.format(Locale.ROOT, "%.2fV", it / 1000.0) }
                        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                            Box(Modifier.size(8.dp).clip(CircleShape).background(dotColor))
                            Text(
                                listOfNotNull(battText, ageText).joinToString(" · "),
                                style = MaterialTheme.typography.labelSmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                    } else if (hasPw) {
                        Text(
                            stringResource(R.string.tap_to_login),
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                }
            }
            if (hasPw) {
                IconButton(onClick = onEditPassword, enabled = !vm.busy) {
                    Icon(Icons.Default.Lock, stringResource(R.string.change_password), modifier = Modifier.size(18.dp),
                        tint = MaterialTheme.colorScheme.onSurfaceVariant)
                }
            }
            IconButton(onClick = { vm.togglePin(c) }, enabled = !vm.busy) {
                Icon(
                    Icons.Default.Star,
                    stringResource(if (pinned) R.string.unpin_repeater else R.string.pin_repeater),
                    modifier = Modifier.size(20.dp),
                    tint = if (pinned) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.outlineVariant,
                )
            }
        }
    }
}

@Composable
private fun formatRelativeAge(ageMs: Long): String {
    val mins = (ageMs / 60_000L).toInt()
    val hours = (ageMs / 3_600_000L).toInt()
    val days = (ageMs / 86_400_000L).toInt()
    return when {
        mins < 1 -> stringResource(R.string.ago_just_now)
        mins < 60 -> stringResource(R.string.ago_mins, mins)
        hours < 24 -> stringResource(R.string.ago_hours, hours)
        else -> stringResource(R.string.ago_days, days)
    }
}

/** An update this repeater keeps (cut short earlier): from the app's own record, no radio traffic. */
@Composable
private fun PausedBadge() {
    Surface(color = MaterialTheme.colorScheme.tertiaryContainer, shape = MaterialTheme.shapes.small) {
        Text(
            stringResource(R.string.paused_badge),
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onTertiaryContainer,
            modifier = Modifier.padding(horizontal = 8.dp, vertical = 2.dp),
        )
    }
}

@Composable
private fun RouteBadge(hops: Int?) {
    val (bg, fg) = when (hops) {
        0 -> MaterialTheme.colorScheme.primaryContainer to MaterialTheme.colorScheme.onPrimaryContainer
        null -> MaterialTheme.colorScheme.surfaceVariant to MaterialTheme.colorScheme.onSurfaceVariant
        else -> MaterialTheme.colorScheme.secondaryContainer to MaterialTheme.colorScheme.onSecondaryContainer
    }
    Surface(color = bg, shape = MaterialTheme.shapes.small) {
        Text(
            when (hops) {
                null -> stringResource(R.string.route_unknown)
                0 -> stringResource(R.string.route_direct)
                else -> pluralStringResource(R.plurals.route_hops, hops, hops)
            },
            modifier = Modifier.padding(horizontal = 8.dp, vertical = 2.dp),
            style = MaterialTheme.typography.labelSmall,
            color = fg,
        )
    }
}

@Composable
private fun RouteText(hops: Int?) {
    RouteBadge(hops)
}

/** Name, route, password and Login together: the result shows here too, not under the list. */
@Composable
internal fun LoginSheet(vm: AppViewModel, c: Contact) {
    var password by remember(c) { mutableStateOf(vm.savedPassword(c) ?: "") }
    var showPassword by remember { mutableStateOf(false) }
    Column(Modifier.padding(horizontal = 16.dp).padding(bottom = 24.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Text(c.name, style = MaterialTheme.typography.titleLarge)
        RouteText(c.hops)
        OutlinedTextField(
            value = password, onValueChange = { password = it },
            label = { Text(stringResource(R.string.admin_password)) },
            visualTransformation = if (showPassword) androidx.compose.ui.text.input.VisualTransformation.None else PasswordVisualTransformation(),
            trailingIcon = {
                TextButton(onClick = { showPassword = !showPassword }) {
                    Text(stringResource(if (showPassword) R.string.hide_password else R.string.show_password),
                        style = MaterialTheme.typography.labelSmall)
                }
            },
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

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun CompanionCard(vm: AppViewModel, withFlashButton: Boolean = true) {
    if (!withFlashButton && vm.companionModel.isEmpty()) return
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(2.dp)) {
            Text(stringResource(if (withFlashButton) R.string.companion else R.string.running_now),
                style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.primary)
            if (vm.companionModel.isNotEmpty()) Text("${vm.companionModel} · ${vm.companionVersion}", style = MaterialTheme.typography.titleMedium)
            if (!vm.companionOta) {
                Text(stringResource(if (vm.companionStockSide) R.string.companion_stock_side else R.string.companion_stock), style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
            if (withFlashButton) FlowRow(
                modifier = Modifier.padding(top = 2.dp),
                horizontalArrangement = Arrangement.spacedBy(4.dp),
            ) {
                TextButton(onClick = { vm.openFlash(FlashTarget.COMPANION) }, enabled = !vm.busy) { Text(stringResource(R.string.flash_open)) }
                TextButton(onClick = { vm.openBackup(DeviceKind.COMPANION) }, enabled = !vm.busy) { Text(stringResource(R.string.backup_open)) }
                TextButton(onClick = { vm.openHealth(DeviceKind.COMPANION) }, enabled = !vm.busy) { Text(stringResource(R.string.health_open)) }
                TextButton(onClick = { vm.openCompanionCli() }, enabled = !vm.busy) { Text(stringResource(R.string.tools_cli)) }
            }
        }
    }
}

// ---- one repeater ----

@Composable
private fun ReloginBanner(vm: AppViewModel) {
    Card(
        Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.secondaryContainer),
    ) {
        Row(
            Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Text(
                stringResource(R.string.relogin_banner),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSecondaryContainer,
                modifier = Modifier.weight(1f),
            )
            TextButton(onClick = { vm.reloginCurrentRepeater() }, enabled = !vm.busy) {
                Text(stringResource(R.string.relogin_now))
            }
            IconButton(onClick = { vm.needsRelogin = false }, modifier = Modifier.size(28.dp)) {
                Icon(Icons.Default.Close, stringResource(R.string.build_close), modifier = Modifier.size(16.dp))
            }
        }
    }
}

@Composable
private fun RepeaterHome(vm: AppViewModel) {
    if (vm.needsRelogin) {
        ReloginBanner(vm)
    }
    when (vm.repeaterTab) {
        RepeaterTab.OVERVIEW -> {
            StatusCard(vm)
            LinkCard(vm)
            if (vm.pausedUpdate) PausedUpdateNote(vm)
            RunningNow(vm)
            QuickActionsCard(vm)
            NeighborsCard(vm)
        }
        RepeaterTab.HEALTH -> {
            HealthScreen(vm)
        }
        RepeaterTab.FIRMWARE -> {
            // the OTA check waits for this tab: once for a login (and 'Check again' when it did not get an answer)
            LaunchedEffect(vm.otaChecked, vm.otaTried, vm.busy, vm.step, vm.reconnecting) { vm.checkOtaOnce() }
            vm.info?.let { RunningCard(it) }
            TabRow(selectedTabIndex = vm.firmwareTab) {
                Tab(selected = vm.firmwareTab == 0, onClick = { vm.showLoraTab() }, enabled = !vm.busy, text = { Text(stringResource(R.string.tab_lora)) })
                Tab(selected = vm.firmwareTab == 1, onClick = { vm.showFlashTab() }, enabled = !vm.busy, text = { Text(stringResource(R.string.tab_flash)) })
            }
            if (vm.firmwareTab == 0) LoraTab(vm) else FlashContent(vm)
        }
        RepeaterTab.TOOLS -> {
            RepeaterToolsTab(vm)
        }
    }
}

/** What runs now: the live answer once the Firmware tab asked for it, until then the firmware of the last backup. */
@Composable
private fun RunningNow(vm: AppViewModel) {
    val info = vm.info
    val backup = vm.backupFirmware
    when {
        info != null -> RunningCard(info)
        backup != null -> RunningCard(null, backup.first, stringResource(R.string.running_from_backup))
    }
}

/** A LoRa update the app paused or stopped here: the repeater may still keep it; the Firmware tab goes on with it. */
@Composable
private fun PausedUpdateNote(vm: AppViewModel) {
    Card(
        Modifier.fillMaxWidth().clickable(enabled = !vm.busy) { vm.selectRepeaterTab(RepeaterTab.FIRMWARE) },
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.tertiaryContainer),
    ) {
        Row(Modifier.padding(horizontal = 12.dp, vertical = 8.dp), verticalAlignment = Alignment.CenterVertically) {
            Icon(Icons.Default.Warning, null, modifier = Modifier.size(18.dp), tint = MaterialTheme.colorScheme.onTertiaryContainer)
            Spacer(Modifier.width(8.dp))
            Text(stringResource(R.string.paused_update_note), style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onTertiaryContainer)
        }
    }
}

@Composable
private fun RunningCard(info: OtaProtocol.Info?, fallbackIdent: String = "", note: String? = null) {
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(stringResource(R.string.running_now), style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.primary, modifier = Modifier.weight(1f))
                if (info != null) Surface(color = MaterialTheme.colorScheme.secondaryContainer, shape = MaterialTheme.shapes.small) {
                    Text(
                        when (info.platform) {
                            OtaProtocol.PLATFORM_NRF52 -> "nRF52"
                            OtaProtocol.PLATFORM_ESP32 -> "ESP32"
                            else -> "?"
                        },
                        modifier = Modifier.padding(horizontal = 8.dp, vertical = 2.dp),
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSecondaryContainer,
                    )
                }
            }
            Text((info?.ident ?: fallbackIdent).replace('|', '\n'), style = MaterialTheme.typography.bodyMedium)
            if (note != null) Text(note, style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
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

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun LoraTab(vm: AppViewModel) {
    val info = vm.info
    var advanced by rememberSaveable { mutableStateOf(false) }
    var showAllReleases by rememberSaveable { mutableStateOf(false) }
    val pickOld = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) vm.pickFile(uri, true)
    }
    val pickNew = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) vm.pickFile(uri, false)
    }

    if (info == null) {
        // a stock repeater (or the answer got lost): login and status only
        if (vm.busy && !vm.otaChecked) return   // the check is on its way
        ReadyRow(false, stringResource(R.string.ready_supported), stringResource(R.string.recheck), !vm.busy) { vm.recheck() }
        if (vm.otaChecked) {
            Hint(stringResource(R.string.no_ota_short), stringResource(R.string.no_ota))
            OutlinedButton(onClick = { vm.showFlashTab() }, enabled = !vm.busy) {
                Text(stringResource(R.string.tab_flash))
            }
        }
        return
    }

    // Collapsible readiness card: stays collapsed when all prerequisites are green, auto-expands on transition to a problem
    val prereqsOk = info.enabled && vm.direct && vm.companionCanOta && vm.oldImage != null
    var readyExpanded by rememberSaveable(vm.repeater?.name) { mutableStateOf(!prereqsOk) }
    var lastPrereqsOk by rememberSaveable(vm.repeater?.name) { mutableStateOf(prereqsOk) }
    LaunchedEffect(prereqsOk) {
        if (prereqsOk != lastPrereqsOk) {
            readyExpanded = !prereqsOk
            lastPrereqsOk = prereqsOk
        }
    }
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.fillMaxWidth()) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                modifier = Modifier
                    .fillMaxWidth()
                    .clickable { readyExpanded = !readyExpanded }
                    .padding(horizontal = 12.dp, vertical = 10.dp),
            ) {
                Icon(
                    if (prereqsOk) Icons.Default.CheckCircle else Icons.Default.Warning,
                    contentDescription = null,
                    modifier = Modifier.size(20.dp),
                    tint = if (prereqsOk) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.error,
                )
                Spacer(Modifier.width(8.dp))
                Text(
                    stringResource(if (prereqsOk) R.string.ready_all_ok else R.string.ready_title),
                    style = MaterialTheme.typography.titleSmall,
                    color = if (prereqsOk) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurface,
                    modifier = Modifier.weight(1f),
                )
                Icon(
                    if (readyExpanded) Icons.Default.KeyboardArrowUp else Icons.Default.KeyboardArrowDown,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            if (readyExpanded) {
                HorizontalDivider()
                Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    ReadyRow(info.enabled, stringResource(R.string.ready_ota_on), stringResource(R.string.enable_ota), !vm.busy) { vm.enableOta() }
                    val hops = vm.routeHops
                    // short lines: the action next to a ✗ says what to do
                    ReadyRow(
                        vm.direct,
                        when (hops) {
                            null -> stringResource(R.string.ready_no_route)
                            0 -> stringResource(R.string.ready_direct)
                            else -> pluralStringResource(R.plurals.ready_hops, hops, hops)
                        },
                        stringResource(R.string.recheck),
                        !vm.busy,
                    ) { vm.recheck() }
                    ReadyRow(
                        vm.companionCanOta, stringResource(R.string.ready_companion),
                        stringResource(R.string.flash_open), !vm.busy,
                    ) { vm.openFlash(FlashTarget.COMPANION) }
                    ReadyRow(
                        vm.oldImage != null, stringResource(R.string.ready_running),
                        stringResource(R.string.pick_running_short), !vm.busy,
                    ) { pickOld.launch(arrayOf("*/*")) }
                    ReadyRow(vm.newImage != null, stringResource(R.string.ready_new))
                }
            }
        }
    }

    if (vm.runningUnknown && vm.oldImage == null) {
        // no release and no build has the image the repeater runs: no delta without the same .bin
        val sha8 = hex(info.imageSha).take(8)
        val commit = info.commit
        InfoCard(if (commit != null) stringResource(R.string.unknown_fw_commit, sha8, commit) else stringResource(R.string.unknown_fw, sha8))
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedButton(onClick = { vm.showFlashTab() }, enabled = !vm.busy) { Text(stringResource(R.string.unknown_fw_flash)) }
            OutlinedButton(onClick = { pickOld.launch(arrayOf("*/*")) }, enabled = !vm.busy) { Text(stringResource(R.string.unknown_fw_pick)) }
        }
    }

    if (vm.olderPage > 0) Text(stringResource(R.string.searching_older, vm.olderPage), style = MaterialTheme.typography.bodySmall)
    if (vm.runningNotChecked && vm.oldImage == null && vm.olderPage == 0) {
        // the search ended early (page cap or a failed fetch): the firmware may still be in an older release
        InfoCard(stringResource(R.string.unchecked_fw, hex(info.imageSha).take(8)))
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedButton(onClick = { vm.searchAgain() }, enabled = !vm.busy) { Text(stringResource(R.string.unchecked_fw_again)) }
            OutlinedButton(onClick = { vm.showFlashTab() }, enabled = !vm.busy) { Text(stringResource(R.string.unknown_fw_flash)) }
            OutlinedButton(onClick = { pickOld.launch(arrayOf("*/*")) }, enabled = !vm.busy) { Text(stringResource(R.string.unknown_fw_pick)) }
        }
    }

    val continuing = info.state == OtaProtocol.STATE_PAUSED || vm.resumable
    // a message about the cut-short update already says what to do
    if (continuing && vm.message.isEmpty()) InfoCard(stringResource(R.string.paused_hint))

    Text(stringResource(R.string.new_firmware), style = MaterialTheme.typography.titleMedium)
    SourceRow(vm.loraSource, !vm.busy) { vm.loraSource = it }
    val candidates = vm.newCandidates()
    when (vm.loraSource) {
        Source.RELEASES -> {
            ReleasesErrorCard(vm)
            if (candidates.isEmpty()) Text(stringResource(R.string.no_new_firmware))
            val best = vm.recommended()
            val ordered = if (best != null) listOf(best) + candidates.filter { it !== best } else candidates
            val visible = if (showAllReleases || ordered.size <= 4) {
                ordered
            } else {
                val top = ordered.take(4)
                val sel = ordered.firstOrNull { it.sha256 == vm.newImage?.sha }
                if (sel != null && sel !in top) top + sel else top
            }
            for (f in visible) {
                FirmwareRow(f, vm.newImage?.sha == f.sha256, !vm.busy, f.env.replace('_', ' '), f == best, showBoard = false) { vm.pickRelease(f) }
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
                if (ordered.size > 4) {
                    TextButton(onClick = { showAllReleases = !showAllReleases }) {
                        Text(
                            if (showAllReleases) stringResource(R.string.releases_show_less)
                            else stringResource(R.string.releases_show_older, ordered.size - visible.size),
                        )
                    }
                }
                TextButton(onClick = { vm.loadReleases() }, enabled = !vm.busy) { Text(stringResource(R.string.releases_refresh)) }
            }
            val hidden = vm.otherBoardCount()
            if (hidden > 0) Text(pluralStringResource(R.plurals.other_boards_hidden, hidden, hidden), style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        Source.BUILD -> BuildPanel(vm)
        Source.FILE -> OutlinedButton(onClick = { pickNew.launch(arrayOf("*/*")) }, enabled = !vm.busy) { Text(stringResource(R.string.pick_new)) }
    }
    if (vm.newImage != null && candidates.none { it.sha256 == vm.newImage?.sha }) {
        Text(stringResource(R.string.chosen, vm.newImage!!.name))
    }
    if (vm.calculatingPatch) {
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            LinearProgressIndicator(Modifier.weight(1f))
            Text(stringResource(R.string.delta_calculating), style = MaterialTheme.typography.bodySmall)
        }
    } else if (vm.patch != null) {
        val p = vm.patch!!
        val est = vm.estimateMinutes() ?: 0.0
        Card(
            Modifier.fillMaxWidth(),
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.primaryContainer),
        ) {
            Text(
                stringResource(R.string.delta_preview, p.size / 1024.0, est),
                modifier = Modifier.padding(12.dp),
                style = MaterialTheme.typography.titleSmall,
                color = MaterialTheme.colorScheme.onPrimaryContainer,
            )
        }
    }
    if (vm.inPlace()) InPlaceWarning(detail = true)
    if (vm.nrfCopy()) CopyNote()
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

        Text(stringResource(R.string.side_channel), style = MaterialTheme.typography.titleSmall)
        Text(stringResource(R.string.side_channel_hint), style = MaterialTheme.typography.bodySmall)
        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), modifier = Modifier.fillMaxWidth()) {
            for (p in AppViewModel.SIDE_PRESETS) {
                val selected = vm.sf.trim() == p.sf && vm.bwKhz.trim() == p.bw &&
                    (p.freq == null || vm.freqMhz.trim() == p.freq)
                val labelRes = p.label
                androidx.compose.material3.FilterChip(
                    selected = selected,
                    onClick = { vm.applySidePreset(p) },
                    enabled = !vm.busy,
                    label = { Text(stringResource(labelRes), style = MaterialTheme.typography.labelSmall) },
                )
            }
        }
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
    var filterQuery by rememberSaveable { mutableStateOf("") }
    var showAllFlash by rememberSaveable { mutableStateOf(false) }
    val pick = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) vm.pickFlashFile(uri)
    }
    val toCompanion = vm.flashTarget == FlashTarget.COMPANION
    if (toCompanion && vm.hasCompanionConnection && !vm.isStandaloneFlash) CompanionCard(vm, withFlashButton = false)
    Hint(
        stringResource(if (toCompanion) R.string.flash_hint_companion_short else R.string.flash_hint_repeater_short),
        stringResource(if (toCompanion) R.string.flash_hint_companion else R.string.flash_hint_repeater),
    )
    if (vm.flashDone) {
        Button(onClick = { vm.closeFlash() }) {
            Text(stringResource(if (toCompanion && vm.hasCompanionConnection && !vm.isStandaloneFlash) R.string.reconnect else R.string.back))
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
            ReleasesErrorCard(vm, upstream = vm.flashUpstream)
            val candidates = vm.flashCandidates()
            if (candidates.size > 4) {
                OutlinedTextField(
                    value = filterQuery, onValueChange = { filterQuery = it }, singleLine = true,
                    leadingIcon = { Icon(Icons.Default.Search, null) },
                    trailingIcon = {
                        if (filterQuery.isNotEmpty()) IconButton(onClick = { filterQuery = "" }) { Icon(Icons.Default.Close, null) }
                    },
                    placeholder = { Text(stringResource(R.string.search_firmware)) },
                    modifier = Modifier.fillMaxWidth(),
                )
            }
            val words = filterQuery.lowercase().split(Regex("[\\s_]+")).filter { it.isNotEmpty() }
            val shown = if (words.isEmpty()) candidates else candidates.filter { f ->
                val text = "${f.env} ${f.version} ${f.release} ${f.dfu} ${f.title} ${f.description} ${f.descriptionBg}".lowercase()
                words.all { it in text }
            }
            val visible = if (words.isNotEmpty() || showAllFlash || shown.size <= 5) {
                shown
            } else {
                val top = shown.take(5)
                val sel = shown.firstOrNull { vm.flashImage?.name == it.dfu && vm.flashImage?.env == it.env }
                if (sel != null && sel !in top) top + sel else top
            }
            if (visible.isEmpty()) Text(stringResource(R.string.flash_none))
            for (f in visible) {
                FirmwareRow(f, vm.flashImage?.name == f.dfu && vm.flashImage?.env == f.env, !vm.busy,
                    f.env.replace('_', ' ') + if (f.dfuIsZip) " · Bluetooth" else " · Wi-Fi") { vm.pickFlashRelease(f) }
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
                if (words.isEmpty() && shown.size > 5) {
                    TextButton(onClick = { showAllFlash = !showAllFlash }) {
                        Text(
                            if (showAllFlash) stringResource(R.string.releases_show_less)
                            else stringResource(R.string.releases_show_older, shown.size - visible.size),
                        )
                    }
                }
                TextButton(onClick = { if (vm.flashUpstream) vm.loadUpstream() else vm.loadReleases() }, enabled = !vm.busy) {
                    Text(stringResource(R.string.releases_refresh))
                }
            }
        }
        Source.BUILD -> BuildPanel(vm)
        Source.FILE -> OutlinedButton(onClick = { pick.launch(arrayOf("*/*")) }, enabled = !vm.busy) {
            Text(stringResource(R.string.flash_from_file))
        }
    }
    vm.flashImage?.let { img ->
        if (img.env == null || !vm.hasCompanionConnection || vm.isStandaloneFlash) {
            Text(
                stringResource(R.string.chosen, img.label),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.primary,
            )
        }
    }
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

/** The list of releases could not be fetched: the one kept on the phone is shown (maybe old); why, and Retry. */
@Composable
internal fun ReleasesErrorCard(vm: AppViewModel, upstream: Boolean = false) {
    val warn = if (upstream) null else vm.releasesWarning
    if (warn != null) Card(Modifier.fillMaxWidth(), colors = CardDefaults.cardColors(
        containerColor = MaterialTheme.colorScheme.secondaryContainer, contentColor = MaterialTheme.colorScheme.onSecondaryContainer)) {
        Text(stringResource(R.string.releases_manifest_failed, warn.first, warn.second), Modifier.padding(12.dp),
            style = MaterialTheme.typography.bodyMedium)
    }
    val why = (if (upstream) vm.upstreamError else vm.releasesError) ?: return
    Card(Modifier.fillMaxWidth(), colors = CardDefaults.cardColors(
        containerColor = MaterialTheme.colorScheme.errorContainer, contentColor = MaterialTheme.colorScheme.onErrorContainer)) {
        Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text(stringResource(R.string.releases_stale, why), style = MaterialTheme.typography.bodyMedium)
            TextButton(onClick = { if (upstream) vm.loadUpstream() else vm.loadReleases() }, enabled = !vm.busy) { Text(stringResource(R.string.releases_retry)) }
        }
    }
}

@Composable
private fun FirmwareRow(f: FirmwareFile, selected: Boolean, enabled: Boolean, detail: String, recommended: Boolean = false,
                        /** false where every row is the same board (LoRa OTA update): version and date stay the main line */
                        showBoard: Boolean = true, onPick: () -> Unit) {
    Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier
        .fillMaxWidth()
        .clickable(enabled = enabled, onClick = onPick)) {
        RadioButton(selected = selected, onClick = onPick, enabled = enabled)
        // the board's name (when the manifest has it) is the main line, the release moves to the detail line
        val lang = Lang.current(LocalContext.current)
        val description = if (showBoard) f.descriptionFor(lang) else ""
        val title = if (showBoard) f.title else ""
        Column(Modifier.weight(1f)) {
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                Text(title.ifEmpty { f.rowLabel() }, style = MaterialTheme.typography.titleMedium, modifier = Modifier.weight(1f, fill = false))
                if (recommended) {
                    Surface(
                        color = MaterialTheme.colorScheme.primaryContainer,
                        shape = MaterialTheme.shapes.small,
                    ) {
                        Text(
                            stringResource(R.string.recommended),
                            modifier = Modifier.padding(horizontal = 6.dp, vertical = 2.dp),
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onPrimaryContainer,
                        )
                    }
                }
            }
            Text(if (title.isEmpty()) detail else "${f.rowLabel()} · $detail",
                style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
            if (description.isNotEmpty()) {
                Text(description, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        }
    }
}

// ---- a new build on GitHub ----

/** "Build Firmware for Boards" from the app: the token once, then a collapsible options card, and progress card. */
@Composable
internal fun BuildPanel(vm: AppViewModel) {
    if (!vm.hasToken) {
        TokenForm(vm)
        return
    }
    val job = vm.buildJob
    val buildRunning = job != null && !job.finished
    var optionsExpanded by rememberSaveable { mutableStateOf(job == null) }
    var lastJobDispatchedAt by rememberSaveable { mutableStateOf(job?.dispatchedAt) }
    var lastJobFinished by rememberSaveable { mutableStateOf(job?.finished) }
    LaunchedEffect(job?.dispatchedAt, job?.finished) {
        val dispatched = job?.dispatchedAt
        val finished = job?.finished
        val startedNew = dispatched != null && dispatched != lastJobDispatchedAt
        val justFinished = dispatched != null && finished == true && lastJobFinished == false
        if (startedNew || justFinished) {
            optionsExpanded = false
        }
        lastJobDispatchedAt = dispatched
        lastJobFinished = finished
    }

    job?.let { BuildCard(vm) }
    LaunchedEffect(vm.buildRole, vm.repeater, vm.oldImage, vm.companionModel) { vm.loadBuildForm() }
    if (vm.buildInputs.isEmpty()) {
        if (!vm.busy) {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedButton(onClick = { vm.reloadBuildForm() }) { Text(stringResource(R.string.build_reload)) }
                if (!vm.groupToken) TextButton(onClick = { vm.forgetToken() }) { Text(stringResource(R.string.build_token_forget)) }
            }
        }
        return
    }

    var picking by remember { mutableStateOf(false) }
    val buildTitle = if (job != null && job.runNumber > 0) {
        stringResource(R.string.build_name_numbered, job.runNumber)
    } else {
        stringResource(R.string.src_build)
    }
    val boardSubtitle = (vm.buildBoard.ifEmpty { job?.board.orEmpty() })
        .ifEmpty { stringResource(R.string.build_no_board) }
        .replace('_', ' ')
    val controlsEnabled = !vm.busy && !buildRunning

    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.fillMaxWidth()) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                modifier = Modifier
                    .fillMaxWidth()
                    .clickable { optionsExpanded = !optionsExpanded }
                    .padding(12.dp),
            ) {
                Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                        Text(buildTitle, style = MaterialTheme.typography.titleMedium)
                        if (job != null) {
                            Surface(
                                color = MaterialTheme.colorScheme.secondaryContainer,
                                shape = MaterialTheme.shapes.small,
                            ) {
                                Text(
                                    stringResource(R.string.build_options_badge),
                                    modifier = Modifier.padding(horizontal = 6.dp, vertical = 2.dp),
                                    style = MaterialTheme.typography.labelSmall,
                                    color = MaterialTheme.colorScheme.onSecondaryContainer,
                                )
                            }
                        }
                    }
                    Text(
                        boardSubtitle,
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
                Icon(
                    if (optionsExpanded) Icons.Default.KeyboardArrowUp else Icons.Default.KeyboardArrowDown,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            if (optionsExpanded) {
                HorizontalDivider()
                Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth()) {
                        Column(Modifier.weight(1f)) {
                            Text(stringResource(R.string.build_board), style = MaterialTheme.typography.labelMedium)
                            Text(
                                vm.buildBoard.ifEmpty { stringResource(R.string.build_no_board) }.replace('_', ' '),
                                style = MaterialTheme.typography.titleMedium,
                            )
                            if (vm.buildBoardAuto) {
                                Text(
                                    stringResource(R.string.build_board_auto),
                                    style = MaterialTheme.typography.bodySmall,
                                    color = MaterialTheme.colorScheme.primary,
                                )
                            }
                        }
                        TextButton(onClick = { picking = true }, enabled = controlsEnabled) {
                            Text(stringResource(R.string.build_board_pick))
                        }
                    }
                    HorizontalDivider()
                    Text(stringResource(R.string.build_options), style = MaterialTheme.typography.titleSmall)
                    for (input in vm.buildInputs.filter { it.isBoolean && !(vm.groupToken && it.name == GROUP_HIDDEN) }) {
                        val on = vm.buildOptions[input.name] ?: input.defaultOn
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            modifier = Modifier
                                .fillMaxWidth()
                                .clickable(enabled = controlsEnabled) { vm.buildOptions[input.name] = !on },
                        ) {
                            Text(optionLabel(input), modifier = Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium)
                            Switch(checked = on, onCheckedChange = { vm.buildOptions[input.name] = it }, enabled = controlsEnabled)
                        }
                    }
                    if (vm.buildRole == "companion" && vm.buildOptions["display"] == false) {
                        Text(stringResource(R.string.build_note_no_display), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.error)
                    }
                    if (vm.buildRole == "repeater" && vm.buildOptions["lora_ota"] == false) {
                        Text(stringResource(R.string.build_note_no_ota), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.error)
                    }
                    val validBoard = vm.buildBoard.isNotEmpty() && vm.buildBoard in vm.boardChoices()
                    Button(
                        onClick = { vm.startBuild() },
                        enabled = validBoard && controlsEnabled,
                        modifier = Modifier.fillMaxWidth(),
                    ) {
                        Text(stringResource(R.string.build_start))
                    }
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        TextButton(onClick = { vm.reloadBuildForm() }, enabled = controlsEnabled) { Text(stringResource(R.string.build_reload)) }
                        if (!vm.groupToken) TextButton(onClick = { vm.forgetToken() }, enabled = controlsEnabled) { Text(stringResource(R.string.build_token_forget)) }
                    }
                    if (vm.groupToken) {
                        Text(
                            stringResource(R.string.build_token_group),
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                }
            }
        }
    }
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
internal fun TokenForm(vm: AppViewModel) {
    val uri = LocalUriHandler.current
    var token by rememberSaveable { mutableStateOf("") }
    var showToken by rememberSaveable { mutableStateOf(false) }
    Hint(stringResource(R.string.build_token_short), stringResource(R.string.build_token_long, vm.repo))
    TextButton(onClick = { runCatching { uri.openUri(GitHubApi.NEW_TOKEN_URL) } }) { Text(stringResource(R.string.build_token_make)) }
    OutlinedTextField(
        value = token, onValueChange = { token = it },
        label = { Text(stringResource(R.string.build_token_field)) },
        visualTransformation = if (showToken) androidx.compose.ui.text.input.VisualTransformation.None else PasswordVisualTransformation(),
        trailingIcon = {
            TextButton(onClick = { showToken = !showToken }) {
                Text(stringResource(if (showToken) R.string.hide_password else R.string.show_password),
                    style = MaterialTheme.typography.labelSmall)
            }
        },
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
            Text(
                if (j.runNumber > 0) stringResource(R.string.build_name_numbered, j.runNumber)
                else stringResource(R.string.src_build),
                style = MaterialTheme.typography.titleMedium,
            )
            Text(
                j.board.replace('_', ' '),
                style = MaterialTheme.typography.bodySmall,
                color = if (failed) MaterialTheme.colorScheme.onErrorContainer else MaterialTheme.colorScheme.onSurfaceVariant,
            )
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
    val name = if (vm.step == Step.FLASH) vm.flashDeviceName else vm.repeater?.name ?: ""
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
        if (vm.log.isNotEmpty()) {
            Card(Modifier.fillMaxWidth()) {
                Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    Text(stringResource(R.string.live_log), style = MaterialTheme.typography.labelMedium,
                        color = MaterialTheme.colorScheme.primary)
                    LogLines(vm.log.takeLast(8))
                }
            }
        }
    }
    InfoCard(stringResource(R.string.power_note))
    // no Stop for a flash: a cut-short DFU leaves the device in its bootloader
    if (!flashing) OutlinedButton(onClick = { vm.stop() }, modifier = Modifier.fillMaxWidth()) { Text(stringResource(R.string.stop)) }
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
    val sb = vm.sensorBattery
    val volts = sb?.volts ?: (s.battMv / 1000.0)
    val chem = vm.batteryChem
    val battPct = StatusFormat.fraction(volts, chem)
    val percent = StatusFormat.percent(volts, chem)
    var chemMenuOpen by remember { mutableStateOf(false) }
    val hops = vm.routeHops
    val routeLabel = when (hops) {
        null -> stringResource(R.string.status_route_unknown)
        0 -> stringResource(R.string.status_route_direct)
        else -> pluralStringResource(R.plurals.status_route_hops, hops, hops)
    }
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth()) {
                Box(Modifier.weight(1f)) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        modifier = Modifier.clickable { chemMenuOpen = true },
                    ) {
                        Text(
                            stringResource(R.string.status_battery) + " \u00b7 " + chem.label,
                            style = MaterialTheme.typography.titleSmall,
                        )
                        Icon(
                            Icons.Default.ArrowDropDown,
                            contentDescription = stringResource(R.string.battery_chem_pick),
                            modifier = Modifier.size(20.dp),
                        )
                    }
                    DropdownMenu(expanded = chemMenuOpen, onDismissRequest = { chemMenuOpen = false }) {
                        for (c in BatteryChem.entries) {
                            DropdownMenuItem(
                                text = { Text(c.label) },
                                leadingIcon = { if (c == chem) Icon(Icons.Default.Check, null) },
                                onClick = { chemMenuOpen = false; vm.chooseBatteryChem(c) },
                            )
                        }
                    }
                }
                Surface(
                    color = if (hops == 0) MaterialTheme.colorScheme.primaryContainer else MaterialTheme.colorScheme.secondaryContainer,
                    shape = MaterialTheme.shapes.small,
                ) {
                    Text(
                        routeLabel,
                        modifier = Modifier.padding(horizontal = 8.dp, vertical = 2.dp),
                        style = MaterialTheme.typography.labelSmall,
                        color = if (hops == 0) MaterialTheme.colorScheme.onPrimaryContainer else MaterialTheme.colorScheme.onSecondaryContainer,
                    )
                }
            }
            Row(verticalAlignment = Alignment.Bottom, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Text("%.2f V".format(volts), style = MaterialTheme.typography.headlineSmall, fontWeight = FontWeight.Bold)
                if (percent != null) {
                    Text("$percent%", style = MaterialTheme.typography.titleMedium, color = MaterialTheme.colorScheme.primary,
                        modifier = Modifier.padding(bottom = 2.dp))
                }
            }
            if (StatusFormat.inRange(volts, chem)) {
                LinearProgressIndicator(
                    progress = { battPct },
                    modifier = Modifier.fillMaxWidth().height(6.dp),
                    color = if (volts < chem.lowV) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.primary,
                )
            }
            if (sb != null) {
                val ma = Math.round(Math.abs(sb.amps) * 1000).toInt()
                Text(when {
                    !sb.signed -> stringResource(R.string.status_batt_current, ma)
                    sb.amps < 0 -> stringResource(R.string.status_batt_charging, ma)
                    else -> stringResource(R.string.status_batt_discharging, ma)
                }, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.primary)
            }
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                StatMetric(stringResource(R.string.status_uptime), duration(s.upTimeSecs), Modifier.weight(1f))
                StatMetric(stringResource(R.string.status_noise), stringResource(R.string.status_noise_value, s.noiseFloor), Modifier.weight(1f))
                StatMetric(stringResource(R.string.status_rx_tx), "${s.packetsRecv} / ${s.packetsSent}", Modifier.weight(1f))
            }
            Text(
                stringResource(R.string.status_airtime, duration(s.airTimeSecs)) +
                    (s.lastSnr?.let { " \u00b7 " + stringResource(R.string.status_snr, it) } ?: ""),
                style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}

@Composable
private fun StatMetric(label: String, value: String, modifier: Modifier = Modifier) {
    Column(modifier) {
        Text(label, style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        Text(value, style = MaterialTheme.typography.bodyMedium, fontWeight = FontWeight.Bold)
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
    val stepIdx = when (phase) {
        Phase.START -> 0
        Phase.SWITCH -> 1
        Phase.SEND, Phase.WAIT_AIR -> 2
        Phase.FINISH, Phase.RESTART -> 3
    }
    val steps = listOf(R.string.step_start, R.string.step_switch, R.string.step_send, R.string.step_verify)
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        for ((i, labelRes) in steps.withIndex()) {
            val done = i < stepIdx
            val current = i == stepIdx
            Surface(
                modifier = Modifier.weight(1f),
                color = when {
                    current -> MaterialTheme.colorScheme.primary
                    done -> MaterialTheme.colorScheme.primaryContainer
                    else -> MaterialTheme.colorScheme.surfaceVariant
                },
                shape = MaterialTheme.shapes.small,
            ) {
                Text(
                    "${i + 1}. ${stringResource(labelRes)}",
                    modifier = Modifier.padding(vertical = 6.dp, horizontal = 6.dp),
                    style = MaterialTheme.typography.labelSmall,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    color = when {
                        current -> MaterialTheme.colorScheme.onPrimary
                        done -> MaterialTheme.colorScheme.onPrimaryContainer
                        else -> MaterialTheme.colorScheme.onSurfaceVariant
                    },
                )
            }
        }
    }
    val secsLeft = ((vm.waitUntil - now).coerceAtLeast(0) / 1000).toInt()
    val minsLeft = (secsLeft + 59) / 60
    val percent = if (vm.total > 0) vm.sent * 100 / vm.total else 0
    val text = when (phase) {
        Phase.START -> stringResource(R.string.phase_start)
        Phase.SWITCH -> stringResource(R.string.phase_switch)
        Phase.SEND -> stringResource(R.string.phase_send, percent, vm.remainingMinutes())
        Phase.WAIT_AIR -> stringResource(R.string.phase_wait_air, minsLeft)
        Phase.FINISH -> stringResource(R.string.phase_finish)
        Phase.RESTART -> stringResource(R.string.phase_restart)
    }
    Text(text, style = MaterialTheme.typography.titleMedium)
    if (vm.total > 0 && phase != Phase.RESTART) {
        LinearProgressIndicator(progress = { vm.sent.toFloat() / vm.total }, modifier = Modifier.fillMaxWidth().height(8.dp))
        Text(stringResource(R.string.packets_progress, vm.sent, vm.total, percent),
            style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
    if (phase == Phase.WAIT_AIR && secsLeft > 0) {
        Text(
            stringResource(R.string.wait_air_countdown, secsLeft / 60, secsLeft % 60),
            style = MaterialTheme.typography.titleSmall,
            fontFamily = FontFamily.Monospace,
            color = MaterialTheme.colorScheme.primary,
        )
    }
}

@Composable
private fun NumberField(value: String, onChange: (String) -> Unit, label: String, modifier: Modifier, enabled: Boolean) {
    OutlinedTextField(
        value = value, onValueChange = onChange, label = { Text(label) }, singleLine = true, enabled = enabled,
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal), modifier = modifier.width(80.dp)
    )
}
