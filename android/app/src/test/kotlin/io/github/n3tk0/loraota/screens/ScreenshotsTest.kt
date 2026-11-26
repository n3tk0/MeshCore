package io.github.n3tk0.loraota.screens

import androidx.activity.result.ActivityResultRegistry
import androidx.activity.result.ActivityResultRegistryOwner
import androidx.activity.result.contract.ActivityResultContract
import androidx.activity.compose.LocalActivityResultRegistryOwner
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.MutableState
import androidx.core.app.ActivityOptionsCompat
import app.cash.paparazzi.DeviceConfig
import app.cash.paparazzi.Paparazzi
import io.github.n3tk0.loraota.DarkColors
import io.github.n3tk0.loraota.LightColors
import io.github.n3tk0.loraota.ThemeMode
import io.github.n3tk0.loraota.backup.Backup
import io.github.n3tk0.loraota.backup.BackupItem
import io.github.n3tk0.loraota.backup.BackupStore
import io.github.n3tk0.loraota.backup.DeviceKind
import io.github.n3tk0.loraota.ble.Contact
import io.github.n3tk0.loraota.ota.FirmwareFile
import io.github.n3tk0.loraota.ota.HealthStore
import io.github.n3tk0.loraota.ota.HealthWarning
import io.github.n3tk0.loraota.ota.OtaProtocol
import io.github.n3tk0.loraota.ota.LinkSample
import io.github.n3tk0.loraota.ota.LinkTrace
import io.github.n3tk0.loraota.ui.LinkRouteView
import io.github.n3tk0.loraota.ota.RepeaterStatus
import io.github.n3tk0.loraota.ota.RepeaterTelemetry
import io.github.n3tk0.loraota.tools.ConfigProfile
import io.github.n3tk0.loraota.tools.KnownRepeaters
import io.github.n3tk0.loraota.tools.NeighborEntry
import io.github.n3tk0.loraota.tools.PingSample
import io.github.n3tk0.loraota.ui.AppScreen
import io.github.n3tk0.loraota.ui.AppViewModel
import io.github.n3tk0.loraota.ui.BackupCompareContent
import io.github.n3tk0.loraota.ui.AppIcons
import io.github.n3tk0.loraota.ui.BatteryChem
import io.github.n3tk0.loraota.ui.CachedFirmwareItem
import io.github.n3tk0.loraota.ui.CliLine
import io.github.n3tk0.loraota.ui.CliLineKind
import io.github.n3tk0.loraota.ui.ProfileBatchContent
import io.github.n3tk0.loraota.ui.LinkState
import io.github.n3tk0.loraota.ui.RepeaterTab
import io.github.n3tk0.loraota.ui.Step
import io.github.n3tk0.loraota.ui.SweepEntry
import io.github.n3tk0.loraota.ui.SweepStatus
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.ui.unit.dp
import org.junit.Rule
import org.junit.Test
import java.io.File
import java.nio.file.Files

/**
 * Renders the real screens of the app (the real [AppViewModel], filled with made-up demo data) to PNG files
 * for the docs gallery. Run `./gradlew :app:recordPaparazziDebug`, then `tools/android-screens.sh`.
 * Every name below is invented; nothing here is data of a real repeater.
 */
class ScreenshotsTest {
    @get:Rule
    val paparazzi = Paparazzi(
        deviceConfig = DeviceConfig.PIXEL_5.copy(locale = "bg"),   // Bulgarian: the app's default language
        showSystemUi = false,
        useDeviceResolution = true,   // full 1080 px wide pictures (the default shrinks them to 1000 px high)
    )

    private val now = System.currentTimeMillis()
    private fun hoursAgo(h: Int) = now - h * 3_600_000L

    // ---- the demo fleet ----

    private class Demo(val name: String, val seed: Int, val hops: Int?) {
        val key = ByteArray(32) { (seed * 37 + it * 11 + 5).toByte() }
        val keyHex = key.joinToString("") { "%02x".format(it) }
    }

    private val r1 = Demo("Vitosha-Peak-R1", 1, 0)
    private val r2 = Demo("Plana-Solar-R2", 2, 0)
    private val r3 = Demo("Balkan-West-R3", 3, 2)
    private val r4 = Demo("Rila-North-R4", 4, 1)
    private val fleet = listOf(r1, r2, r3, r4)

    private fun contact(d: Demo): Contact {
        val frame = ByteArray(132)
        frame[0] = 3
        d.key.copyInto(frame, 1)
        frame[33] = 2   // a repeater
        frame[35] = (d.hops ?: 0xFF).toByte()
        d.name.toByteArray().copyInto(frame, 100)
        return Contact(frame)
    }

    private val radioMain = "869.525,250,11,5"

    private fun settings(d: Demo, radio: String = radioMain, lat: String = "42.5636", lon: String = "23.2750",
                         repeat: String = "on", allowRo: String = "on") = listOf(
        BackupItem(BackupItem.GROUP_SETTING, "name", "name", d.name),
        BackupItem(BackupItem.GROUP_SETTING, "radio", "radio", radio),
        BackupItem(BackupItem.GROUP_SETTING, "tx", "tx", "14"),
        BackupItem(BackupItem.GROUP_SETTING, "lat", "lat", lat),
        BackupItem(BackupItem.GROUP_SETTING, "lon", "lon", lon),
        BackupItem(BackupItem.GROUP_SETTING, "repeat", "repeat", repeat),
        BackupItem(BackupItem.GROUP_SETTING, "allow.read.only", "allow.read.only", allowRo),
        BackupItem(BackupItem.GROUP_SETTING, "guest.password", "guest.password", "guest"),
    )

    private fun backup(d: Demo, firmware: String, time: Long, items: List<BackupItem>) =
        Backup(DeviceKind.REPEATER, d.keyHex, d.name, time, false, firmware, items)

    private val fwBoard = "Xiao_nrf52_repeater"
    private val fwOld = "v1.2.1-b83d0e7"
    private val fwNew = "v1.3.0-4f2a9c1"

    private fun firmware(version: String, published: String) = FirmwareFile(
        release = version.substringBefore('-'), env = fwBoard, version = version,
        file = "$fwBoard-$version.bin", size = 412_160, sha256 = "a1".repeat(32), url = "",
        dfu = "$fwBoard-$version.zip", dfuSize = 455_680, dfuSha256 = "b2".repeat(32), dfuUrl = "",
        published = published,
    )

    private val fwFiles = listOf(firmware(fwNew, "2026-10-20T08:00:00Z"), firmware(fwOld, "2026-09-02T09:30:00Z"))

    private fun healthLine(mv: Int, pct: Int) =
        "ok radio=ok rx=120 tx=40 nf=-112 i2c=ok qspi=ok batt=${mv}mV/$pct% left=41h day=+420/-310 mail=3 ota=on ps=normal up=3d04h"

    // ---- building the view model ----

    private fun newVm(extraPrefs: Map<String, Map<String, Any?>> = emptyMap(), prepare: (File) -> Unit = {}): AppViewModel {
        val dir = Files.createTempDirectory("meshcore-shots").toFile().also { it.deleteOnExit() }
        val prefs = mapOf<String, Map<String, Any?>>(
            "settings" to mapOf("lang" to "bg", "auto_update_check" to false),   // no update check over the network
        ) + extraPrefs
        val app = FakeApp(paparazzi.context, dir, prefs)
        prepare(dir)
        return AppViewModel(app)
    }

    /** Sets a state of the view model that has a private setter. */
    @Suppress("UNCHECKED_CAST")
    private fun force(vm: AppViewModel, name: String, value: Any?) {
        val f = AppViewModel::class.java.getDeclaredField("$name\$delegate")
        f.isAccessible = true
        (f.get(vm) as MutableState<Any?>).value = value
    }

    private val registryOwner = object : ActivityResultRegistryOwner {
        override val activityResultRegistry = object : ActivityResultRegistry() {
            override fun <I, O> onLaunch(requestCode: Int, contract: ActivityResultContract<I, O>, input: I, options: ActivityOptionsCompat?) {}
        }
    }

    /** One phone width (1080 px), the height each screen needs to show its content. */
    private fun phone(heightPx: Int) =
        paparazzi.unsafeUpdateConfig(DeviceConfig.PIXEL_5.copy(locale = "bg", screenHeight = heightPx))

    private fun shot(dark: Boolean = false, content: @Composable () -> Unit) {
        paparazzi.snapshot {
            CompositionLocalProvider(LocalActivityResultRegistryOwner provides registryOwner) {
                MaterialTheme(colorScheme = if (dark) DarkColors else LightColors) { content() }
            }
        }
    }

    private fun app(vm: AppViewModel, dark: Boolean = false) = shot(dark) {
        AppScreen(vm = vm, themeMode = if (dark) ThemeMode.DARK else ThemeMode.LIGHT, setThemeMode = {}, setLang = {})
    }

    /** A repeater logged in, as the repeater tabs show it. */
    private fun loggedIn(vm: AppViewModel, d: Demo = r1, tab: RepeaterTab = RepeaterTab.OVERVIEW) {
        vm.companionName = "MeshCore-Companion"
        vm.repeaters.addAll(fleet.map(::contact))
        vm.repeater = contact(d)
        vm.step = Step.UPDATE
        vm.repeaterTab = tab
        vm.routeHops = d.hops
        vm.canReadRepeater = true
        vm.companionOta = true
        vm.status = RepeaterStatus(
            battMv = 3987, txQueue = 0, noiseFloor = -112, lastRssi = -78,
            packetsRecv = 18_452, packetsSent = 7_310, airTimeSecs = 5_400, upTimeSecs = 9 * 86_400L + 4 * 3_600L, lastSnr = 7.5,
        )
        vm.info = OtaProtocol.Info(
            platform = OtaProtocol.PLATFORM_NRF52, state = OtaProtocol.STATE_IDLE, flags = OtaProtocol.FLAG_ENABLED,
            imageSize = 412_160, imageSha = ByteArray(32), maxNew = 450_000, maxInPlace = 800_000,
            ident = "$fwBoard|$fwNew|2026-10-20",
        )
        vm.otaChecked = true
        // the link, as a trace and a minute of following measured it (fixed numbers: the picture stays the same)
        val outs = listOf(6.5, 7.0, 7.5, 6.0, 7.25, 8.0, 7.0, 6.75)
        val ins = listOf(3.0, 2.5, 4.0, 1.5, -0.5, 2.0, 2.75, 3.25)
        vm.linkSamples = outs.indices.map { LinkSample(1_782_000_000_000L + it * 10_000L, outs[it], ins[it]) }
        vm.linkOut = outs.last()
        vm.linkIn = ins.last()
        vm.linkTime = 1_782_000_070_000L
        vm.linkState = LinkState.OK
    }

    // ---- the screens ----

    private fun overview(dark: Boolean) {
        phone(3100)
        val vm = newVm()
        loggedIn(vm)
        // right after the login: the firmware is the last backup's until the Firmware tab asks the repeater
        vm.info = null
        vm.otaChecked = false
        vm.backupFirmware = "$fwBoard $fwNew 2026-10-20" to hoursAgo(30)
        vm.pausedUpdate = true
        vm.sensorBattery = RepeaterTelemetry.Battery(volts = 4.02, amps = -0.142, signed = true)   // solar charging
        for ((i, rtt) in listOf(412L, 388L, 455L, 401L, 379L).withIndex()) {
            vm.toolsState.pingSamples.add(PingSample(i + 1, true, rtt, ""))
        }
        vm.toolsState.lastClockReply = "Часовникът е синхронизиран"
        vm.toolsState.neighborsExpanded = true
        vm.toolsState.neighbors.addAll(
            listOf(
                NeighborEntry(r2.keyHex.take(8), 95, 9.5f, r2.name),
                NeighborEntry(r4.keyHex.take(8), 180, 3.0f, r4.name),
                NeighborEntry(r3.keyHex.take(8), 740, -7.5f, r3.name),
            ),
        )
        vm.toolsState.neighborsLoaded = true
        app(vm, dark)
    }

    @Test fun repeaterOverview() = overview(dark = false)
    @Test fun repeaterOverviewDark() = overview(dark = true)

    /** A repeater two hops away: the trace goes along the learned route and back, a direct check found no direct link. */
    @Test fun repeaterOverviewMultihop() {
        phone(2600)
        val vm = newVm()
        loggedIn(vm, r3)
        vm.linkSamples = emptyList()
        val route = LinkTrace.Route(listOf(LinkTrace.Hop(8.0, 6.5), LinkTrace.Hop(4.5, -2.0), LinkTrace.Hop(-3.0, 1.5)))
        vm.linkRoute = LinkRouteView(listOf(r4.name, r2.name, r3.name), route)
        vm.linkOut = route.weakestOut
        vm.linkIn = route.weakestIn
        vm.directState = LinkState.NO_REPLY
        app(vm)
    }

    @Test fun repeaterSettings() {
        phone(2700)
        val vm = newVm()
        loggedIn(vm, tab = RepeaterTab.TOOLS)
        vm.toolsState.toolSection = 1
        val s = mapOf(
            "name" to r1.name, "owner.info" to "MeshCore Sofia", "repeat" to "on", "tx" to "14",
            "advert.interval" to "240", "flood.advert.interval" to "12", "lat" to "42.5636", "lon" to "23.2750",
            "guest.password" to "", "allow.read.only" to "on", "multi.acks" to "on", "cad" to "on",
            "radio" to radioMain, "af" to "1.0", "flood.max" to "64",
        )
        vm.toolsState.settings.putAll(s)
        vm.toolsState.settingsDraft.putAll(s)
        vm.toolsState.settingsLoaded = true
        vm.toolsState.configProfiles.addAll(
            listOf(
                ConfigProfile("Планински (EU 869.525)", mapOf("radio" to radioMain, "tx" to "14", "repeat" to "on", "af" to "1.0", "flood.max" to "64")),
                ConfigProfile("Градски (малка мощност)", mapOf("radio" to radioMain, "tx" to "8", "repeat" to "on")),
            ),
        )
        app(vm)
    }

    @Test fun repeaterCli() {
        phone(2340)
        val vm = newVm()
        loggedIn(vm, tab = RepeaterTab.TOOLS)
        vm.toolsState.toolSection = 0
        vm.toolsState.customCliSnippets.add("get radio")
        val t = vm.toolsState
        var clock = 1_782_000_000_000L   // fixed, so the picture does not change from run to run
        fun line(text: String, kind: CliLineKind, ms: Long? = null) {
            clock += 4_000
            t.repeaterCliLog.add(CliLine(clock, text, kind, ms))
        }
        line("ver", CliLineKind.COMMAND)
        line("v1.3.0-4f2a9c1 (Build: 20 Oct 2026)", CliLineKind.REPLY, 640)
        line("get radio", CliLineKind.COMMAND)
        line("> $radioMain", CliLineKind.REPLY, 590)
        line("neighbors", CliLineKind.COMMAND)
        line("a1b2c3d4:95:38\nc4d5e6f7:180:12", CliLineKind.REPLY, 1_240)
        t.addCliHistory(false, "get tx")
        t.addCliHistory(false, "neighbors")
        t.repeaterCliInput = "set t"   // typing: the command suggestions show under the console
        app(vm)
    }

    @Test fun offlineFirmware() {
        phone(3100)
        val vm = newVm()
        vm.step = Step.OFFLINE
        force(vm, "offlineFocusTab", 2)
        vm.fleetState.offlineFirmwares.addAll(
            fwFiles.map { CachedFirmwareItem(it, hasCachedOta = true, hasCachedDfu = true) },
        )
        vm.cacheFileCount = 4
        vm.cacheBytes = 4 * 430_000L
        app(vm)
    }

    @Test fun offlineAudit() {
        phone(2340)
        val vm = newVm()
        vm.step = Step.OFFLINE
        force(vm, "offlineFocusTab", 3)
        val f = vm.fleetState
        f.offlineFirmwares.addAll(fwFiles.map { CachedFirmwareItem(it, hasCachedOta = true, hasCachedDfu = true) })
        f.offlineBackups.addAll(
            listOf(
                backup(r1, "$fwBoard|$fwOld", hoursAgo(30), settings(r1)),
                backup(r2, "$fwBoard|$fwNew", hoursAgo(50), settings(r2, lat = "42.5012", lon = "23.4890")),
                backup(r3, "$fwBoard|v1.1.0-0c1d2e3", hoursAgo(70), settings(r3, lat = "0", lon = "0", allowRo = "off")),
                backup(r4, "RAK_4631_repeater|v1.2.0-77aa11b", hoursAgo(90), settings(r4, radio = "868.0,125,9,5", lat = "42.2891", lon = "23.4012")),
            ),
        )
        f.offlineHealth[r1.keyHex] = listOf(HealthStore.Record(hoursAgo(2), healthLine(3987, 72)))
        f.offlineHealth[r2.keyHex] = listOf(HealthStore.Record(hoursAgo(3), healthLine(3480, 12)))
        for (d in fleet) f.knownDeviceNames[d.keyHex] = d.name
        f.observedNeighbors[r1.keyHex] = listOf(
            NeighborEntry(r2.keyHex.take(8), 60, 9.0f, r2.name),
            NeighborEntry(r4.keyHex.take(8), 200, 2.5f, r4.name),
        )
        f.observedNeighbors[r2.keyHex] = listOf(
            NeighborEntry(r1.keyHex.take(8), 80, 1.5f, r1.name),
            NeighborEntry(r4.keyHex.take(8), 120, 6.0f, r4.name),
        )
        f.observedNeighbors[r4.keyHex] = listOf(NeighborEntry(r1.keyHex.take(8), 300, 3.0f, r1.name))
        app(vm)
    }

    @Test fun fleetSweep() {
        phone(1900)
        val vm = newVm()
        vm.companionName = "MeshCore-Companion"
        vm.repeaters.addAll(fleet.map(::contact))
        vm.step = Step.SWEEP
        vm.canReadRepeater = true
        vm.fleetState.sweepEntries.addAll(
            listOf(
                SweepEntry(r1.keyHex, r1.name, SweepStatus.OK, "Всичко е наред · 3д 04ч", "3.99 V · 72%"),
                SweepEntry(r2.keyHex, r2.name, SweepStatus.WARN, "I²C сензор изключен", "3.48 V · 12%",
                    listOf(HealthWarning.I2C_OFF)),
                SweepEntry(r3.keyHex, r3.name, SweepStatus.DANGER, "Радиото не отговаря", "3.71 V · 48%",
                    listOf(HealthWarning.RADIO)),
                SweepEntry(r4.keyHex, r4.name, SweepStatus.FAILED, "Няма отговор"),
            ),
        )
        vm.fleetState.sweepFinished = true
        app(vm)
    }

    @Test fun startMyRepeaters() {
        phone(3100)
        val vm = newVm(
            extraPrefs = mapOf(
                "companion" to mapOf("address" to "AA:BB:CC:DD:EE:01", "name" to "MeshCore-Companion"),
                "pins" to mapOf(r1.keyHex to true),
                "device_names" to fleet.associate { it.keyHex to it.name },
                "passwords" to mapOf(r1.keyHex to "demo", r2.keyHex to "demo"),   // the legacy store: only the keys matter here
                "battery_chem" to mapOf(r1.keyHex to "LI_ION", r2.keyHex to "LI_ION"),
            ),
            prepare = { dir ->
                val health = HealthStore(dir)
                health.add(r1.keyHex, healthLine(3987, 72), hoursAgo(2))
                health.add(r2.keyHex, "WARN i2c batt=3480mV/12% left=6h", hoursAgo(5))
                health.add(r3.keyHex, "WARN radio batt=3710mV/48%", hoursAgo(26))
            },
        )
        // what loadKnownRepeaters builds from the same data (it runs in the background of the real screen)
        val rows = KnownRepeaters.build(
            health = mapOf(
                r1.keyHex to listOf(HealthStore.Record(hoursAgo(2), healthLine(3987, 72))),
                r2.keyHex to listOf(HealthStore.Record(hoursAgo(5), "WARN i2c batt=3480mV/12% left=6h")),
                r3.keyHex to listOf(HealthStore.Record(hoursAgo(26), "WARN radio batt=3710mV/48%")),
            ),
            backups = listOf(
                backup(r1, "$fwBoard|$fwNew", hoursAgo(2), settings(r1)),
                backup(r4, "$fwBoard|$fwOld", hoursAgo(80), settings(r4)),
            ),
            pinned = listOf(r1.keyHex),
            names = fleet.associate { it.keyHex to it.name },
            chems = mapOf(r1.keyHex to BatteryChem.LI_ION, r2.keyHex to BatteryChem.LI_ION),
            passwordKeys = listOf(r1.keyHex, r2.keyHex),
        )
        force(vm, "knownRepeaters", rows)
        app(vm)
    }

    @Test fun backupCompare() {
        phone(1900)
        val old = backup(r1, "$fwBoard|$fwOld", 1_777_000_000_000L, settings(r1) +
            listOf(
                BackupItem(BackupItem.GROUP_CHANNEL, "0", "Public", "00".repeat(48)),
                BackupItem(BackupItem.GROUP_CONTACT, r2.keyHex, r2.name, "00"),
                BackupItem(BackupItem.GROUP_CONTACT, r3.keyHex, r3.name, "00"),
            ))
        val new = backup(r1, "$fwBoard|$fwNew", 1_779_600_000_000L,
            settings(r1, radio = "869.618,62.5,8,8").filter { it.id != "guest.password" } +
                listOf(
                    BackupItem(BackupItem.GROUP_SETTING, "af", "af", "1.0"),
                    BackupItem(BackupItem.GROUP_SETTING, "password", "password", "new-secret"),
                    BackupItem(BackupItem.GROUP_CHANNEL, "0", "Public", "00".repeat(48)),
                    BackupItem(BackupItem.GROUP_CHANNEL, "1", "Планина", "11".repeat(48)),
                    BackupItem(BackupItem.GROUP_CONTACT, r2.keyHex, r2.name, "00"),
                    BackupItem(BackupItem.GROUP_CONTACT, r4.keyHex, r4.name, "00"),
                ))
        shot { BackupCompareContent(old, new, onClose = {}) }
    }

    @Test fun profileBatch() {
        phone(1900)
        val vm = newVm(extraPrefs = mapOf("passwords" to mapOf(r1.keyHex to "demo", r2.keyHex to "demo", r4.keyHex to "demo")))
        vm.repeaters.addAll(fleet.map(::contact))
        vm.toolsState.configProfiles.add(
            ConfigProfile("Планински (EU 869.525)", mapOf("radio" to radioMain, "tx" to "14", "repeat" to "on", "af" to "1.0")),
        )
        vm.openProfileBatch(null)
        shot { ProfileBatchContent(vm) }
    }

    /** Not for the docs: the icons the app draws itself (ui/AppIcons.kt), large, to see that they look right. */
    @Test fun appIconsCheck() {
        phone(600)
        shot {
            androidx.compose.foundation.layout.Row(androidx.compose.ui.Modifier.padding(24.dp)) {
                for (icon in listOf(AppIcons.SystemUpdate, AppIcons.Terminal, AppIcons.CloudOff)) {
                    androidx.compose.material3.Icon(icon, null, androidx.compose.ui.Modifier.size(64.dp).padding(8.dp))
                }
            }
        }
    }
}
