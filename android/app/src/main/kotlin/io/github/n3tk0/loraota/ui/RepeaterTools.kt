package io.github.n3tk0.loraota.ui

import android.Manifest
import android.annotation.SuppressLint
import android.content.Context
import android.content.pm.PackageManager
import android.location.Location
import android.location.LocationManager
import android.os.CancellationSignal
import android.os.Handler
import android.os.Looper
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
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
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.KeyboardArrowDown
import androidx.compose.material.icons.filled.KeyboardArrowUp
import androidx.compose.material.icons.filled.LocationOn
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.luminance
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import androidx.core.location.LocationManagerCompat
import io.github.n3tk0.loraota.R
import io.github.n3tk0.loraota.backup.DeviceKind
import io.github.n3tk0.loraota.ota.hex
import io.github.n3tk0.loraota.tools.LoraRadioCalculator
import io.github.n3tk0.loraota.tools.LoraRadioMetrics
import io.github.n3tk0.loraota.tools.MeshGeoTopology
import io.github.n3tk0.loraota.tools.NeighborEntry
import io.github.n3tk0.loraota.tools.PingStatsCalculator
import io.github.n3tk0.loraota.tools.RepeaterSettingsValidator
import io.github.n3tk0.loraota.tools.SettingValidationError
import java.util.Locale

/** Quick one-tap repeater maintenance actions on the Overview tab (including 5x Ping Link Test). */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun QuickActionsCard(vm: AppViewModel) {
    var confirmReboot by remember { mutableStateOf(false) }
    val pingSummary = remember(vm.pingSamples.size, vm.pingRunning) {
        if (vm.pingSamples.isEmpty()) null else PingStatsCalculator.summarize(vm.pingSamples)
    }

    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(stringResource(R.string.qa_title), style = MaterialTheme.typography.titleMedium)
            FlowRow(
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                OutlinedButton(onClick = { vm.syncRepeaterClock() }, enabled = !vm.busy) {
                    Text(stringResource(R.string.qa_clock_sync))
                }
                OutlinedButton(onClick = { vm.sendRepeaterAdvert(flood = true) }, enabled = !vm.busy) {
                    Text(stringResource(R.string.qa_advert))
                }
                OutlinedButton(onClick = { vm.runRepeaterPingTest(5) }, enabled = !vm.busy && !vm.pingRunning) {
                    Text(stringResource(R.string.ping_start))
                }
                OutlinedButton(onClick = { vm.recheck() }, enabled = !vm.busy) {
                    Text(stringResource(R.string.qa_refresh_status))
                }
                OutlinedButton(onClick = { confirmReboot = true }, enabled = !vm.busy) {
                    Text(stringResource(R.string.qa_reboot), color = MaterialTheme.colorScheme.error)
                }
            }
            vm.lastClockReply?.let { reply ->
                Text(
                    reply,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            if (pingSummary != null) {
                val rttText = if (pingSummary.minRttMs != null && pingSummary.avgRttMs != null && pingSummary.maxRttMs != null) {
                    "${pingSummary.minRttMs} / ${pingSummary.avgRttMs} / ${pingSummary.maxRttMs} ms"
                } else {
                    "—"
                }
                Surface(
                    color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.55f),
                    shape = MaterialTheme.shapes.small,
                    modifier = Modifier.fillMaxWidth(),
                ) {
                    Column(Modifier.padding(10.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                        Text(
                            stringResource(
                                R.string.ping_summary,
                                pingSummary.received,
                                pingSummary.sent,
                                pingSummary.successPercent,
                                rttText,
                            ),
                            style = MaterialTheme.typography.labelMedium,
                            color = if (pingSummary.received == pingSummary.sent) MaterialTheme.colorScheme.primary
                            else MaterialTheme.colorScheme.error,
                        )
                        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                            for (s in vm.pingSamples) {
                                val ok = s.ok
                                Surface(
                                    color = if (ok) MaterialTheme.colorScheme.primaryContainer else MaterialTheme.colorScheme.errorContainer,
                                    shape = MaterialTheme.shapes.extraSmall,
                                ) {
                                    Text(
                                        if (ok) "#${s.seq} ${s.rttMs}ms" else "#${s.seq} ×",
                                        modifier = Modifier.padding(horizontal = 6.dp, vertical = 2.dp),
                                        style = MaterialTheme.typography.labelSmall,
                                        fontFamily = FontFamily.Monospace,
                                        color = if (ok) MaterialTheme.colorScheme.onPrimaryContainer else MaterialTheme.colorScheme.onErrorContainer,
                                    )
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if (confirmReboot) {
        AlertDialog(
            onDismissRequest = { confirmReboot = false },
            title = { Text(stringResource(R.string.qa_reboot_title)) },
            text = { Text(stringResource(R.string.qa_reboot_text, vm.repeater?.name ?: "")) },
            confirmButton = {
                TextButton(onClick = {
                    confirmReboot = false
                    vm.rebootRepeater()
                }) {
                    Text(stringResource(R.string.qa_reboot), color = MaterialTheme.colorScheme.error)
                }
            },
            dismissButton = {
                TextButton(onClick = { confirmReboot = false }) { Text(stringResource(R.string.cancel)) }
            },
        )
    }
}

/** Expandable neighbors list on the Overview tab (loads via `neighbors` CLI; supports `neighbor.remove`). */
@Composable
fun NeighborsCard(vm: AppViewModel) {
    var expanded by vm.toolsState::neighborsExpanded
    var removeCandidate by remember { mutableStateOf<NeighborEntry?>(null) }
    val dark = MaterialTheme.colorScheme.background.luminance() < 0.5f

    LaunchedEffect(expanded, vm.neighborsLoaded, vm.busy, vm.neighborsError) {
        if (expanded && !vm.neighborsLoaded && vm.neighborsError == null && !vm.busy) {
            vm.loadNeighbors()
        }
    }

    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.fillMaxWidth()) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                modifier = Modifier
                    .fillMaxWidth()
                    .clickable { expanded = !expanded }
                    .padding(horizontal = 12.dp, vertical = 10.dp),
            ) {
                Column(Modifier.weight(1f)) {
                    Text(
                        if (vm.neighborsLoaded) "${stringResource(R.string.neighbors_title)} (${vm.neighbors.size})"
                        else stringResource(R.string.neighbors_title),
                        style = MaterialTheme.typography.titleMedium,
                    )
                    Text(
                        if (vm.neighborsLoaded) stringResource(R.string.neighbors_subtitle_loaded)
                        else stringResource(R.string.neighbors_subtitle_tap),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
                if (expanded) {
                    IconButton(onClick = { vm.loadNeighbors() }, enabled = !vm.busy) {
                        Icon(Icons.Default.Refresh, stringResource(R.string.refresh))
                    }
                }
                Icon(
                    if (expanded) Icons.Default.KeyboardArrowUp else Icons.Default.KeyboardArrowDown,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            if (expanded) {
                HorizontalDivider()
                Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    val err = vm.neighborsError
                    when {
                        err != null -> {
                            Row(verticalAlignment = Alignment.Top) {
                                Icon(Icons.Default.Warning, null, modifier = Modifier.size(18.dp),
                                    tint = MaterialTheme.colorScheme.error)
                                Spacer(Modifier.width(8.dp))
                                Text(err, style = MaterialTheme.typography.bodySmall, modifier = Modifier.weight(1f))
                                TextButton(onClick = { vm.clearNeighborsError() }, enabled = !vm.busy) {
                                    Text(stringResource(R.string.neighbors_retry))
                                }
                            }
                        }
                        !vm.neighborsLoaded -> {
                            Text(
                                stringResource(R.string.working_neighbors),
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                        vm.neighbors.isEmpty() -> {
                            Text(
                                stringResource(R.string.neighbors_none),
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                        else -> {
                            val coordsByPrefix = remember(vm.offlineBackups, vm.repeater?.name, vm.repeaterSettingsLoaded) {
                                val map = MeshGeoTopology.extractCoordsFromBackups(vm.offlineBackups).toMutableMap()
                                val r = vm.repeater
                                val lat = vm.repeaterSettings["lat"]?.replace(',', '.')?.toDoubleOrNull()
                                val lon = vm.repeaterSettings["lon"]?.replace(',', '.')?.toDoubleOrNull()
                                if (r != null && lat != null && lon != null && (lat != 0.0 || lon != 0.0)) {
                                    map[hex(r.publicKey).lowercase(Locale.ROOT)] = lat to lon
                                }
                                map
                            }
                            val selfCoords = remember(coordsByPrefix, vm.repeater) {
                                val selfKey = vm.repeater?.let { hex(it.publicKey).lowercase(Locale.ROOT) }
                                if (selfKey == null) null
                                else coordsByPrefix.entries.firstOrNull { (k, _) ->
                                    k.startsWith(selfKey) || selfKey.startsWith(k)
                                }?.value
                            }
                            for (n in vm.neighbors) {
                                val snrColor = when {
                                    n.snrDb >= 0f -> MaterialTheme.colorScheme.primary
                                    n.snrDb >= -8f -> if (dark) Color(0xFFFFB74D) else Color(0xFF9A5B00)
                                    else -> MaterialTheme.colorScheme.error
                                }
                                val geoVector = remember(selfCoords, coordsByPrefix, n.prefixHex) {
                                    val peerPrefix = n.prefixHex.lowercase(Locale.ROOT)
                                    val peerCoords = coordsByPrefix.entries.firstOrNull { (k, _) ->
                                        peerPrefix.isNotEmpty() && (k.startsWith(peerPrefix) || peerPrefix.startsWith(k))
                                    }?.value
                                    if (selfCoords != null && peerCoords != null) {
                                        MeshGeoTopology.distanceAndBearing(
                                            selfCoords.first,
                                            selfCoords.second,
                                            peerCoords.first,
                                            peerCoords.second,
                                        )
                                    } else null
                                }
                                Row(
                                    modifier = Modifier.fillMaxWidth(),
                                    verticalAlignment = Alignment.CenterVertically,
                                ) {
                                    Column(Modifier.weight(1f)) {
                                        Text(
                                            n.resolvedName ?: n.prefixHex.uppercase(Locale.ROOT),
                                            style = MaterialTheme.typography.bodyMedium,
                                        )
                                        val sub = buildString {
                                            if (n.resolvedName != null) append("${n.prefixHex} · ")
                                            append(formatSecsAgo(n.secsAgo))
                                            if (geoVector != null) {
                                                append(" · ")
                                                append(
                                                    String.format(
                                                        Locale.ROOT,
                                                        "%.1f km %s (%d°)",
                                                        geoVector.distanceKm,
                                                        geoVector.cardinal,
                                                        geoVector.bearingDeg,
                                                    ),
                                                )
                                            }
                                        }
                                        Text(
                                            sub,
                                            style = MaterialTheme.typography.bodySmall,
                                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                                        )
                                    }
                                    Surface(
                                        color = snrColor.copy(alpha = 0.14f),
                                        shape = MaterialTheme.shapes.small,
                                    ) {
                                        Text(
                                            String.format(Locale.ROOT, "SNR %+.1f dB", n.snrDb),
                                            modifier = Modifier.padding(horizontal = 8.dp, vertical = 3.dp),
                                            style = MaterialTheme.typography.labelSmall,
                                            color = snrColor,
                                        )
                                    }
                                    IconButton(
                                        onClick = { removeCandidate = n },
                                        enabled = !vm.busy,
                                        modifier = Modifier.size(34.dp),
                                    ) {
                                        Icon(
                                            Icons.Default.Delete,
                                            contentDescription = stringResource(R.string.neighbors_remove),
                                            modifier = Modifier.size(16.dp),
                                            tint = MaterialTheme.colorScheme.onSurfaceVariant,
                                        )
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    removeCandidate?.let { n ->
        val display = n.resolvedName ?: n.prefixHex.uppercase(Locale.ROOT)
        AlertDialog(
            onDismissRequest = { removeCandidate = null },
            title = { Text(stringResource(R.string.neighbors_remove_title)) },
            text = { Text(stringResource(R.string.neighbors_remove_body, display)) },
            confirmButton = {
                TextButton(onClick = {
                    removeCandidate = null
                    vm.removeNeighbor(n.prefixHex)
                }) { Text(stringResource(R.string.neighbors_remove), color = MaterialTheme.colorScheme.error) }
            },
            dismissButton = {
                TextButton(onClick = { removeCandidate = null }) { Text(stringResource(R.string.cancel)) }
            },
        )
    }
}

@Composable
private fun formatSecsAgo(secs: Long): String = when {
    secs < 0L || secs > 365L * 86400L -> stringResource(R.string.ago_unknown)
    secs < 5L -> stringResource(R.string.ago_just_now)
    secs < 60L -> stringResource(R.string.ago_secs, secs.toInt())
    secs < 3600L -> stringResource(R.string.ago_mins, (secs / 60L).toInt())
    secs < 86400L -> stringResource(R.string.ago_hours, (secs / 3600L).toInt())
    else -> stringResource(R.string.ago_days, (secs / 86400L).toInt())
}

/** Tools tab inside a logged-in repeater: CLI Console | Visual Settings | Backup/Restore. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun RepeaterToolsTab(vm: AppViewModel) {
    var toolSection by vm.toolsState::toolSection
    val sections = listOf(
        R.string.tools_cli,
        R.string.tools_settings,
        R.string.tools_backup_short,
    )

    SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth()) {
        for ((i, labelRes) in sections.withIndex()) {
            SegmentedButton(
                selected = toolSection == i,
                onClick = { toolSection = i },
                shape = SegmentedButtonDefaults.itemShape(i, sections.size),
                icon = {},
            ) {
                Text(stringResource(labelRes), maxLines = 1)
            }
        }
    }

    when (toolSection) {
        0 -> CliConsole(vm, isCompanion = false)
        1 -> RepeaterSettingsEditor(vm)
        2 -> {
            Card(Modifier.fillMaxWidth()) {
                Column(Modifier.padding(14.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    Text(stringResource(R.string.tools_backup_title), style = MaterialTheme.typography.titleMedium)
                    Text(
                        stringResource(R.string.tools_backup_desc),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                    Button(
                        onClick = { vm.openBackup(DeviceKind.REPEATER) },
                        enabled = !vm.busy,
                        modifier = Modifier.fillMaxWidth(),
                    ) {
                        Text(stringResource(R.string.backup_open))
                    }
                }
            }
        }
    }
}

private data class SettingFieldSpec(
    val key: String,
    val labelRes: Int,
    val isBool: Boolean = false,
    val keyboard: KeyboardType = KeyboardType.Text,
)

private val CORE_SETTING_SPECS = listOf(
    SettingFieldSpec("name", R.string.rset_name),
    SettingFieldSpec("owner.info", R.string.rset_owner),
    SettingFieldSpec("repeat", R.string.rset_repeat, isBool = true),
    SettingFieldSpec("tx", R.string.rset_tx, keyboard = KeyboardType.Number),
    SettingFieldSpec("advert.interval", R.string.rset_advert_int, keyboard = KeyboardType.Number),
    SettingFieldSpec("flood.advert.interval", R.string.rset_flood_advert_int, keyboard = KeyboardType.Number),
    SettingFieldSpec("lat", R.string.rset_lat, keyboard = KeyboardType.Decimal),
    SettingFieldSpec("lon", R.string.rset_lon, keyboard = KeyboardType.Decimal),
    SettingFieldSpec("guest.password", R.string.rset_guest_pw),
    SettingFieldSpec("allow.read.only", R.string.rset_allow_ro, isBool = true),
    SettingFieldSpec("multi.acks", R.string.rset_multi_acks, isBool = true),
    SettingFieldSpec("cad", R.string.rset_cad, isBool = true),
)

@Composable
private fun validationMessage(err: SettingValidationError): String = when (err) {
    SettingValidationError.NAME_EMPTY -> stringResource(R.string.rset_err_name_empty)
    SettingValidationError.NAME_TOO_LONG -> stringResource(R.string.rset_err_name_long)
    SettingValidationError.NAME_BAD_CHARS -> stringResource(R.string.rset_err_name_chars)
    SettingValidationError.TX_RANGE -> stringResource(R.string.rset_err_tx)
    SettingValidationError.LAT_RANGE -> stringResource(R.string.rset_err_lat)
    SettingValidationError.LON_RANGE -> stringResource(R.string.rset_err_lon)
    SettingValidationError.ADVERT_RANGE -> stringResource(R.string.rset_err_advert)
    SettingValidationError.FLOOD_ADVERT_RANGE -> stringResource(R.string.rset_err_flood_advert)
    SettingValidationError.AF_RANGE -> stringResource(R.string.rset_err_af)
}

@SuppressLint("MissingPermission")
private fun readLastKnownLocation(ctx: Context): Location? {
    val lm = ctx.getSystemService(Context.LOCATION_SERVICE) as? LocationManager ?: return null
    val providers = listOf(LocationManager.GPS_PROVIDER, LocationManager.NETWORK_PROVIDER, LocationManager.PASSIVE_PROVIDER)
    return providers.mapNotNull { p -> runCatching { lm.getLastKnownLocation(p) }.getOrNull() }
        .maxByOrNull { it.time }
}

private fun hasAnyLocationPermission(ctx: Context): Boolean =
    ContextCompat.checkSelfPermission(ctx, Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED ||
        ContextCompat.checkSelfPermission(ctx, Manifest.permission.ACCESS_COARSE_LOCATION) == PackageManager.PERMISSION_GRANTED

@SuppressLint("MissingPermission")
private fun requestPhoneLocation(
    ctx: Context,
    signal: CancellationSignal,
    onResult: (Location?) -> Unit,
) {
    val cached = readLastKnownLocation(ctx)
    val freshEnough = cached != null && (System.currentTimeMillis() - cached.time) < 120_000L && cached.accuracy <= 50f
    if (freshEnough) {
        onResult(cached)
        return
    }
    val lm = ctx.getSystemService(Context.LOCATION_SERVICE) as? LocationManager
    if (lm == null) {
        onResult(cached)
        return
    }
    val provider = listOf(LocationManager.GPS_PROVIDER, LocationManager.NETWORK_PROVIDER)
        .firstOrNull { p -> runCatching { lm.isProviderEnabled(p) }.getOrDefault(false) }
    if (provider == null) {
        onResult(cached)
        return
    }
    var settled = false
    val handler = Handler(Looper.getMainLooper())
    val timeout = Runnable {
        if (!settled && !signal.isCanceled) {
            settled = true
            signal.cancel()
            onResult(readLastKnownLocation(ctx) ?: cached)
        }
    }
    handler.postDelayed(timeout, 6_000L)
    runCatching {
        LocationManagerCompat.getCurrentLocation(
            lm,
            provider,
            signal,
            ContextCompat.getMainExecutor(ctx),
        ) { loc ->
            if (!settled && !signal.isCanceled) {
                settled = true
                handler.removeCallbacks(timeout)
                onResult(loc ?: readLastKnownLocation(ctx) ?: cached)
            }
        }
    }.onFailure {
        if (!settled && !signal.isCanceled) {
            settled = true
            handler.removeCallbacks(timeout)
            onResult(cached)
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun RepeaterSettingsEditor(vm: AppViewModel) {
    val ctx = LocalContext.current
    var showAdvanced by rememberSaveable { mutableStateOf(false) }
    var confirmRadioChange by remember { mutableStateOf(false) }
    var showSaveProfileDialog by remember { mutableStateOf(false) }
    var profileNameInput by rememberSaveable { mutableStateOf("") }
    var gpsLoading by remember { mutableStateOf(false) }
    var gpsSignal by remember { mutableStateOf<CancellationSignal?>(null) }

    DisposableEffect(Unit) {
        onDispose { gpsSignal?.cancel() }
    }

    fun applyPhoneGps() {
        gpsSignal?.cancel()
        val sig = CancellationSignal()
        gpsSignal = sig
        gpsLoading = true
        requestPhoneLocation(ctx, sig) { loc ->
            gpsLoading = false
            if (loc == null) {
                vm.message = ctx.getString(R.string.rset_gps_unavailable)
                vm.messageIsError = true
            } else {
                vm.updateRepeaterSettingDraft("lat", String.format(Locale.ROOT, "%.5f", loc.latitude))
                vm.updateRepeaterSettingDraft("lon", String.format(Locale.ROOT, "%.5f", loc.longitude))
                vm.message = ctx.getString(R.string.rset_gps_filled)
                vm.messageIsError = false
            }
        }
    }

    val gpsPermLauncher = rememberLauncherForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { results ->
        val granted = results[Manifest.permission.ACCESS_FINE_LOCATION] == true ||
            results[Manifest.permission.ACCESS_COARSE_LOCATION] == true ||
            hasAnyLocationPermission(ctx)
        if (granted) {
            applyPhoneGps()
        } else {
            vm.message = ctx.getString(R.string.rset_gps_denied)
            vm.messageIsError = true
        }
    }

    if (vm.canReadRepeater == false) {
        InfoCard(stringResource(R.string.health_no_capture))
        TempRadioCard(vm)
        return
    }

    // a read of the last half hour (also an edit made here) opens the editor with no request over the air
    LaunchedEffect(vm.repeaterSettingsLoaded, vm.busy) { vm.loadCachedSettings() }

    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth()) {
                Column(Modifier.weight(1f)) {
                    Text(stringResource(R.string.rset_title), style = MaterialTheme.typography.titleMedium)
                    Text(
                        stringResource(R.string.rset_subtitle),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
                OutlinedButton(onClick = { vm.loadRepeaterSettings(force = vm.repeaterSettingsLoaded) }, enabled = !vm.busy) {
                    Text(stringResource(if (vm.repeaterSettingsLoaded) R.string.refresh else R.string.rset_load))
                }
            }
            vm.settingsCachedMin?.let { min ->
                if (vm.repeaterSettingsLoaded) Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                    Text(stringResource(R.string.cached_settings, min), style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.weight(1f))
                    TextButton(onClick = { vm.loadRepeaterSettings(force = true) }, enabled = !vm.busy) {
                        Text(stringResource(R.string.cached_settings_reread))
                    }
                }
            }
            vm.repeaterSettingsProgress?.let { (cur, total) ->
                if (total > 0) {
                    LinearProgressIndicator(
                        progress = { cur.toFloat() / total.toFloat() },
                        modifier = Modifier.fillMaxWidth(),
                    )
                }
            }
        }
    }

    if (vm.repeaterSettingsLoaded) {
        val changed = vm.changedRepeaterSettings()
        val coreKeys = remember { CORE_SETTING_SPECS.map { it.key }.toSet() }
        val validationErrors = changed.mapNotNull { (k, v) ->
            RepeaterSettingsValidator.validate(k, v)?.let { k to it }
        }.toMap()

        // Config profiles / templates bar
        Card(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth()) {
                    Text(
                        stringResource(R.string.profile_title),
                        style = MaterialTheme.typography.titleSmall,
                        modifier = Modifier.weight(1f),
                    )
                    OutlinedButton(
                        onClick = {
                            profileNameInput = ""
                            showSaveProfileDialog = true
                        },
                        enabled = !vm.busy,
                    ) {
                        Text(stringResource(R.string.profile_save_current))
                    }
                }
                if (vm.configProfiles.isNotEmpty()) {
                    FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                        for (p in vm.configProfiles) {
                            Surface(
                                color = MaterialTheme.colorScheme.secondaryContainer,
                                shape = MaterialTheme.shapes.small,
                            ) {
                                Row(
                                    verticalAlignment = Alignment.CenterVertically,
                                    modifier = Modifier.padding(start = 10.dp, end = 4.dp, top = 2.dp, bottom = 2.dp),
                                ) {
                                    Text(
                                        "${p.name} (${p.settings.size})",
                                        style = MaterialTheme.typography.labelMedium,
                                        color = MaterialTheme.colorScheme.onSecondaryContainer,
                                        modifier = Modifier.clickable(enabled = !vm.busy) { vm.applyConfigProfile(p) },
                                    )
                                    IconButton(
                                        onClick = { vm.deleteConfigProfile(p.name) },
                                        enabled = !vm.busy,
                                        modifier = Modifier.size(24.dp),
                                    ) {
                                        Icon(
                                            Icons.Default.Delete,
                                            contentDescription = stringResource(R.string.backup_delete),
                                            modifier = Modifier.size(14.dp),
                                            tint = MaterialTheme.colorScheme.onSecondaryContainer,
                                        )
                                    }
                                }
                            }
                        }
                    }
                    TextButton(onClick = { vm.openProfileBatch(null) }, enabled = !vm.busy && vm.repeaters.isNotEmpty()) {
                        Text(stringResource(R.string.pbatch_open))
                    }
                } else {
                    Text(
                        stringResource(R.string.profile_empty),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            }
        }

        val radioDraft = vm.repeaterSettingsDraft["radio"]
        val draftRadioMetrics = remember(radioDraft) {
            radioDraft?.let { LoraRadioCalculator.computeFromRadioString(it) }
        }
        if (draftRadioMetrics != null) {
            LoraAirtimeBadge(draftRadioMetrics)
        }

        Card(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                for (spec in CORE_SETTING_SPECS) {
                    if (spec.key !in vm.repeaterSettings) continue
                    val current = vm.repeaterSettingsDraft[spec.key] ?: ""
                    val dirty = vm.repeaterSettings[spec.key] != current
                    val err = if (dirty) validationErrors[spec.key] else null
                    if (spec.isBool) {
                        val on = current.equals("on", ignoreCase = true) || current == "1" || current.equals("true", ignoreCase = true)
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            Column(Modifier.weight(1f)) {
                                Text(
                                    stringResource(spec.labelRes) + if (dirty) " *" else "",
                                    style = MaterialTheme.typography.bodyMedium,
                                    color = if (dirty) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurface,
                                )
                                Text(spec.key, style = MaterialTheme.typography.labelSmall,
                                    fontFamily = FontFamily.Monospace, color = MaterialTheme.colorScheme.onSurfaceVariant)
                            }
                            Switch(
                                checked = on,
                                onCheckedChange = { checked ->
                                    val orig = vm.repeaterSettings[spec.key] ?: ""
                                    val nextVal = if (orig == "0" || orig == "1") {
                                        if (checked) "1" else "0"
                                    } else {
                                        if (checked) "on" else "off"
                                    }
                                    vm.updateRepeaterSettingDraft(spec.key, nextVal)
                                },
                                enabled = !vm.busy,
                            )
                        }
                    } else {
                        OutlinedTextField(
                            value = current,
                            onValueChange = { vm.updateRepeaterSettingDraft(spec.key, it) },
                            label = {
                                Text(stringResource(spec.labelRes) + " (${spec.key})" + if (dirty) " *" else "")
                            },
                            isError = err != null,
                            supportingText = err?.let { e -> { Text(validationMessage(e)) } },
                            keyboardOptions = KeyboardOptions(keyboardType = spec.keyboard),
                            singleLine = true,
                            enabled = !vm.busy,
                            modifier = Modifier.fillMaxWidth(),
                        )
                    }
                    // Phone GPS button right after lon when lat & lon exist
                    if (spec.key == "lon" && "lat" in vm.repeaterSettings) {
                        OutlinedButton(
                            onClick = {
                                if (hasAnyLocationPermission(ctx)) {
                                    applyPhoneGps()
                                } else {
                                    gpsPermLauncher.launch(
                                        arrayOf(
                                            Manifest.permission.ACCESS_FINE_LOCATION,
                                            Manifest.permission.ACCESS_COARSE_LOCATION,
                                        ),
                                    )
                                }
                            },
                            enabled = !vm.busy && !gpsLoading,
                        ) {
                            Icon(Icons.Default.LocationOn, null, modifier = Modifier.size(16.dp))
                            Spacer(Modifier.width(6.dp))
                            Text(stringResource(if (gpsLoading) R.string.rset_gps_waiting else R.string.rset_use_phone_gps))
                        }
                    }
                }
            }
        }

        // Other loaded keys (radio, af, flood.max, delays, etc.)
        val otherKeys = vm.repeaterSettings.keys.filter { it !in coreKeys }.sorted()
        if (otherKeys.isNotEmpty()) {
            Card(Modifier.fillMaxWidth()) {
                Column(Modifier.fillMaxWidth()) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        modifier = Modifier
                            .fillMaxWidth()
                            .clickable { showAdvanced = !showAdvanced }
                            .padding(12.dp),
                    ) {
                        Text(
                            stringResource(R.string.rset_advanced, otherKeys.size),
                            style = MaterialTheme.typography.titleSmall,
                            modifier = Modifier.weight(1f),
                        )
                        Icon(
                            if (showAdvanced) Icons.Default.KeyboardArrowUp else Icons.Default.KeyboardArrowDown,
                            contentDescription = null,
                            tint = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                    if (showAdvanced) {
                        HorizontalDivider()
                        Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                            for (k in otherKeys) {
                                val current = vm.repeaterSettingsDraft[k] ?: ""
                                val dirty = vm.repeaterSettings[k] != current
                                val err = if (dirty) validationErrors[k] else null
                                OutlinedTextField(
                                    value = current,
                                    onValueChange = { vm.updateRepeaterSettingDraft(k, it) },
                                    label = { Text(if (dirty) "$k *" else k, fontFamily = FontFamily.Monospace) },
                                    isError = err != null,
                                    supportingText = err?.let { e -> { Text(validationMessage(e)) } },
                                    singleLine = true,
                                    enabled = !vm.busy,
                                    modifier = Modifier.fillMaxWidth(),
                                )
                            }
                        }
                    }
                }
            }
        }

        if (changed.isNotEmpty()) {
            Card(
                Modifier.fillMaxWidth(),
                colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.primaryContainer),
            ) {
                Row(
                    modifier = Modifier.fillMaxWidth().padding(12.dp),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Button(
                        onClick = {
                            if ("radio" in changed) confirmRadioChange = true
                            else vm.saveRepeaterSettings()
                        },
                        enabled = !vm.busy && validationErrors.isEmpty(),
                        modifier = Modifier.weight(1f),
                    ) {
                        Text(stringResource(R.string.rset_save_changes, changed.size))
                    }
                    TextButton(onClick = { vm.resetRepeaterSettingsDraft() }, enabled = !vm.busy) {
                        Text(stringResource(R.string.rset_reset))
                    }
                }
            }
        }

        if (confirmRadioChange) {
            AlertDialog(
                onDismissRequest = { confirmRadioChange = false },
                title = { Text(stringResource(R.string.rset_radio_confirm_title)) },
                text = { Text(stringResource(R.string.rset_radio_confirm_body)) },
                confirmButton = {
                    TextButton(onClick = {
                        confirmRadioChange = false
                        vm.saveRepeaterSettings()
                    }) {
                        Text(stringResource(R.string.rset_save_changes, changed.size), color = MaterialTheme.colorScheme.error)
                    }
                },
                dismissButton = {
                    TextButton(onClick = { confirmRadioChange = false }) { Text(stringResource(R.string.cancel)) }
                },
            )
        }

        if (showSaveProfileDialog) {
            AlertDialog(
                onDismissRequest = { showSaveProfileDialog = false },
                title = { Text(stringResource(R.string.profile_save_current)) },
                text = {
                    OutlinedTextField(
                        value = profileNameInput,
                        onValueChange = { profileNameInput = it },
                        label = { Text(stringResource(R.string.profile_name_hint)) },
                        singleLine = true,
                        modifier = Modifier.fillMaxWidth(),
                    )
                },
                confirmButton = {
                    TextButton(
                        onClick = {
                            vm.saveConfigProfile(profileNameInput)
                            showSaveProfileDialog = false
                        },
                        enabled = profileNameInput.trim().isNotEmpty(),
                    ) {
                        Text(stringResource(R.string.profile_save_current))
                    }
                },
                dismissButton = {
                    TextButton(onClick = { showSaveProfileDialog = false }) { Text(stringResource(R.string.cancel)) }
                },
            )
        }
    }

    TempRadioCard(vm)
}

@Composable
private fun LoraAirtimeBadge(m: LoraRadioMetrics) {
    Surface(
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.6f),
        shape = MaterialTheme.shapes.small,
        modifier = Modifier.fillMaxWidth(),
    ) {
        Column(Modifier.padding(10.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text(
                stringResource(R.string.lora_calc_title) + " · " +
                    String.format(Locale.ROOT, "%.3f MHz / %.1f kHz / SF%d / CR4/%d", m.freqMhz, m.bwKhz, m.sf, m.cr),
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.primary,
            )
            Text(
                stringResource(
                    R.string.lora_calc_metrics,
                    m.bitrateBps,
                    m.packet64AirMs,
                    m.sensitivityDbm,
                    m.estOta25KbMins,
                ),
                style = MaterialTheme.typography.bodySmall,
                fontFamily = FontFamily.Monospace,
            )
            if (m.exceeds869SubBand) {
                Text(
                    stringResource(R.string.lora_calc_warn_subband),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.error,
                )
            }
            if (m.slowForOta) {
                Text(
                    stringResource(R.string.lora_calc_warn_slow),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.error,
                )
            }
        }
    }
}

/** Collapsible safe temporary radio test (`tempradio`) card that auto-reverts after N minutes. */
@Composable
private fun TempRadioCard(vm: AppViewModel) {
    var expanded by rememberSaveable(vm.repeater?.name) { mutableStateOf(false) }
    var confirmApply by remember(vm.repeater?.name) { mutableStateOf(false) }
    var freq by rememberSaveable { mutableStateOf("869.525") }
    var bw by rememberSaveable { mutableStateOf("250") }
    var sf by rememberSaveable { mutableStateOf("7") }
    var cr by rememberSaveable { mutableStateOf("5") }
    var mins by rememberSaveable { mutableStateOf("5") }
    val cmd = RepeaterSettingsValidator.buildTempRadioCommand(freq, bw, sf, cr, mins)
    val metrics = remember(freq, bw, sf, cr) {
        val f = freq.trim().replace(',', '.').toDoubleOrNull()
        val b = bw.trim().replace(',', '.').toDoubleOrNull()
        val s = sf.trim().toIntOrNull()
        val c = cr.trim().toIntOrNull()
        if (f != null && b != null && s != null && c != null) LoraRadioCalculator.compute(f, b, s, c) else null
    }

    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.fillMaxWidth()) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                modifier = Modifier
                    .fillMaxWidth()
                    .clickable { expanded = !expanded }
                    .padding(12.dp),
            ) {
                Column(Modifier.weight(1f)) {
                    Text(stringResource(R.string.tempradio_title), style = MaterialTheme.typography.titleSmall)
                    Text(
                        stringResource(R.string.tempradio_subtitle),
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
                Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        OutlinedTextField(
                            value = freq, onValueChange = { freq = it },
                            label = { Text("Freq (MHz)") },
                            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal),
                            singleLine = true, enabled = !vm.busy, modifier = Modifier.weight(1f),
                        )
                        OutlinedTextField(
                            value = bw, onValueChange = { bw = it },
                            label = { Text("BW (kHz)") },
                            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal),
                            singleLine = true, enabled = !vm.busy, modifier = Modifier.weight(1f),
                        )
                    }
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        OutlinedTextField(
                            value = sf, onValueChange = { sf = it.filter(Char::isDigit).take(2) },
                            label = { Text("SF (5-12)") },
                            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                            singleLine = true, enabled = !vm.busy, modifier = Modifier.weight(1f),
                        )
                        OutlinedTextField(
                            value = cr, onValueChange = { cr = it.filter(Char::isDigit).take(1) },
                            label = { Text("CR (5-8)") },
                            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                            singleLine = true, enabled = !vm.busy, modifier = Modifier.weight(1f),
                        )
                        OutlinedTextField(
                            value = mins, onValueChange = { mins = it.filter(Char::isDigit).take(2) },
                            label = { Text(stringResource(R.string.tempradio_mins)) },
                            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                            singleLine = true, enabled = !vm.busy, modifier = Modifier.weight(1f),
                        )
                    }
                    if (metrics != null) {
                        LoraAirtimeBadge(metrics)
                    }
                    Button(
                        onClick = { if (cmd != null) confirmApply = true },
                        enabled = !vm.busy && cmd != null,
                        modifier = Modifier.fillMaxWidth(),
                    ) {
                        Text(stringResource(R.string.tempradio_apply))
                    }
                }
            }
        }
    }

    if (confirmApply && cmd != null) {
        AlertDialog(
            onDismissRequest = { confirmApply = false },
            title = { Text(stringResource(R.string.tempradio_confirm_title)) },
            text = { Text(stringResource(R.string.tempradio_confirm_body, cmd, mins.trim())) },
            confirmButton = {
                TextButton(onClick = {
                    confirmApply = false
                    vm.applyTempRadio(cmd)
                }) { Text(stringResource(R.string.tempradio_apply), color = MaterialTheme.colorScheme.error) }
            },
            dismissButton = {
                TextButton(onClick = { confirmApply = false }) { Text(stringResource(R.string.cancel)) }
            },
        )
    }
}

