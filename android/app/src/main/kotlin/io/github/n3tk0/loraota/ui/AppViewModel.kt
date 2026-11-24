package io.github.n3tk0.loraota.ui

import android.annotation.SuppressLint
import android.app.Application
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.net.Uri
import android.os.Build
import android.os.ParcelUuid
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import io.github.n3tk0.loraota.AppPrefs
import io.github.n3tk0.loraota.BuildConfig
import io.github.n3tk0.loraota.Lang
import io.github.n3tk0.loraota.OtaService
import io.github.n3tk0.loraota.R
import io.github.n3tk0.loraota.ota.AirtimeBudget
import io.github.n3tk0.loraota.ota.OtaFailed
import io.github.n3tk0.loraota.ota.Phase
import io.github.n3tk0.loraota.ota.estimateMinutes
import io.github.n3tk0.loraota.backup.Backup
import io.github.n3tk0.loraota.backup.BackupDiff
import io.github.n3tk0.loraota.backup.BackupItem
import io.github.n3tk0.loraota.backup.BackupStore
import io.github.n3tk0.loraota.backup.CompanionBackup
import io.github.n3tk0.loraota.backup.DeviceKind
import io.github.n3tk0.loraota.backup.DiffRow
import io.github.n3tk0.loraota.backup.NoAnswer
import io.github.n3tk0.loraota.backup.PasswordBox
import io.github.n3tk0.loraota.backup.BackupItem.Companion.GROUP_SETTING
import io.github.n3tk0.loraota.backup.RepeaterBackup
import io.github.n3tk0.loraota.backup.RestoreResult
import io.github.n3tk0.loraota.backup.WrongPassword
import io.github.n3tk0.loraota.ble.CompanionRadio
import io.github.n3tk0.loraota.ble.CompanionLink
import io.github.n3tk0.loraota.ble.scanNeedsLocation
import io.github.n3tk0.loraota.ble.scanNeedsLocationPermission
import io.github.n3tk0.loraota.ble.scanPermissionMessage
import io.github.n3tk0.loraota.ble.Contact
import io.github.n3tk0.loraota.delta.DeltaGenerator
import io.github.n3tk0.loraota.delta.DetoolsPatch
import io.github.n3tk0.loraota.flash.BleDfu
import io.github.n3tk0.loraota.flash.BoardCheck
import io.github.n3tk0.loraota.flash.FlashFailed
import io.github.n3tk0.loraota.flash.PickedImage
import io.github.n3tk0.loraota.flash.Role
import io.github.n3tk0.loraota.flash.WifiOta
import io.github.n3tk0.loraota.github.AppRelease
import io.github.n3tk0.loraota.github.AppUpdater
import io.github.n3tk0.loraota.github.BuildJob
import io.github.n3tk0.loraota.github.BuildPhase
import io.github.n3tk0.loraota.github.BuildWatcher
import io.github.n3tk0.loraota.github.GitHubApi
import io.github.n3tk0.loraota.github.TokenStore
import io.github.n3tk0.loraota.github.WorkflowInput
import io.github.n3tk0.loraota.github.WorkflowInputs
import io.github.n3tk0.loraota.ota.FirmwareFile
import io.github.n3tk0.loraota.ota.OtaProtocol
import io.github.n3tk0.loraota.ota.OtaSession
import io.github.n3tk0.loraota.ota.Releases
import io.github.n3tk0.loraota.ota.RepeaterStatus
import io.github.n3tk0.loraota.ota.SideRadio
import io.github.n3tk0.loraota.ota.hex
import io.github.n3tk0.loraota.ota.BattCapacity
import io.github.n3tk0.loraota.ota.BattSave
import io.github.n3tk0.loraota.ota.HealthCheck
import io.github.n3tk0.loraota.ota.OtaQueueState
import io.github.n3tk0.loraota.ota.QueueEntry
import io.github.n3tk0.loraota.ota.QueueResult
import io.github.n3tk0.loraota.ota.HealthStore
import io.github.n3tk0.loraota.ota.HealthWarning
import io.github.n3tk0.loraota.ota.HistorySeries
import io.github.n3tk0.loraota.ota.SelfTest
import io.github.n3tk0.loraota.security.PasswordStore
import io.github.n3tk0.loraota.tools.CliHelper
import io.github.n3tk0.loraota.tools.DiagnosticReport
import io.github.n3tk0.loraota.tools.NeighborEntry
import io.github.n3tk0.loraota.tools.NeighborsParser
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

enum class Step { COMPANION, REPEATER, UPDATE, FLASH, BACKUP, HEALTH, QUEUE, SETTINGS, CLI, SWEEP, OFFLINE }

/** Bottom navigation tabs inside a logged-in repeater (Step.UPDATE). */
enum class RepeaterTab { OVERVIEW, HEALTH, FIRMWARE, TOOLS }

/** One line in the interactive CLI console transcript (durationMs set on replies to show link RTT). */
enum class CliLineKind { COMMAND, REPLY, INFO, ERROR }
data class CliLine(val timeMs: Long, val text: String, val kind: CliLineKind, val durationMs: Long? = null)

/** Status of one repeater inside a batch Health Sweep. */
enum class SweepStatus { PENDING, RUNNING, OK, WARN, DANGER, FAILED }
data class SweepEntry(
    val key: String,
    val name: String,
    val status: SweepStatus = SweepStatus.PENDING,
    val detail: String = "",
    val batteryText: String = "",
    val warnings: List<HealthWarning> = emptyList(),
)

/** What the flash screen writes: the repeater the app is logged in to, or the companion itself. */
enum class FlashTarget { REPEATER, COMPANION }

/** A file to flash: a DFU .zip (nRF52, Bluetooth) or an app image .bin (ESP32, Wi-Fi); [env] null: from a file. */
class FlashImage(val name: String, val data: ByteArray, val env: String?, val version: String = "") {
    val isZip get() = name.endsWith(".zip", ignoreCase = true)
    /** For people: "Xiao nrf52 repeater v1.3.0" (a file: its name). */
    val label get() = if (env == null) name else "${env.replace('_', ' ')} $version".trim()
}

/** The 'are you sure' before a flash: where it goes and what did not check out. */
class FlashPlan(val image: FlashImage, val device: String, val address: String?, val warnings: List<String>,
                val name: String = device)   // the plain Bluetooth name, for the DFU library

/** Where the firmware to write comes from: a release (or a build made before), a new build, a file. */
enum class Source { RELEASES, BUILD, FILE }

/** A firmware image the user has: from a release or from a file. */
class Image(val name: String, val data: ByteArray, val env: String? = null) {
    val sha: String = hex(DetoolsPatch.sha256(data))
}

private const val OTA_CHECK_MS = 15_000L

/** The box the group APK does not show: its builds would pile up as pre-releases of the repository. */
const val GROUP_HIDDEN = "release"

@SuppressLint("MissingPermission")
class AppViewModel(app: Application) : AndroidViewModel(app) {
    private val ctx: Context get() = getApplication()
    private fun str(res: Int, vararg args: Any): String = Lang.wrap(ctx).getString(res, *args)
    private val link = CompanionLink(app)
    private val companion = CompanionRadio(link)

    var step by mutableStateOf(Step.COMPANION)
    var busy by mutableStateOf(false)
    var message by mutableStateOf("")
    var messageIsError by mutableStateOf(false)
    var working by mutableStateOf("")   // what a long step does now, under the progress bar
    val log = mutableStateListOf<String>()

    // companion
    val devices = mutableStateListOf<BluetoothDevice>()
    val deviceRssi = mutableStateMapOf<String, Int>()
    private val companionPrefs get() = ctx.getSharedPreferences("companion", Context.MODE_PRIVATE)
    var lastCompanionAddress by mutableStateOf(companionPrefs.getString("address", null))
    var lastCompanionName by mutableStateOf(companionPrefs.getString("name", null))
    var scanning by mutableStateOf(false)
    var companionName by mutableStateOf("")
    var companionOta by mutableStateOf(true)   // our companion firmware: it can go to the side channel

    // repeater
    val repeaters = mutableStateListOf<Contact>()
    var repeater by mutableStateOf<Contact?>(null)
    var info by mutableStateOf<OtaProtocol.Info?>(null)   // null: the repeater has no LoRa OTA (stock firmware)
    var status by mutableStateOf<RepeaterStatus?>(null)
    /** The battery from a current sensor (INA219) in the repeater's telemetry; null without one. */
    var sensorBattery by mutableStateOf<io.github.n3tk0.loraota.ota.RepeaterTelemetry.Battery?>(null)
    var routeHops by mutableStateOf<Int?>(null)   // hops on the route the login learned; null: none learned
    val direct get() = routeHops == 0   // only then the update can go
    var otaChecked by mutableStateOf(false)   // the OTA check got to its end (an answer or none)

    // firmware
    var releases by mutableStateOf<List<FirmwareFile>>(emptyList())
    var oldImage by mutableStateOf<Image?>(null)
    var newImage by mutableStateOf<Image?>(null)
    var patch by mutableStateOf<ByteArray?>(null)
    var repo by mutableStateOf(Releases.DEFAULT_REPO)
    var loraSource by mutableStateOf(Source.RELEASES)
    var flashSource by mutableStateOf(Source.RELEASES)
    var builds by mutableStateOf(Releases.builds(app.filesDir))   // made by the app's builds, kept on the phone
    var upstream by mutableStateOf<List<FirmwareFile>>(emptyList())   // stock MeshCore releases (flash only)
    /** Firmware found in releases after the first page of the list (what the repeater runs), kept on the phone. */
    var olderFound by mutableStateOf(Releases.olderCached(Releases.DEFAULT_REPO, app.filesDir))
    /** The repeater's running image is in no release (all pages looked through) and no build of the app. */
    var runningUnknown by mutableStateOf(false)
    /** The search for the running image ended early (page cap or a failed fetch): it may still be in a release. */
    var runningNotChecked by mutableStateOf(false)
    /** The page of older releases being read now, 0 when no search runs. */
    var olderPage by mutableStateOf(0)
    var flashUpstream by mutableStateOf(false)

    /** Our releases and the app's builds, the builds first (one file once: a build with `release` is both). */
    val allFirmware: List<FirmwareFile>
        get() = (builds + releases + olderFound).distinctBy { it.env + it.sha256 + it.dfuSha256 }

    // a new build on GitHub ("Build Firmware for Boards")
    private val tokens = TokenStore(app)
    var hasToken by mutableStateOf(tokens.has)
    var groupToken by mutableStateOf(tokens.usesGroup)   // the group APK's own or preserved token: nothing to enter
    var hasOwnToken by mutableStateOf(tokens.hasOwn)
    var hasBuiltInGroupToken by mutableStateOf(tokens.hasBuiltInGroup)
    var hasSavedGroupToken by mutableStateOf(tokens.hasSavedGroup)
    var buildInputs by mutableStateOf<List<WorkflowInput>>(emptyList())
    private var buildInputsRole = ""   // the board list the form shows: repeater or companion
    private var buildFormFor = ""      // the device the board was picked for
    var buildBoard by mutableStateOf("")
    var buildBoardAuto by mutableStateOf(false)   // picked by the app from the device, not by the user
    val buildOptions = mutableStateMapOf<String, Boolean>()
    var buildJob by mutableStateOf<BuildJob?>(null)

    // side channel
    var freqMhz by mutableStateOf("869.470")
    var bwKhz by mutableStateOf("125")
    var sf by mutableStateOf("7")
    var cr by mutableStateOf("5")
    var dutyPercent by mutableStateOf("10")
    var ignoreAirLimit by mutableStateOf(false)   // not kept: each start of the app is back within the limit
    private val budget = AirtimeBudget(0.1)   // the companion's time on air over the last hour

    var sent by mutableStateOf(0)
    var total by mutableStateOf(0)
    var done by mutableStateOf(false)
    var phase by mutableStateOf<Phase?>(null)
    var waitUntil by mutableStateOf(0L)
    var confirmUpdate by mutableStateOf(false)   // the 'are you sure' dialog
    var resumable by mutableStateOf(false)       // the repeater keeps an update cut short: 'Continue'
    private var job: Job? = null

    // flash over Bluetooth (nRF52 DFU) or Wi-Fi (ESP32)
    var flashTarget by mutableStateOf(FlashTarget.REPEATER)
    var flashImage by mutableStateOf<FlashImage?>(null)
    var flashPlan by mutableStateOf<FlashPlan?>(null)
    var flashPercent by mutableStateOf(-1)   // -1: not writing

    /** A LoRa update, a flash or a build running: the screen stays on (MainActivity). */
    val keepScreenOn get() = phase != null || flashPercent >= 0 || buildJob?.finished == false
    var flashDone by mutableStateOf(false)
    /** The DFU library's steps of the last Bluetooth flash, seconds from its start first: kept after a failure. */
    var flashLog by mutableStateOf<List<String>>(emptyList())
    var loginSheet by mutableStateOf<Contact?>(null)   // the repeater whose login sheet is open
    var firmwareTab by mutableStateOf(0)   // the repeater's firmware: 0 over LoRa, 1 over Bluetooth / Wi-Fi
    var companionModel by mutableStateOf("")
    var companionVersion by mutableStateOf("")
    private var companionDevice: BluetoothDevice? = null
    val hasCompanionConnection: Boolean get() = companionDevice != null
    /** Offline flash of another device while a companion stays connected: flashCheck/flashBle ignore the companion. */
    private var flashStandalone = false
    private val flashCompanionDevice: BluetoothDevice? get() = if (flashStandalone) null else companionDevice
    private var flashingCompanion = false   // its link drops on purpose

    // backups of the settings (the companion's or the repeater's), kept on the phone
    private val backupStore = BackupStore(app)
    var backupKind by mutableStateOf(DeviceKind.COMPANION)   // whose backups the backup screen shows
    var backups by mutableStateOf<List<Backup>>(emptyList())
    var canReadRepeater by mutableStateOf<Boolean?>(null)   // false: a stock companion cannot read its console
    var restoreFrom by mutableStateOf<Backup?>(null)        // the backup whose differences are shown
    var restoreRows by mutableStateOf<List<DiffRow>>(emptyList())
    val restoreChecked = mutableStateMapOf<String, Boolean>()
    var importText by mutableStateOf<String?>(null)         // an encrypted file: asks for its password
    var importPassword by mutableStateOf("")
    var exportingBackup by mutableStateOf<Backup?>(null)
    var exportPassword by mutableStateOf("")
    var updateBackupNote by mutableStateOf<String?>(null)   // in the update's 'are you sure': no backup, and why
    private var backupReturn = Step.REPEATER
    private var scanJob: Job? = null

    // settings & app self-update
    private var settingsReturn = Step.COMPANION
    var autoUpdateCheck by mutableStateOf(AppPrefs.autoUpdateCheck(app))
    var checkingAppUpdate by mutableStateOf(false)
    var appUpdateChecked by mutableStateOf(false)
    var latestAppRelease by mutableStateOf<AppRelease?>(null)
    var availableUpdate by mutableStateOf<AppRelease?>(null)
    var updateBannerDismissed by mutableStateOf(false)
    var appUpdateError by mutableStateOf<String?>(null)
    var appUpdateDownloading by mutableStateOf(false)
    var appUpdateProgress by mutableStateOf(0)
    var downloadedApk by mutableStateOf<java.io.File?>(null)
    var cacheFileCount by mutableStateOf(0)
    var cacheBytes by mutableStateOf(0L)
    // BLE auto-reconnect
    var reconnecting by mutableStateOf(false)
    var reconnectAttempt by mutableStateOf(0)
    private var reconnectJob: Job? = null
    private var disconnectingIntentionally = false

    // repeater 4-tab navigation & modular tools/fleet state holders
    var repeaterTab by mutableStateOf(RepeaterTab.OVERVIEW)
    private var repeaterHealthLoadedFor: String? = null
    var cliKind by mutableStateOf(DeviceKind.REPEATER)
    private var cliReturn = Step.REPEATER
    private var offlineReturn = Step.COMPANION
    private val allContactsByKey = HashMap<String, String>()

    val toolsState = RepeaterToolsState(app)
    val fleetState = FleetToolsState(app)

    val repeaterCliLog get() = toolsState.repeaterCliLog
    val companionCliLog get() = toolsState.companionCliLog
    fun cliHistory(isCompanion: Boolean) = toolsState.cliHistory(isCompanion)
    val customCliSnippets get() = toolsState.customCliSnippets
    val neighbors get() = toolsState.neighbors
    val neighborsLoaded get() = toolsState.neighborsLoaded
    val neighborsError get() = toolsState.neighborsError
    val lastClockReply get() = toolsState.lastClockReply
    var needsRelogin: Boolean
        get() = toolsState.needsRelogin
        set(v) { toolsState.needsRelogin = v }
    val repeaterSettings get() = toolsState.settings
    val repeaterSettingsDraft get() = toolsState.settingsDraft
    val repeaterSettingsLoaded get() = toolsState.settingsLoaded
    val repeaterSettingsProgress get() = toolsState.settingsProgress
    val configProfiles get() = toolsState.configProfiles
    val pingSamples get() = toolsState.pingSamples
    val pingRunning get() = toolsState.pingRunning

    val pinnedKeys get() = fleetState.pinnedKeys
    val fleetHealth get() = fleetState.fleetHealth
    val knownDeviceNames get() = fleetState.knownDeviceNames
    val observedNeighbors get() = fleetState.observedNeighbors
    val sweepPicked get() = fleetState.sweepPicked
    val sweepEntries get() = fleetState.sweepEntries
    val sweepRunning get() = fleetState.sweepRunning
    val sweepFinished get() = fleetState.sweepFinished
    var sweepSyncClock: Boolean
        get() = fleetState.sweepSyncClock
        set(v) { fleetState.sweepSyncClock = v }
    val offlineHealth get() = fleetState.offlineHealth
    val offlineBackups get() = fleetState.offlineBackups
    val offlineFirmwares get() = fleetState.offlineFirmwares
    val offlineLoading get() = fleetState.offlineLoading
    val offlineBuildRole get() = fleetState.offlineBuildRole
    var exportingFirmware: Pair<FirmwareFile, Boolean>?
        get() = fleetState.exportingFirmware
        set(v) { fleetState.exportingFirmware = v }
    var workspaceArchivePassword: String
        get() = fleetState.workspaceArchivePassword
        set(v) { fleetState.workspaceArchivePassword = v }
    var pendingWorkspaceImportJson: String?
        get() = fleetState.pendingWorkspaceImportJson
        set(v) { fleetState.pendingWorkspaceImportJson = v }
    val fieldPrepProgress get() = fleetState.fieldPrepProgress
    val fieldPrepCandidates get() = fleetState.fieldPrepCandidates
    val fieldPrepPicked get() = fleetState.fieldPrepPicked
    val fieldPrepAlreadyCached get() = fleetState.fieldPrepAlreadyCached
    var showFieldPrepDialog: Boolean
        get() = fleetState.showFieldPrepDialog
        set(v) { fleetState.showFieldPrepDialog = v }

    /** One-shot APK ready to hand to Android's package installer using the live Activity Context. */
    var pendingInstallApk by mutableStateOf<java.io.File?>(null)
        private set

    fun consumePendingInstallApk() {
        pendingInstallApk = null
    }

    /** Survives configuration changes while the system CSV file picker is open. */
    var pendingCsvExport: Pair<HistorySeries, String>? = null

    /** Guards against concurrent onDisconnect coroutines during job.join() / cancelAndJoin(). */
    private var handlingDisconnect = false

    private fun clearDeviceSession() {
        companionDevice = null
        companionName = ""
        companionModel = ""
        companionVersion = ""
        companionOta = false
        canReadRepeater = null
        repeater = null
        info = null
        status = null
        sensorBattery = null
        otaChecked = false
        oldImage = null
        newImage = null
        patch = null
        flashImage = null
        repeaterHealthLoadedFor = null
    }

    private fun finishDisconnect(flashMsg: String?) {
        reconnecting = false
        reconnectAttempt = 0
        patchJob?.cancel()
        calculatingPatch = false
        confirmUpdate = false
        flashPlan = null
        queueAsk = false
        loginSheet = null
        fleetState.sweepRunning = false
        clearDeviceSession()
        message = if (flashMsg != null) "$flashMsg · ${str(R.string.msg_disconnected)}" else str(R.string.msg_disconnected)
        messageIsError = true
        when (step) {
            Step.SETTINGS -> settingsReturn = Step.COMPANION
            Step.OFFLINE -> offlineReturn = Step.COMPANION
            else -> step = Step.COMPANION
        }
    }

    fun cancelReconnect() {
        reconnectJob?.cancel()
        reconnectJob = null
        if (reconnecting) finishDisconnect(null)
    }

    init {
        BuildWatcher.resume(app)
        viewModelScope.launch {
            AppUpdater.installResults.collect { r ->
                if (!r.success) appUpdateError = r.message ?: ""
            }
        }
        viewModelScope.launch {
            BuildWatcher.state.collect { j ->
                val before = buildJob
                buildJob = j
                if (j?.finished == true && before != null && !before.finished) {
                    buildFinished(j)
                } else if (j?.finished == true) {
                    builds = withContext(Dispatchers.IO) { Releases.builds(ctx.filesDir) }
                }
            }
        }
        if (autoUpdateCheck && System.currentTimeMillis() - AppPrefs.lastUpdateCheck(app) > AppUpdater.AUTO_CHECK_INTERVAL_MS) {
            checkAppUpdate(manual = false)
        }
        link.onDisconnect = {
            viewModelScope.launch {
                fun isActiveConnection() = step != Step.COMPANION &&
                    !(step == Step.OFFLINE && offlineReturn == Step.COMPANION) &&
                    !(step == Step.SETTINGS && settingsReturn == Step.COMPANION)

                if (!isActiveConnection() || flashingCompanion || disconnectingIntentionally || reconnecting || handlingDisconnect) {
                    return@launch
                }
                handlingDisconnect = true
                try {
                    val wasUpdating = phase != null
                    val wasFlashingRepeater = flashPercent >= 0
                    // a repeater's Bluetooth / Wi-Fi flash does not need the companion: let it finish on screen first;
                    // a LoRa update (even while sleeping in WAIT_AIR / PAUSE / RESTART) or other work is cancelled now
                    // (the link is already gone so no ABORT is sent, and resumable is kept below)
                    if (wasFlashingRepeater) job?.join() else job?.cancelAndJoin()
                    // Re-check after suspension in case user intentionally disconnected while we waited
                    if (!isActiveConnection() || disconnectingIntentionally) return@launch
                    if (wasUpdating) resumable = true
                    val flashMsg = if (wasFlashingRepeater) message.takeIf { it.isNotEmpty() } else null
                    val dev = companionDevice
                    val btOn = (ctx.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager)?.adapter?.isEnabled == true
                    if (dev != null && btOn) {
                        reconnecting = true
                        reconnectJob = viewModelScope.launch {
                            var ok = false
                            try {
                                for (attempt in 1..2) {
                                    reconnectAttempt = attempt
                                    delay(if (attempt == 1) 1200L else 2200L)
                                    val connected = try {
                                        link.connect(dev)
                                    } catch (e: CancellationException) {
                                        throw e
                                    } catch (_: Exception) {
                                        false
                                    }
                                    if (connected) {
                                        val started = try {
                                            companion.start()
                                            companionOta = try {
                                                companion.cli("tempradio off")
                                                true
                                            } catch (e: CancellationException) {
                                                throw e
                                            } catch (_: Exception) {
                                                false
                                            }
                                            canReadRepeater = try {
                                                RepeaterBackup.supported(companion)
                                            } catch (e: CancellationException) {
                                                throw e
                                            } catch (_: Exception) {
                                                false
                                            }
                                            true
                                        } catch (e: CancellationException) {
                                            throw e
                                        } catch (_: Exception) {
                                            false
                                        }
                                        if (started) {
                                            ok = true
                                            break
                                        } else {
                                            withContext(NonCancellable) { link.closeAndRefresh() }
                                        }
                                    }
                                }
                            } catch (e: CancellationException) {
                                throw e
                            } finally {
                                reconnectJob = null
                            }
                            if (ok) {
                                reconnecting = false
                                reconnectAttempt = 0
                                message = if (flashMsg != null) "$flashMsg · ${str(R.string.msg_reconnected)}" else str(R.string.msg_reconnected)
                                messageIsError = false
                            } else {
                                finishDisconnect(flashMsg)
                            }
                        }
                    } else {
                        finishDisconnect(flashMsg)
                    }
                } finally {
                    handlingDisconnect = false
                }
            }
        }
    }

    private fun work(block: suspend () -> Unit) {
        if (busy || reconnecting) return
        busy = true
        message = ""
        messageIsError = false
        job = viewModelScope.launch {
            try {
                block()
            } catch (e: kotlinx.coroutines.CancellationException) {
                message = str(R.string.msg_stopped)
                messageIsError = false
            } catch (e: OtaFailed) {
                message = str(e.res, *e.args.toTypedArray())
                messageIsError = true
            } catch (e: io.github.n3tk0.loraota.ble.CompanionError) {
                message = str(R.string.err_companion, e.message ?: "")
                messageIsError = true
            } catch (e: Exception) {
                message = e.message ?: e.toString()
                messageIsError = true
            } finally {
                busy = false
                working = ""
                job = null
            }
        }
    }

    fun stop() {
        job?.cancel()
    }

    /** A job that Stop (or Back) can end; a disconnect's own clean-up is not one. */
    val canStop get() = busy && job != null

    // ---- companion ----

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            val dev = result.device
            val rssi = result.rssi
            viewModelScope.launch(Dispatchers.Main) {
                if (!scanning) return@launch
                deviceRssi[dev.address] = rssi
                if (devices.none { it.address == dev.address }) devices.add(dev)
            }
        }

        override fun onScanFailed(errorCode: Int) {
            viewModelScope.launch(Dispatchers.Main) {
                scanJob?.cancel()
                scanning = false
                message = str(R.string.msg_scan_failed, errorCode)
                messageIsError = true
            }
        }
    }

    /** Called by MainActivity after the user grants Bluetooth / Location permissions. */
    fun onPermissionsGranted() {
        if (step == Step.COMPANION && !busy && !scanning) {
            message = ""
            messageIsError = false
            scan()
        }
    }

    fun scan() {
        val adapter = (ctx.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter
        val scanner = if (adapter?.isEnabled == true) adapter.bluetoothLeScanner else null
        if (scanner == null) {
            message = str(R.string.msg_bluetooth_off)
            messageIsError = true
            return
        }
        if (scanNeedsLocationPermission(ctx)) {
            message = str(R.string.msg_no_location_permission)
            messageIsError = true
            return
        }
        if (scanNeedsLocation(ctx)) {
            message = str(R.string.msg_location_off)
            messageIsError = true
            return
        }
        scanJob?.cancel()
        runCatching { scanner.stopScan(scanCallback) }
        devices.clear()
        deviceRssi.clear()
        val filter = ScanFilter.Builder().setServiceUuid(ParcelUuid(CompanionLink.SERVICE)).build()
        try {
            scanner.startScan(listOf(filter), ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(), scanCallback)
        } catch (e: SecurityException) {   // 'Nearby devices' (12+) or Location (7-11) not allowed
            message = str(scanPermissionMessage())
            messageIsError = true
            return
        }
        scanning = true
        scanJob = viewModelScope.launch {
            try {
                delay(10_000)
            } finally {
                runCatching { scanner.stopScan(scanCallback) }
                scanning = false
            }
        }
    }

    fun connectByAddress(address: String) {
        val adapter = (ctx.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter
        if (adapter == null || !adapter.isEnabled) {
            message = str(R.string.msg_bluetooth_off)
            messageIsError = true
            return
        }
        val dev = devices.firstOrNull { it.address.equals(address, ignoreCase = true) }
            ?: runCatching { adapter.getRemoteDevice(address) }.getOrNull()
            ?: run {
                message = str(R.string.msg_connect_failed)
                messageIsError = true
                return
            }
        connect(dev)
    }

    fun connect(device: BluetoothDevice) = work {
        val adapter = (ctx.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter
        if (adapter == null || !adapter.isEnabled) throw Exception(str(R.string.msg_bluetooth_off))
        if (scanning) {
            scanJob?.cancel()
            runCatching { adapter.bluetoothLeScanner?.stopScan(scanCallback) }
            scanning = false
            // let the BLE controller finish tearing down the low-latency scan before connectGatt (avoids GATT 133)
            delay(250)
        }
        working = str(R.string.working_connect)
        companionModel = ""
        companionVersion = ""
        canReadRepeater = null   // another companion: asked again
        val connectedOk = try {
            link.connect(device)
        } catch (e: SecurityException) {
            throw Exception(str(scanPermissionMessage()))
        }
        if (!connectedOk) {
            val detail = link.lastError
            throw Exception(
                if (detail.isNotEmpty()) str(R.string.msg_connect_failed_detail, detail)
                else str(R.string.msg_connect_failed)
            )
        }
        try {
            companion.start()
            companionDevice = device
            // left on the side channel by an update cut short; a stock companion has no such command
            companionOta = runCatching { companion.cli("tempradio off") }.isSuccess
            canReadRepeater = runCatching { RepeaterBackup.supported(companion) }.getOrDefault(false)
            companionName = companion.nodeName.ifEmpty {
                runCatching { device.name }.getOrNull() ?: device.address
            }
            lastCompanionAddress = device.address
            lastCompanionName = companionName
            companionPrefs.edit().putString("address", device.address).putString("name", companionName).apply()
            deviceKey(DeviceKind.COMPANION).takeIf { it.isNotEmpty() }?.let { rememberDeviceName(it, companionName) }
            runCatching { companion.deviceInfo() }.onSuccess { (model, version) ->
                companionModel = model
                companionVersion = version
            }
            loadRepeaters()
            step = Step.REPEATER
        } catch (e: Throwable) {
            // do not leave a half-open GATT link behind (the companion stops advertising while connected)
            withContext(NonCancellable) { link.closeAndRefresh() }
            throw e
        }
    }

    fun refreshRepeaters() = work { loadRepeaters() }

    private fun rememberDeviceName(keyHex: String, name: String) = fleetState.rememberDeviceName(keyHex, name)

    fun refreshFleetHealth() {
        viewModelScope.launch {
            val latest = withContext(Dispatchers.IO) { healthStore.allLatest() }
            fleetState.fleetHealth.clear()
            fleetState.fleetHealth.putAll(latest)
        }
    }

    private suspend fun loadRepeaters() {
        val all = companion.contacts()
        allContactsByKey.clear()
        for (c in all) {
            val k = hex(c.publicKey)
            if (c.name.isNotBlank()) allContactsByKey[k] = c.name
        }
        fleetState.rememberDeviceNames(allContactsByKey)
        val list = all.filter { it.isRepeater }
            .sortedWith(compareBy<Contact>({ it.hops ?: Int.MAX_VALUE }, { it.name.lowercase() }))
        repeaters.clear()
        // the ones next to us first: only those can be updated
        repeaters.addAll(list)
        val latest = withContext(Dispatchers.IO) { healthStore.allLatest() }
        fleetState.fleetHealth.clear()
        fleetState.fleetHealth.putAll(latest)
    }

    /** An update or a flash is writing: the screen shows only its progress, Back asks first. */
    val running get() = busy && (phase != null || flashPercent >= 0)

    /** The phone's Back and the arrow in the top bar. */
    fun back() {
        when (step) {
            Step.SETTINGS -> closeSettings()
            Step.OFFLINE -> closeOffline()
            Step.CLI -> closeCli()
            Step.SWEEP -> closeSweep()
            Step.FLASH -> closeFlash()
            Step.BACKUP -> closeBackup()
            Step.HEALTH -> closeHealth()
            Step.QUEUE -> closeQueue()
            Step.UPDATE -> backToRepeaters()
            Step.REPEATER -> disconnect()
            Step.COMPANION -> {}
        }
    }

    fun disconnect() {
        loginSheet = null
        val rj = reconnectJob
        reconnectJob = null
        reconnecting = false
        reconnectAttempt = 0
        disconnectingIntentionally = true
        busy = true
        step = Step.COMPANION
        devices.clear()   // a new connect waits for a new scan, after the clean-up
        viewModelScope.launch {
            try {
                rj?.cancelAndJoin()    // wait for any in-flight reconnect attempt before closing GATT
                job?.cancelAndJoin()   // the update's own clean-up (ABORT, normal radio) goes first
                busy = true
                restorePath()
                link.closeAndRefresh()
            } finally {
                clearDeviceSession()
                busy = false
                disconnectingIntentionally = false
            }
            if (step == Step.COMPANION && !scanning && devices.isEmpty()) scan()
        }
    }

    // ---- repeater ----

    /**
     * Admin login by flood, so both sides learn the route (the update only goes when it has no hop);
     * the contact with the route just learned, null when the companion does not say.
     */
    private suspend fun signIn(contact: Contact, password: String): Contact? {
        companion.resetPath(contact)
        working = str(R.string.working_login)
        return try {
            if (!companion.login(contact, password)) throw Exception(str(R.string.msg_not_admin))
            delay(4000)   // the companion sends its route back to the repeater
            companion.contact(contact.publicKey)
        } catch (e: Exception) {
            withContext(NonCancellable) { runCatching { companion.restorePath(contact) } }
            throw e
        }
    }

    fun login(contact: Contact, password: String, initialTab: RepeaterTab = RepeaterTab.OVERVIEW) = work {
        val learned = try {
            signIn(contact, password)
        } catch (e: CancellationException) {
            throw e
        } catch (e: Exception) {
            if (step == Step.REPEATER && loginSheet == null) loginSheet = contact
            throw e
        }
        savePassword(contact, password)
        // the route just learned is the one to keep afterwards, not the older (longer) one
        repeater = learned ?: contact
        loadBatteryChem(repeater)
        info = null
        status = null
        sensorBattery = null
        otaChecked = false
        patchJob?.cancel()
        calculatingPatch = false
        oldImage = null
        runningUnknown = false
        runningNotChecked = false
        newImage = null
        patch = null
        done = false
        confirmUpdate = false
        sent = 0
        total = 0
        phase = null
        log.clear()
        repeaterTab = initialTab
        repeaterHealthLoadedFor = null
        toolsState.clearForNewRepeater()
        firmwareTab = 0
        flashTarget = FlashTarget.REPEATER
        flashImage = null
        flashDone = false
        loginSheet = null
        step = Step.UPDATE
        if (initialTab == RepeaterTab.HEALTH) {
            resetHealthState(DeviceKind.REPEATER)
            val target = learned ?: contact
            inspect(target, learned?.hops)
            val dev = deviceKey(DeviceKind.REPEATER)
            healthRecords = withContext(Dispatchers.IO) { healthStore.records(dev) }
            val latestRec = healthRecords.lastOrNull()
            if (latestRec != null) {
                val parsed = SelfTest.parse(latestRec.line)
                val prev = healthRecords.getOrNull(healthRecords.size - 2)?.let { SelfTest.parse(it.line) }
                selfTest = parsed
                healthWarnings = parsed?.let { HealthCheck.warnings(it, prev) }.orEmpty()
                if (parsed != null && canReadRepeater == true) {
                    try {
                        readBattery(target, hasGauge = "day" in parsed.values)
                    } finally {
                        withContext(NonCancellable) { runCatching { companion.captureCli(0) } }
                    }
                }
                repeaterHealthLoadedFor = dev
            } else if (canReadRepeater == true) {
                // No prior health record (e.g. opened a failed Sweep row): fetch live selftest + battery
                working = str(R.string.working_selftest)
                val line = try {
                    val l = RepeaterBackup.ask(companion, target, SelfTest.COMMAND)?.removePrefix("> ")?.trim()
                    SelfTest.parse(l ?: "")?.let { readBattery(target, hasGauge = "day" in it.values) }
                    l
                } finally {
                    withContext(NonCancellable) { runCatching { companion.captureCli(0) } }
                }
                val parsed = line?.let { SelfTest.parse(it) }
                if (line != null && parsed != null) {
                    selfTest = parsed
                    healthRecords = withContext(Dispatchers.IO) {
                        healthStore.add(dev, line)
                        healthStore.records(dev)
                    }
                    healthRecords.lastOrNull()?.let { fleetState.fleetHealth[dev] = it }
                    healthWarnings = HealthCheck.warnings(parsed, null)
                } else if (line != null) {
                    selfTestMissing = true
                    historyMissing = true
                }
                repeaterHealthLoadedFor = dev
            }
        } else {
            // not read back: the pre-login route (reset for the login) says nothing about now
            inspect(learned ?: contact, learned?.hops)
        }
    }

    /** Again the status and the OTA check, e.g. when an answer got lost. */
    fun recheck() = work {
        val old = repeater ?: return@work
        patchJob?.cancel()
        calculatingPatch = false
        oldImage = null
        patch = null
        val now = companion.contact(old.publicKey)
        if (now != null) repeater = now
        inspect(now ?: old, now?.hops)
    }

    /**
     * The status (any repeater answers it), then whether the repeater has LoRa OTA: a stock MeshCore
     * repeater does not answer the OTA request at all, so one short try, and 'Check again' on the screen.
     */
    private suspend fun inspect(r: Contact, hops: Int?) {
        otaChecked = false
        routeHops = hops
        working = str(R.string.working_status)
        status = readStatus(r)
        // the battery from a sensor: a stock repeater reports its board's own reading (0.45 V on a Xiao without
        // its battery pins) also when an INA219 measures the battery
        if (status != null) sensorBattery = readSensorBattery(r)
        working = str(R.string.working_ota)
        val i = try {
            session(r).info(OTA_CHECK_MS, 1)
        } catch (e: OtaFailed) {
            if (e.res != R.string.err_no_answer) throw e
            null
        }
        info = i
        otaChecked = true
        resumable = i?.state == OtaProtocol.STATE_PAUSED
        if (i != null) loadReleases()   // a route with hops is shown on the screen (and keeps Update off)
    }

    private suspend fun readSensorBattery(r: Contact): io.github.n3tk0.loraota.ota.RepeaterTelemetry.Battery? = runCatching {
        val tag = companion.sendRequest(r, io.github.n3tk0.loraota.ota.RepeaterTelemetry.request())
        companion.awaitResponse(tag, 12_000)?.let { io.github.n3tk0.loraota.ota.RepeaterTelemetry.parse(it).battery() }
    }.getOrNull()

    /** One try: 'Check again' asks again, and a slow link should not hold the OTA check back. */
    private suspend fun readStatus(r: Contact): RepeaterStatus? {
        val tag = companion.sendRequest(r, RepeaterStatus.request())
        return companion.awaitResponse(tag, 12_000)?.let { RepeaterStatus.parse(it) }
    }

    // admin passwords, per repeater, encrypted at rest with Android Keystore (migrating legacy plaintext automatically)
    private val passwordStore by lazy { PasswordStore(ctx) }

    // which repeaters have one, read once: the list asks for every row
    private val savedKeys = mutableStateListOf<String>().apply { addAll(passwordStore.keys()) }

    fun hasSavedPassword(contact: Contact) = hex(contact.publicKey) in savedKeys

    fun savedPassword(contact: Contact): String? {
        val key = hex(contact.publicKey)
        val pw = passwordStore.get(key)
        if (pw == null && key !in passwordStore.keys()) savedKeys.remove(key)
        return pw
    }

    private fun savePassword(contact: Contact, password: String) {
        val key = hex(contact.publicKey)
        val secure = passwordStore.set(key, password)
        if (key !in savedKeys) savedKeys.add(key)
        if (!secure) message = str(R.string.msg_password_not_secure)
    }

    fun forgetPassword(contact: Contact) {
        val key = hex(contact.publicKey)
        passwordStore.remove(key)
        savedKeys.remove(key)
    }

    /** `set ota on` in the repeater's console, then INFO to see that it took. */
    fun enableOta() = work {
        val r = repeater ?: return@work
        companion.sendRemoteCli(r, "set ota on")
        var i: OtaProtocol.Info? = null
        for (attempt in 1..3) {
            delay(3000)
            i = runCatching { session(r).info() }
                .also { it.exceptionOrNull()?.let { e -> if (e is CancellationException) throw e } }
                .getOrNull()
            if (i?.enabled == true) break
        }
        if (i != null) info = i
        if (i?.enabled != true) throw Exception(str(R.string.msg_ota_on_failed))
        message = str(R.string.msg_ota_on)
    }

    private fun session(contact: Contact) = OtaSession(companion, contact, sideRadio(), budget.also { it.duty = duty() },
        { log.add(it) }, { p, until -> phase = p; waitUntil = until; notifyProgress() }) { s, t ->
        sent = s
        total = t
        notifyProgress()
    }

    private fun notifyTitle() = str(R.string.notif_title, repeater?.name ?: "")

    /** The phase in the notification, for when the app is not on screen. */
    private fun notifyProgress() {
        val text = when (phase) {
            Phase.START -> str(R.string.phase_start)
            Phase.SWITCH -> str(R.string.phase_switch)
            Phase.SEND -> str(R.string.phase_send, if (total > 0) sent * 100 / total else 0, remainingMinutes())
            Phase.WAIT_AIR -> str(R.string.phase_wait_air, ((waitUntil - System.currentTimeMillis()).coerceAtLeast(0) + 59_999) / 60_000)
            Phase.FINISH -> str(R.string.phase_finish)
            Phase.RESTART -> str(R.string.phase_restart)
            null -> return
        }
        OtaService.update(ctx, notifyTitle(), text, if (phase == Phase.RESTART) 0 else sent, if (phase == Phase.RESTART) 0 else total)
    }

    private suspend fun restorePath(r: Contact? = repeater) {
        if (r == null) return
        runCatching { companion.restorePath(r) }
    }

    fun backToRepeaters() {
        val oldRepeater = repeater
        patchJob?.cancel()
        calculatingPatch = false
        repeater = null
        info = null
        status = null
        sensorBattery = null
        confirmUpdate = false
        step = Step.REPEATER
        if (searching) releasesJob?.cancel()   // the search of the old repeater's firmware is of no use now
        viewModelScope.launch {
            job?.cancelAndJoin()
            restorePath(oldRepeater)
        }
    }

    // ---- firmware ----

    /** Why the list of releases could not be fetched (the list kept on the phone is shown): null when it was. */
    var releasesError by mutableStateOf<String?>(null)

    /** Releases whose manifest did not download (shown from GitHub's file list): count and reason, null when none. */
    var releasesWarning by mutableStateOf<Pair<Int, String>?>(null)

    /** The same for the Upstream list of the flash screen. */
    var upstreamError by mutableStateOf<String?>(null)

    private fun releasesWhy(e: Throwable?): String =
        if ((e as? io.github.n3tk0.loraota.ota.HttpError)?.rateLimited == true) str(R.string.msg_releases_rate_limit, e.code)
        else e?.message ?: ""

    private var releasesLoadedAt = 0L
    private var upstreamLoadedAt = 0L

    private fun refreshOursIfStale() { if (releases.isEmpty() || Releases.isStale(releasesLoadedAt)) loadReleases() }
    private fun refreshUpstreamIfStale() { if (upstream.isEmpty() || Releases.isStale(upstreamLoadedAt)) loadUpstream() }

    private var releasesJob: Job? = null
    private var upstreamJob: Job? = null

    /** Loads our releases; a load that is already running (double Refresh, tab switch) is not started twice: its Job is returned. */
    fun loadReleases(): Job {
        releasesJob?.takeIf { it.isActive }?.let { return it }
        return doLoadReleases().also { releasesJob = it }
    }

    private fun doLoadReleases() = viewModelScope.launch {
        Releases.apiToken = tokens.get()
        releasesError = null
        releasesWarning = null
        // finds of earlier searches first: the queue needs them with no running repeater (and no internet) too
        olderFound = withContext(Dispatchers.IO) { Releases.olderCached(repo, ctx.filesDir) }
        val list = runCatching { withContext(Dispatchers.IO) { Releases.listDetailed(repo, ctx.filesDir) } }
        list.getOrNull()?.let { l -> if (l.manifestFailed > 0) releasesWarning = l.manifestFailed to l.lastError }
        releases = list.getOrNull()?.files ?: withContext(Dispatchers.IO) { Releases.cached(repo, ctx.filesDir) }
        if (list.isSuccess) releasesLoadedAt = System.currentTimeMillis()
        if (list.isFailure) {
            val why = releasesWhy(list.exceptionOrNull())
            releasesError = why
            if (releases.isNotEmpty()) {
                // no internet on site: the list kept last time (an error on screen stays)
                if (!messageIsError || message.isEmpty()) {
                    message = str(R.string.msg_releases_offline, why)
                    messageIsError = false
                }
            } else {
                message = str(R.string.msg_releases_failed, why)
                messageIsError = true
            }
        }
        // the firmware the repeater runs, if a release has it
        val i = info ?: return@launch
        searchRunning(hex(i.imageSha), Releases.MAX_PAGES, list.isSuccess, list.getOrNull()?.releaseCount ?: -1)
    }

    /** "Search again" on the card of an incomplete search: more pages. */
    fun searchAgain(): Job {
        releasesJob?.takeIf { it.isActive }?.let { return it }
        return viewModelScope.launch {
            val i = info ?: return@launch
            searchRunning(hex(i.imageSha), Releases.MAX_PAGES_RETRY, true, -1)
        }.also { releasesJob = it }
    }

    /**
     * Looks for the firmware with [sha] (what the repeater runs) in the loaded lists and then in the older
     * releases, sets the cards' state and the old image. "Not from GitHub" only when every page was looked
     * through ([listOk]: no internet proves nothing).
     */
    private suspend fun searchRunning(sha: String, maxPages: Int, listOk: Boolean, firstPageReleases: Int) {
        // another repeater may be picked while this runs: the results are for the one the search started with
        fun stale() = info?.let { hex(it.imageSha) } != sha
        var found = allFirmware.firstOrNull { it.sha256 == sha }
        var result: Releases.SearchResult? = null
        var olderFailed = false
        // offline: nothing to search (the find kept from earlier is read below); a short first page: no page 2
        val kept = found == null && !listOk &&
            withContext(Dispatchers.IO) { Releases.olderCached(repo, ctx.filesDir).any { it.sha256 == sha } }
        if (found == null && (listOk || kept) && Releases.hasOlderPages(firstPageReleases)) {
            // not in the first page: the older pages of releases (a find is kept for no internet)
            olderPage = 2
            searching = true
            val older = try {
                runCatching {
                    // the page callback comes from the IO thread: the state is written on the main one
                    withContext(Dispatchers.IO) {
                        Releases.findOlder(repo, sha, ctx.filesDir, maxPages) { p ->
                            viewModelScope.launch(Dispatchers.Main) { if (searching) olderPage = p }
                        }
                    }
                }.also { it.exceptionOrNull()?.let { e -> if (e is CancellationException) throw e } }
            } finally {
                searching = false
                olderPage = 0
            }
            if (stale()) return
            olderFailed = older.isFailure
            result = older.getOrNull()
            if (result is Releases.SearchResult.Found) {
                found = result.file
                addOlderFound(result.file)
            }
        }
        if (stale()) return
        runningUnknown = found == null && listOk && oldImage == null &&
            (result is Releases.SearchResult.NotFound || (result == null && !olderFailed && !Releases.hasOlderPages(firstPageReleases)))
        runningNotChecked = found == null && listOk && oldImage == null && (result is Releases.SearchResult.Incomplete || olderFailed)
        val running = found
        if (running != null && oldImage == null) {
            try {
                val data = withContext(Dispatchers.IO) { Releases.download(running, ctx.filesDir) }
                if (!stale() && oldImage == null) {
                    oldImage = Image(running.file, data, running.env)
                    refreshPatchPreview()
                }
            } catch (e: CancellationException) {
                throw e
            } catch (e: Exception) {
                // found, but the file did not come: say it (the old firmware can still be picked by hand)
                if (!stale()) {
                    message = str(R.string.msg_running_download_failed, running.file, releasesWhy(e))
                    messageIsError = true
                }
            }
        }
    }

    private var searching = false

    private suspend fun addOlderFound(f: FirmwareFile) {
        olderFound = withContext(Dispatchers.IO) { Releases.olderCached(repo, ctx.filesDir) }
            .let { if (it.any { o -> o.sha256 == f.sha256 }) it else listOf(f) + it }
    }

    /** The one entry of [newCandidates] to suggest: the newest for the board (the choice stays the user's). */
    fun recommended(): FirmwareFile? = Releases.recommended(newCandidates())

    /** Releases for the same board as the one the repeater runs (all when that is not known). */
    fun newCandidates(): List<FirmwareFile> {
        val env = oldImage?.env ?: allFirmware.firstOrNull { it.sha256 == oldImage?.sha }?.env
        val nrf = info?.platform == OtaProtocol.PLATFORM_NRF52
        return allFirmware.filter {
            // unknown board: at least the same chip family (the repeater also refuses an image for another chip)
            it.hasLoraOta &&
            (if (env != null) it.env == env else it.env.contains("nrf52", ignoreCase = true) == nrf) &&
                it.sha256 != oldImage?.sha
        }
    }

    var calculatingPatch by mutableStateOf(false)
    private var patchJob: Job? = null

    private fun refreshPatchPreview() {
        patchJob?.cancel()
        val old = oldImage
        val new = newImage
        val i = info
        if (old == null || new == null || (i != null && !i.fits(new.data.size))) {
            patch = null
            calculatingPatch = false
            return
        }
        calculatingPatch = true
        patchJob = viewModelScope.launch {
            try {
                val p = withContext(Dispatchers.Default) { DeltaGenerator.create(old.data, new.data) }
                if (oldImage === old && newImage === new) patch = p
            } catch (e: CancellationException) {
                throw e
            } catch (_: Exception) {
            } catch (_: OutOfMemoryError) {
                // a small phone (Android 7, 2-3 GB): no preview; 'Update' tries again and says why
            } finally {
                if (oldImage === old && newImage === new) calculatingPatch = false
            }
        }
    }

    /** A side channel preset; [freq] when the width needs its own centre to stay in 869.4-869.65 MHz. */
    data class SidePreset(val sf: String, val bw: String, val freq: String?, val label: Int)

    fun applySidePreset(p: SidePreset) {
        sf = p.sf
        bwKhz = p.bw
        if (p.freq != null) freqMhz = p.freq
    }

    fun pickRelease(f: FirmwareFile) = work {
        val data = withContext(Dispatchers.IO) { Releases.download(f, ctx.filesDir) }
        newImage = Image(f.file, data, f.env)
        patch = null
        refreshPatchPreview()
    }

    fun pickFile(uri: Uri, isOld: Boolean) = work {
        val name = fileName(uri)
        val i = info
        // read and parse (unzip, SHA-256) off the main thread, once; an invalid .zip / .uf2 says why (not its size)
        val picked = withContext(Dispatchers.IO) {
            val data = ctx.contentResolver.openInputStream(uri)!!.use { it.readBytes() }
            val app = PickedImage.app(data)
            if (!isOld) return@withContext app
            if (i == null) return@withContext app
            // an image padded past what the repeater runs (a .uf2): its first imageSize bytes
            PickedImage.runningOf(app, i.imageSize, hex(i.imageSha)) ?: throw Exception(
                str(R.string.msg_not_running_firmware) + " " + str(R.string.msg_not_running_detail,
                    OtaProtocol.parseIdent(i.ident).commit ?: "?", i.imageSize, app.size))
        }
        if (isOld) {
            oldImage = Image(name, picked)
            runningUnknown = false
            runningNotChecked = false
        } else {
            newImage = Image(name, picked)
        }
        patch = null
        refreshPatchPreview()
    }

    /** The name the file has for the user (a content URI's last segment can be just an id). */
    private fun fileName(uri: Uri): String =
        runCatching {
            ctx.contentResolver.query(uri, arrayOf(android.provider.OpenableColumns.DISPLAY_NAME), null, null, null)?.use {
                if (it.moveToFirst()) it.getString(0) else null
            }
        }.getOrNull() ?: uri.lastPathSegment?.substringAfterLast('/') ?: "file"

    fun sideRadio(): SideRadio = SideRadio(
        ((freqMhz.replace(',', '.').toDoubleOrNull() ?: 869.47) * 1000).toInt().coerceIn(137_000, 1_020_000),
        ((bwKhz.replace(',', '.').toDoubleOrNull() ?: 125.0) * 1000).toInt().coerceIn(7_800, 500_000),
        (sf.trim().toIntOrNull() ?: 7).coerceIn(5, 12),
        (cr.trim().toIntOrNull() ?: 5).coerceIn(5, 8),
    )

    /** The new image only goes "in place" on this repeater: a power cut while it copies needs a visit. */
    fun inPlace(): Boolean {
        val n = newImage?.data?.size ?: return false
        return info?.inPlace(n) == true
    }

    /** Any other nRF52 update: the same final copy, so the same (smaller) risk note. */
    fun nrfCopy(): Boolean = newImage != null && info?.platform == OtaProtocol.PLATFORM_NRF52 && !inPlace()

    fun duty(): Double = if (ignoreAirLimit) 1.0 else ((dutyPercent.replace(',', '.').toDoubleOrNull() ?: 10.0) / 100.0).coerceIn(0.01, 1.0)

    fun estimateMinutes(): Double? {
        val p = patch ?: return null
        budget.duty = duty()
        return estimateMinutes(sideRadio(), budget, p.size)
    }

    fun remainingMinutes(): Int {
        val p = patch ?: return 0
        budget.duty = duty()
        val left = ((total - sent).coerceAtLeast(0) * OtaSession.CHUNK).coerceAtMost(p.size)
        return kotlin.math.ceil(estimateMinutes(sideRadio(), budget, left)).toInt()
    }

    /** 'Update' / 'Continue': makes the delta if needed, then asks before it starts. */
    fun update() = work {
        val old = oldImage ?: return@work
        val new = newImage ?: return@work
        val i = info
        if (i != null && !i.fits(new.data.size)) throw Exception(str(R.string.msg_too_big))
        patchJob?.join()
        if (patch == null) {
            patch = withContext(Dispatchers.Default) { DeltaGenerator.create(old.data, new.data) }
        }
        updateBackupNote = autoBackup(DeviceKind.REPEATER)
        confirmUpdate = true
    }

    fun send() {
        confirmUpdate = false
        work {
            val r = repeater ?: return@work
            val p = patch ?: return@work
            val old = oldImage ?: return@work
            val new = newImage ?: return@work
            done = false
            log.clear()
            OtaService.start(ctx, notifyTitle(), str(R.string.phase_start))
            val after = try {
                session(r).run(old.data, new.data, p, inPlace())
            } catch (e: OtaFailed) {
                resumable = e.resumable
                throw e
            } catch (e: kotlinx.coroutines.CancellationException) {
                resumable = false
                throw e
            } finally {
                phase = null
                OtaService.stop(ctx)
            }
            resumable = false
            done = true
            info = after
            oldImage = new
            newImage = null
            patch = null
            message = str(R.string.msg_verified, after.ident.replace('|', ' '))
        }
    }

    // ---- flash ----

    /** The companion's own flash screen, or (repeater) the Bluetooth / Wi-Fi tab of the repeater's screen. */
    fun openFlash(target: FlashTarget) {
        flashStandalone = false
        if (target == FlashTarget.COMPANION) flashReturn = step
        flashTarget = target
        flashImage = null
        flashPlan = null
        flashPercent = -1
        flashDone = false
        message = ""
        if (target == FlashTarget.COMPANION) step = Step.FLASH
        if (flashUpstream) refreshUpstreamIfStale() else refreshOursIfStale()
    }

    private var flashReturn = Step.REPEATER   // where the companion's flash screen was opened from

    /** The repeater's LoRa tab: the list of releases is refreshed when it is old, as when the page opens. */
    fun showLoraTab() {
        firmwareTab = 0
        refreshOursIfStale()
    }

    /** The repeater's Bluetooth / Wi-Fi tab. */
    fun showFlashTab() {
        firmwareTab = 1
        if (flashTarget != FlashTarget.REPEATER) openFlash(FlashTarget.REPEATER)
        else if (flashUpstream) refreshUpstreamIfStale() else refreshOursIfStale()
    }

    fun closeFlash() {
        if (flashTarget == FlashTarget.COMPANION) {
            // after a flash (or a failed one) the companion is gone: back to the start (or back to Offline if opened from there)
            if (flashDone || flashingCompanion) {
                flashingCompanion = false
                // its link is gone after the flash: no stale connected-companion state behind Back
                if (!flashStandalone) clearDeviceSession()
                devices.clear()
                step = if (flashReturn == Step.OFFLINE) Step.OFFLINE else Step.COMPANION
            } else {
                // back where it was opened: the repeater's screen keeps its login and route
                flashTarget = FlashTarget.REPEATER
                flashImage = null
                step = flashReturn
            }
            if (step == Step.OFFLINE) loadOfflineData()
        } else if (flashDone) {
            backToRepeaters()   // it restarted with other firmware: log in again
        }
    }

    /**
     * Launches the DFU / Wi-Fi Flash screen directly from `Offline -> Firmware` with [f] preloaded,
     * allowing field flashing of any nRF52 (in bootloader `*_OTA`) or ESP32 (`MeshCore-OTA` AP)
     * without requiring a connected companion.
     */
    fun startDirectOfflineFlash(f: FirmwareFile, standalone: Boolean = false) = work {
        // the DFU file when there is one; the LoRa-OTA .bin only as a Wi-Fi image of an ESP32
        if (!f.hasDfu && !f.hasLoraOta) throw Exception(str(R.string.err_flash_need_dfu))
        val bytes = withContext(Dispatchers.IO) {
            Releases.readCachedFile(f, dfu = f.hasDfu, cache = ctx.filesDir)
                ?: if (f.hasDfu) Releases.downloadDfu(f, ctx.filesDir) else Releases.download(f, ctx.filesDir)
        }
        // the target is not the logged-in repeater, so the image itself decides (ESP32 magic byte)
        if (!f.hasDfu && !f.plainBinFlashable(null, Releases.isEsp32Image(bytes))) {
            throw Exception(str(R.string.err_flash_need_dfu))
        }
        val name = (if (f.hasDfu) f.dfu else f.file).ifEmpty { "${f.env}.bin" }
        flashReturn = step
        flashStandalone = standalone
        flashTarget = FlashTarget.COMPANION
        flashSource = Source.RELEASES
        flashUpstream = false
        flashPlan = null
        flashPercent = -1
        flashDone = false
        flashImage = FlashImage(name, bytes, f.env, f.version.ifEmpty { f.release })
        step = Step.FLASH
    }

    /** How many firmware with LoRa OTA are for other boards than the one the repeater runs (hidden from the list); 0 if its board is unknown. */
    fun otherBoardCount(): Int {
        val env = oldImage?.env ?: allFirmware.firstOrNull { it.sha256 == oldImage?.sha }?.env ?: return 0
        return allFirmware.count { it.hasLoraOta && it.env != env }
    }

    /** Releases that fit: the right role, and for a repeater whose chip is known the right file type. */
    fun flashCandidates(): List<FirmwareFile> {
        val platform = info?.platform
        val running = oldImage?.env
        val offlineFlash = flashTarget == FlashTarget.COMPANION && flashCompanionDevice == null
        val selectedEnv = flashImage?.env
        return (if (flashUpstream) upstream else allFirmware).filter {
            if (offlineFlash) {
                it.hasDfu || it.plainBinFlashable(platform)
            } else {
                it.hasDfu && when (flashTarget) {
                    FlashTarget.COMPANION -> BoardCheck.fitsCompanion(it.env) && it.dfuIsZip
                    FlashTarget.REPEATER -> BoardCheck.role(it.env) == Role.REPEATER && when (platform) {
                        OtaProtocol.PLATFORM_NRF52 -> it.dfuIsZip
                        OtaProtocol.PLATFORM_ESP32 -> !it.dfuIsZip
                        else -> true
                    }
                }
            }
        }.sortedBy {
            // the board it runs now (or the preselected board in offline flash) first
            val same = when {
                offlineFlash -> selectedEnv != null && it.env == selectedEnv
                flashTarget == FlashTarget.COMPANION -> BoardCheck.matches(it.env, companionModel) == true
                else -> it.env == running
            }
            if (same) 0 else 1
        }
    }

    /** 'Ours' / 'Upstream' above the releases of the flash screen. */
    fun showUpstream(on: Boolean) {
        flashUpstream = on
        if (on) refreshUpstreamIfStale() else refreshOursIfStale()
    }

    /** The same for the upstream list: one load at a time. */
    fun loadUpstream(): Job {
        upstreamJob?.takeIf { it.isActive }?.let { return it }
        return doLoadUpstream().also { upstreamJob = it }
    }

    private fun doLoadUpstream() = viewModelScope.launch {
        Releases.apiToken = tokens.get()
        upstreamError = null
        val list = runCatching { withContext(Dispatchers.IO) { Releases.list(Releases.UPSTREAM_REPO, ctx.filesDir) } }
        upstream = list.getOrNull() ?: withContext(Dispatchers.IO) { Releases.cached(Releases.UPSTREAM_REPO, ctx.filesDir) }
        if (list.isSuccess) upstreamLoadedAt = System.currentTimeMillis()
        if (list.isFailure) {
            val why = releasesWhy(list.exceptionOrNull())
            upstreamError = why
            message = str(if (upstream.isEmpty()) R.string.msg_releases_failed else R.string.msg_releases_offline, why)
            messageIsError = upstream.isEmpty()
        }
    }

    fun pickFlashRelease(f: FirmwareFile) = work {
        if (!f.hasDfu && !f.plainBinFlashable(info?.platform)) throw Exception(str(R.string.err_flash_need_dfu))
        val data = withContext(Dispatchers.IO) {
            if (f.hasDfu) Releases.downloadDfu(f, ctx.filesDir) else Releases.download(f, ctx.filesDir)
        }
        flashImage = FlashImage(if (f.hasDfu) f.dfu else f.file, data, f.env, f.version.ifEmpty { f.release })
    }

    fun pickFlashFile(uri: Uri) = work {
        val data = withContext(Dispatchers.IO) { ctx.contentResolver.openInputStream(uri)!!.use { it.readBytes() } }
        val name = fileName(uri)
        flashImage = if (name.endsWith(".uf2", ignoreCase = true)) {
            // the USB drive's file: made into the Bluetooth DFU .zip here
            val zip = runCatching { withContext(Dispatchers.Default) { io.github.n3tk0.loraota.flash.Uf2.toDfuZip(data) } }
                .getOrElse {
                    if (it is CancellationException) throw it
                    throw Exception(str(R.string.err_flash_uf2, it.message ?: ""))
                }
            FlashImage(name.dropLast(4) + ".zip", zip, null)
        } else FlashImage(name, data, null)
    }

    /** 'Flash': checks the file, wakes the repeater's DFU mode (or scans directly if offline) and asks before writing. */
    fun flashCheck() = work {
        val img = flashImage ?: return@work
        val warnings = ArrayList<String>()
        val zipData = img.data.size > 2 && img.data[0] == 'P'.code.toByte() && img.data[1] == 'K'.code.toByte()
        if (img.isZip != zipData) throw Exception(str(R.string.err_flash_bad_file))
        if (img.name.endsWith("-merged.bin", ignoreCase = true)) throw Exception(str(R.string.err_flash_need_bin))
        val env = img.env
        if (env == null) warnings.add(str(R.string.warn_board_unknown))

        if (flashTarget == FlashTarget.COMPANION) {
            val dev = flashCompanionDevice
            if (dev != null) {
                if (!img.isZip) throw Exception(str(R.string.err_flash_need_zip))
                if (env != null && !BoardCheck.fitsCompanion(env)) warnings.add(str(R.string.warn_not_companion, env))
                if (env != null && BoardCheck.matches(env, companionModel) == false) {
                    warnings.add(str(R.string.warn_board, companionModel, env))
                }
                autoBackup(DeviceKind.COMPANION)?.let { warnings.add(it) }
                flashPlan = FlashPlan(img, "$companionName (${dev.address})", dev.address, warnings, companionName)
                return@work
            }
            // Standalone offline flash (no companion connected):
            // - ESP32 .bin -> connects directly to the device's "MeshCore-OTA" Wi-Fi access point
            // - nRF52 .zip -> scans directly over BLE for a device advertising in DFU mode (*_OTA / DfuTarg)
            if (!img.isZip) {
                warnings.add(str(R.string.warn_wifi))
                flashPlan = FlashPlan(img, WifiOta.SSID, null, warnings)
                return@work
            }
            working = str(R.string.working_scan)
            val devices = BleDfu.scan(ctx, 12_000)
            val d = devices.firstOrNull() ?: throw Exception(str(R.string.err_no_dfu))
            if (devices.size > 1) warnings.add(str(R.string.warn_many_dfu, devices.size, d.name))
            if (env != null && BoardCheck.matches(env, d.name) == false) warnings.add(str(R.string.warn_board, d.name, env))
            flashPlan = FlashPlan(img, "${d.name} (${d.address})", d.address, warnings, d.name)
            return@work
        }

        val r = repeater ?: return@work
        when (info?.platform) {
            OtaProtocol.PLATFORM_NRF52 -> if (!img.isZip) throw Exception(str(R.string.err_flash_need_zip))
            OtaProtocol.PLATFORM_ESP32 -> if (img.isZip) throw Exception(str(R.string.err_flash_need_bin))
        }
        if (env != null && BoardCheck.role(env) != Role.REPEATER) warnings.add(str(R.string.warn_not_repeater, env))
        val running = oldImage?.env
        if (env != null && running != null && env != running) warnings.add(str(R.string.warn_not_running, running, env))
        // before `start ota`: afterwards the repeater is in its bootloader
        autoBackup(DeviceKind.REPEATER)?.let { warnings.add(it) }

        if (!img.isZip) {
            // ESP32: nothing tells the board before the write
            if (running == null) warnings.add(str(R.string.warn_wifi))
            flashPlan = FlashPlan(img, WifiOta.SSID, null, warnings)
            return@work
        }
        // nRF52: `start ota` over LoRa, then the repeater shows up over Bluetooth as <BOARD>_OTA
        var devices = emptyList<io.github.n3tk0.loraota.flash.DfuDevice>()
        try {
            for (attempt in 1..2) {
                working = str(R.string.working_start_ota)
                companion.sendRemoteCli(r, "start ota")
                working = str(R.string.working_scan)
                devices = BleDfu.scan(ctx, if (attempt == 1) 12_000 else 15_000)
                if (devices.isNotEmpty()) break
            }
        } catch (e: kotlinx.coroutines.CancellationException) {
            leaveDfuMode()   // Stop or Back while it may already be in DFU mode
            throw e
        }
        val d = devices.firstOrNull() ?: run {
            leaveDfuMode()   // it may have got the command and only be out of Bluetooth range
            throw Exception(str(R.string.err_no_dfu))
        }
        if (devices.size > 1) warnings.add(str(R.string.warn_many_dfu, devices.size, d.name))
        if (env != null && BoardCheck.matches(env, d.name) == false) warnings.add(str(R.string.warn_board, d.name, env))
        flashPlan = FlashPlan(img, "${d.name} (${d.address})", d.address, warnings, d.name)
    }

    /** 'Cancel' on the dialog: an nRF52 repeater already advertises its DFU mode, a restart ends that. */
    fun flashCancel() {
        val p = flashPlan ?: return
        flashPlan = null
        if (flashTarget == FlashTarget.REPEATER && p.address != null) leaveDfuMode()
    }

    private fun leaveDfuMode() {
        val r = repeater ?: return
        viewModelScope.launch { runCatching { companion.sendRemoteCli(r, "reboot") } }
    }

    fun flashStart() {
        val p = flashPlan ?: return
        flashPlan = null
        work {
            flashPercent = 0
            try {
                if (p.address != null) flashBle(p) else flashWifi(p.image)
            } catch (e: FlashFailed) {
                throw Exception(str(R.string.err_flash, e.message ?: ""))
            } finally {
                flashPercent = -1
            }
            flashDone = true
            message = str(if (flashTarget == FlashTarget.COMPANION && flashCompanionDevice != null) R.string.msg_flash_done_companion else R.string.msg_flash_done)
        }
    }

    private suspend fun flashBle(p: FlashPlan) {
        val address = p.address ?: return
        val zip = java.io.File(ctx.cacheDir, "flash.zip")
        withContext(Dispatchers.IO) { zip.writeBytes(p.image.data) }
        val isConnectedCompanion = flashTarget == FlashTarget.COMPANION && flashCompanionDevice != null
        if (isConnectedCompanion) {
            // the DFU library needs the device to itself
            flashingCompanion = true
            link.closeAndRefresh()
            delay(1500)
        }
        working = str(R.string.working_flash)
        val t0 = System.currentTimeMillis()
        flashLog = emptyList()
        val log = { m: String ->
            val line = "%.1f %s".format(java.util.Locale.ROOT, (System.currentTimeMillis() - t0) / 1000.0, m)
            viewModelScope.launch { flashLog = (flashLog + line).takeLast(300) }
            Unit
        }
        log("${p.name} $address, ${p.image.data.size} B")
        BleDfu.flash(ctx, address, p.name, zip, isConnectedCompanion, log) { flashPercent = it }
    }

    private suspend fun flashWifi(img: FlashImage) {
        val r = repeater
        if (r != null) {
            working = str(R.string.working_start_ota)
            companion.sendRemoteCli(r, "start ota")
            if (info == null) {
                // a stock repeater does not say its chip: an nRF52 shows up over Bluetooth instead of Wi-Fi
                working = str(R.string.working_scan)
                if (BleDfu.scan(ctx, 8_000).isNotEmpty()) {
                    leaveDfuMode()
                    throw Exception(str(R.string.err_flash_is_nrf))
                }
            } else delay(5000)   // the access point comes up
        }
        working = str(R.string.working_wifi)
        val (network, release) = WifiOta.join(ctx, 60_000)
            ?: throw Exception(str(if (Build.VERSION.SDK_INT < 29) R.string.err_no_wifi_manual else R.string.err_no_wifi))
        try {
            working = str(R.string.working_flash)
            WifiOta.upload(network, img.data) { flashPercent = it }
        } finally {
            release()
        }
    }

    // ---- a queue: several repeaters updated one after another ----

    private val queuePrefs get() = ctx.getSharedPreferences("ota_queue", Context.MODE_PRIVATE)
    /** The queue being run or cut short (kept on the phone), or the last one finished (its report). */
    var queue by mutableStateOf(OtaQueueState.fromJson(queuePrefs.getString("state", null)))
    val queuePicked = mutableStateMapOf<String, Boolean>()   // repeaters ticked for a new queue, by key
    var queueRelease by mutableStateOf<String?>(null)
    var queueCurrent by mutableStateOf<QueueEntry?>(null)
    private var queueReturn = Step.REPEATER

    private fun saveQueue() {
        val q = queue
        queuePrefs.edit().apply { if (q == null) remove("state") else putString("state", q.toJson()) }.apply()
    }

    fun openQueue() {
        queueReturn = step
        message = ""
        step = Step.QUEUE
        refreshOursIfStale()
    }

    fun closeQueue() {
        job?.cancel()
        queueAsk = false   // the confirmation must not come back by itself when the page is opened again
        step = queueReturn
    }

    /** The queue's choices: releases with LoRa OTA files, (release, label). */
    fun queueReleases() = OtaQueueState.releases(allFirmware)

    /** Repeaters the queue can take: those with a password kept from an earlier login, nearest first. */
    fun queueCandidates(): List<Contact> = OtaQueueState.order(repeaters.filter { hasSavedPassword(it) }, { it.hops }, { it.name })

    /** Repeaters it cannot take yet (no kept password): shown greyed, with how to fix it. */
    fun queueNoPassword(): List<Contact> = repeaters.filter { !hasSavedPassword(it) }.sortedBy { it.name.lowercase() }

    /** The confirmation before a new queue starts (the list, the release, that it takes hours). */
    var queueAsk by mutableStateOf(false)

    fun startQueue() {
        queueAsk = false
        val rel = queueRelease ?: return
        val label = queueReleases().firstOrNull { it.first == rel }?.second ?: rel
        val picked = queueCandidates().filter { queuePicked[hex(it.publicKey)] == true }
        if (picked.isEmpty()) return
        queue = OtaQueueState(rel, label, picked.map { QueueEntry(hex(it.publicKey), it.name) })
        saveQueue()
        runQueue()
    }

    fun retryQueue() {
        val q = queue ?: return
        q.retryFailed()
        queue = OtaQueueState(q.release, q.label, q.entries)
        saveQueue()
        runQueue()
    }

    /** A finished (or unwanted) queue: gone, the screen is back to picking. */
    fun clearQueue() {
        queue = null
        saveQueue()
    }

    fun runQueue() = work {
        val q = queue ?: return@work
        if (!companionOta) throw Exception(str(R.string.q_stock_companion))
        if (releases.isEmpty()) loadReleases().join()
        // one foreground service (and wake lock) for the whole queue: the next repeater's login, download and
        // backup run with the screen off too, and Android does not let an app in the background start one again
        OtaService.start(ctx, notifyTitle(), str(R.string.phase_start))
        try {
            while (true) {
                val e = q.next ?: break
                queueCurrent = e
                val (result, note) = queueOne(q, e)
                e.result = result
                e.note = note
                queue = OtaQueueState(q.release, q.label, q.entries)   // a new object: the screen redraws
                saveQueue()
            }
        } finally {
            queueCurrent = null
            repeater = null
            info = null
            phase = null
            OtaService.stop(ctx)
        }
        message = str(R.string.q_report, q.count(QueueResult.DONE), q.count(QueueResult.CURRENT),
            q.count(QueueResult.SKIPPED), q.count(QueueResult.FAILED))
        messageIsError = q.count(QueueResult.FAILED) > 0
    }

    /** One repeater: log in, check, back up, update. Stop (a cancel) leaves it pending, to go on with later. */
    private suspend fun queueOne(q: OtaQueueState, e: QueueEntry): Pair<QueueResult, String> {
        val c = repeaters.firstOrNull { hex(it.publicKey) == e.key } ?: return QueueResult.SKIPPED to str(R.string.q_not_found)
        val pw = savedPassword(c) ?: return QueueResult.SKIPPED to str(R.string.q_no_password)
        repeater = c
        info = null
        sent = 0
        total = 0
        log.clear()
        val learned = try {
            signIn(c, pw)
        } catch (x: kotlinx.coroutines.CancellationException) {
            throw x
        } catch (x: Exception) {
            return QueueResult.FAILED to (x.message ?: x.toString())
        }
        val r = learned ?: c
        repeater = r
        try {
            if (learned?.hops != 0) return QueueResult.SKIPPED to str(R.string.q_not_direct)
            working = str(R.string.working_ota)
            val i = try {
                session(r).info(OTA_CHECK_MS, 2)
            } catch (x: OtaFailed) {
                return QueueResult.SKIPPED to if (x.res == R.string.err_no_answer) str(R.string.q_no_ota) else str(x.res, *x.args.toTypedArray())
            }
            info = i
            if (!i.enabled) return QueueResult.SKIPPED to str(R.string.q_ota_off)
            val sha = hex(i.imageSha)
            // not in the loaded lists: the older releases too (the find is kept for the next repeater)
            val running = allFirmware.firstOrNull { it.sha256 == sha } ?: run {
                val older = runCatching { withContext(Dispatchers.IO) { Releases.findOlder(repo, sha, ctx.filesDir) } }
                    .also { it.exceptionOrNull()?.let { ex -> if (ex is CancellationException) throw ex } }
                    .getOrNull()
                (older as? Releases.SearchResult.Found)?.file?.also { addOlderFound(it) }
            } ?: return QueueResult.SKIPPED to str(R.string.q_unknown_firmware)
            val target = OtaQueueState.target(allFirmware, q.release, running)
                ?: return QueueResult.SKIPPED to str(R.string.q_no_file, running.env)
            if (target.sha256 == running.sha256) return QueueResult.CURRENT to running.label
            val old = withContext(Dispatchers.IO) { Releases.download(running, ctx.filesDir) }
            val new = withContext(Dispatchers.IO) { Releases.download(target, ctx.filesDir) }
            if (!i.fits(new.size)) return QueueResult.SKIPPED to str(R.string.msg_too_big)
            // a power cut while it copies in place needs a visit: only one at a time, by hand
            if (i.inPlace(new.size)) return QueueResult.SKIPPED to str(R.string.q_in_place)
            oldImage = Image(running.file, old, running.env)
            newImage = Image(target.file, new, target.env)
            val p = withContext(Dispatchers.Default) { DeltaGenerator.create(old, new) }
            patch = p
            val noBackup = autoBackup(DeviceKind.REPEATER)
            val after = try {
                session(r).run(old, new, p, false)
            } catch (x: OtaFailed) {
                val why = str(x.res, *x.args.toTypedArray())
                return QueueResult.FAILED to if (x.resumable) str(R.string.q_failed_resumable, why) else why
            } finally {
                phase = null
            }
            val ident = after.ident.replace('|', ' ').ifEmpty { target.label }
            return QueueResult.DONE to (if (noBackup != null) "$ident · $noBackup" else ident)
        } catch (x: kotlinx.coroutines.CancellationException) {
            throw x
        } catch (x: io.github.n3tk0.loraota.ble.CompanionError) {
            throw x   // the companion is gone: the queue stops here and goes on later
        } catch (x: Exception) {
            // a download that broke off, a repeater that answered oddly: this one failed, the next one goes on
            return QueueResult.FAILED to (x.message ?: x.toString())
        } finally {
            withContext(NonCancellable) { runCatching { companion.restorePath(r) } }
            oldImage = null
            newImage = null
            patch = null
        }
    }

    // ---- health: the repeater's self test, and the telemetry history of either device ----

    private val healthStore by lazy { HealthStore(ctx.filesDir) }
    var healthKind by mutableStateOf(DeviceKind.REPEATER)
    var selfTest by mutableStateOf<SelfTest?>(null)
    var selfTestMissing by mutableStateOf(false)   // the repeater's firmware has no `selftest`
    var healthWarnings by mutableStateOf<List<HealthWarning>>(emptyList())
    var healthRecords by mutableStateOf<List<HealthStore.Record>>(emptyList())
    var historyField by mutableStateOf(HistorySeries.F_BATT_PCT)
    var historyRing by mutableStateOf(HistorySeries.RING_DAY)
    /** Series read since the page opened, by (ring, field); a field the node does not measure has only nulls. */
    val historyCache = mutableStateMapOf<Pair<Int, Int>, HistorySeries>()
    /** Currently loading (ring, field) pair, null when idle. */
    var loadingHistoryKey by mutableStateOf<Pair<Int, Int>?>(null)
    /** Pending (ring, field) pairs queued while another chart is loading. */
    val queuedHistoryKeys = mutableStateListOf<Pair<Int, Int>>()
    /** Series the device did not answer: not asked again until an explicit refresh. */
    private val failedHistoryKeys = HashSet<Pair<Int, Int>>()
    /** Incremented on openHealth/refreshHealth so UI resets expanded chart cards. */
    var healthGeneration by mutableStateOf(0)
    var historyMissing by mutableStateOf(false)    // the node keeps no history (stock or older firmware)
    var battSave by mutableStateOf<BattSave?>(null)        // the repeater's `battsave` (null: not read / older firmware)
    var battCapacity by mutableStateOf<BattCapacity?>(null)
    var battGauge by mutableStateOf(false)                  // an INA219 counts the charge: capacity matters, voltage is exact
    var battChecked by mutableStateOf(false)                // the person says the voltage reading was checked
    private var healthReturn = Step.UPDATE

    val healthDeviceName get() = if (healthKind == DeviceKind.COMPANION) companionName else repeater?.name ?: ""

    private fun resetHealthState(kind: DeviceKind) {
        healthKind = kind
        selfTest = null
        selfTestMissing = false
        healthWarnings = emptyList()
        historyCache.clear()
        failedHistoryKeys.clear()
        queuedHistoryKeys.clear()
        loadingHistoryKey = null
        healthGeneration++
        historyMissing = false
        historyField = HistorySeries.F_BATT_PCT
        historyRing = HistorySeries.RING_DAY
        battSave = null
        battCapacity = null
        battGauge = false
        battChecked = false
        message = ""
        healthRecords = emptyList()
    }

    fun openHealth(kind: DeviceKind) {
        healthReturn = step
        resetHealthState(kind)
        step = Step.HEALTH
        refreshHealth()
    }

    fun closeHealth() {
        job?.cancel()
        queuedHistoryKeys.clear()
        loadingHistoryKey = null
        if (step == Step.UPDATE) {
            repeaterTab = RepeaterTab.FIRMWARE
        } else {
            step = healthReturn
        }
    }

    /** The self test and battery config (for a repeater); telemetry charts are fetched on demand when opened. */
    fun refreshHealth() = work {
        val kind = healthKind
        val dev = deviceKey(kind)
        healthRecords = withContext(Dispatchers.IO) { healthStore.records(dev) }
        val warnings = ArrayList<HealthWarning>()
        if (kind == DeviceKind.REPEATER) {
            val r = repeater ?: return@work
            if (!RepeaterBackup.supported(companion)) {
                canReadRepeater = false
            } else {
                canReadRepeater = true
                working = str(R.string.working_selftest)
                val line = try {
                    val l = RepeaterBackup.ask(companion, r, SelfTest.COMMAND)?.removePrefix("> ")?.trim()
                    // "day=" (mAh in/out) only comes with an INA219: no capacity to ask for without one
                    SelfTest.parse(l ?: "")?.let { readBattery(r, hasGauge = "day" in it.values) }
                    l
                } finally {
                    withContext(NonCancellable) { runCatching { companion.captureCli(0) } }
                }
                val t = line?.let { SelfTest.parse(it) }
                if (line == null) throw Exception(str(R.string.health_no_answer))
                if (t == null) {
                    selfTestMissing = true
                } else {
                    val before = healthRecords.lastOrNull()?.let { SelfTest.parse(it.line) }
                    healthRecords = withContext(Dispatchers.IO) {
                        healthStore.add(dev, line)
                        healthStore.records(dev)
                    }
                    healthRecords.lastOrNull()?.let { fleetHealth[dev] = it }
                    selfTest = t
                    warnings.addAll(HealthCheck.warnings(t, before))
                }
            }
            repeaterHealthLoadedFor = dev
        }
        healthWarnings = warnings.toList()
        historyCache.clear()
        failedHistoryKeys.clear()
        queuedHistoryKeys.clear()
        loadingHistoryKey = null
        healthGeneration++
        // a repeater without `selftest` (older or stock firmware) has no history either
        historyMissing = selfTestMissing
    }

    /** `battsave` and `battery capacity`, with the console capture already on. */
    private suspend fun readBattery(r: Contact, hasGauge: Boolean) {
        working = str(R.string.working_battery)
        val b = RepeaterBackup.ask(companion, r, BattSave.COMMAND)?.let { BattSave.parse(it) }
        battSave = b
        battCapacity = if (hasGauge) RepeaterBackup.ask(companion, r, BattCapacity.COMMAND)?.let { BattCapacity.parse(it) } else null
        battGauge = battCapacity != null
        // the INA219 measures the cell itself; an 'off' step set before means the reading was checked then
        if (battGauge || (b?.off ?: 0) > 0) battChecked = true
    }

    /**
     * Commands to the repeater's console, then what they changed read again (`battsave` only; the
     * capacity is known from the command); a reply other than OK is shown.
     */
    private fun battCommands(vararg commands: String, capacity: Int? = null) = work {
        val r = repeater ?: return@work
        try {
            for (c in commands) {
                working = c
                val reply = RepeaterBackup.ask(companion, r, c)?.removePrefix("> ")?.trim()
                    ?: throw Exception(str(R.string.health_no_answer))
                if (!reply.startsWith("OK")) throw Exception(reply)
            }
            if (capacity != null) {
                battCapacity = BattCapacity(capacity, learnt = false)
            } else {
                working = str(R.string.working_battery)
                RepeaterBackup.ask(companion, r, BattSave.COMMAND)?.let { BattSave.parse(it) }?.let { battSave = it }
            }
        } finally {
            withContext(NonCancellable) { runCatching { companion.captureCli(0) } }
        }
        message = str(R.string.batt_saved)
    }

    fun applyBattSave() {
        val set = BattSave.command(BattSave.recommended(battChecked))
        if (battSave?.enabled == true) battCommands(set) else battCommands(set, "${BattSave.COMMAND} on")
    }
    fun setBattSave(on: Boolean) = battCommands("${BattSave.COMMAND} ${if (on) "on" else "off"}")
    fun setBattCapacity(mah: Int) = battCommands("${BattCapacity.COMMAND} $mah", capacity = mah)

    fun showHistory(ring: Int, field: Int) {
        historyRing = ring
        historyField = field
        failedHistoryKeys.remove(ring to field)
        loadHistoryField(ring, field)
    }

    /** Enqueues a chart series to be fetched from the device; runs sequentially if another chart is already loading. */
    fun loadHistoryField(ring: Int, field: Int) {
        val key = ring to field
        if (historyMissing || historyCache.containsKey(key) || loadingHistoryKey == key || key in failedHistoryKeys) return
        if (busy && loadingHistoryKey == null) return
        if (key !in queuedHistoryKeys) queuedHistoryKeys.add(key)
        if (!busy) drainHistoryQueue()
    }

    /** Writes a telemetry CSV export on Dispatchers.IO with truncate mode ("wt"), failing explicitly on null stream. */
    fun exportHistoryCsv(series: HistorySeries, label: String, unit: String, uri: android.net.Uri) = work {
        val csv = io.github.n3tk0.loraota.tools.TelemetryCsv.export(healthDeviceName, label, unit, series)
        withContext(Dispatchers.IO) {
            ctx.contentResolver.openOutputStream(uri, "wt")?.use { it.write(csv.toByteArray(Charsets.UTF_8)) }
                ?: throw Exception(str(R.string.offline_fw_export_failed))
        }
        message = str(R.string.health_csv_saved)
    }

    /** Switches 24h / week ring and fetches any currently expanded charts that are not yet cached. */
    fun setHistoryRing(ring: Int, openFields: Collection<Int>) {
        historyRing = ring
        if (historyMissing) return
        queuedHistoryKeys.removeAll { it.first != ring }
        for (f in openFields) {
            val key = ring to f
            if (!historyCache.containsKey(key) && loadingHistoryKey != key && key !in queuedHistoryKeys && key !in failedHistoryKeys) {
                queuedHistoryKeys.add(key)
            }
        }
        if (queuedHistoryKeys.isNotEmpty() && !busy) drainHistoryQueue()
    }

    private fun drainHistoryQueue() = work {
        working = str(R.string.working_history)
        try {
            while (queuedHistoryKeys.isNotEmpty() && !historyMissing) {
                val key = queuedHistoryKeys.removeAt(0)
                if (historyCache.containsKey(key)) continue
                loadingHistoryKey = key
                val s = loadSeries(key.first, key.second)
                if (s != null) checkFasterDrain() else failedHistoryKeys.add(key)
            }
        } finally {
            loadingHistoryKey = null
            queuedHistoryKeys.clear()
        }
    }

    private fun checkFasterDrain() {
        if (HealthWarning.FAST_DRAIN in healthWarnings) return
        val day = historyCache[HistorySeries.RING_DAY to HistorySeries.F_BATT_PCT] ?: return
        val week = historyCache[HistorySeries.RING_WEEK to HistorySeries.F_BATT_PCT] ?: return
        if (HealthCheck.fasterDrain(day.values, week.values)) {
            healthWarnings = healthWarnings + HealthWarning.FAST_DRAIN
        }
    }

    private suspend fun loadSeries(ring: Int, field: Int): HistorySeries? {
        historyCache[ring to field]?.let { return it }
        val kind = healthKind
        val s = HistorySeries.load(ring, field) { start, max ->
            when (kind) {
                DeviceKind.COMPANION -> companion.history(HistorySeries.args(ring, field, start, max))
                DeviceKind.REPEATER -> {
                    val r = repeater ?: return@load null
                    // a piece lost on the air is asked for once more
                    var reply: ByteArray? = null
                    for (attempt in 1..2) {
                        val tag = companion.sendRequest(r, HistorySeries.request(ring, field, start, max))
                        reply = companion.awaitResponse(tag, 12_000)
                        if (reply != null) break
                    }
                    reply
                }
            }
        } ?: return null
        historyCache[ring to field] = s
        return s
    }

    // ---- backups of the settings ----

    /** The device the backup screen is for: its public key (hex), "" when not connected. */
    private fun deviceKey(kind: DeviceKind): String = when (kind) {
        DeviceKind.COMPANION -> companion.selfInfo.takeIf { it.size >= 36 }?.let { hex(it.copyOfRange(4, 36)) } ?: ""
        DeviceKind.REPEATER -> repeater?.let { hex(it.publicKey) } ?: ""
    }

    val backupDevice get() = deviceKey(backupKind)

    val backupDeviceName get() = if (backupKind == DeviceKind.COMPANION) companionName else repeater?.name ?: ""

    fun openBackup(kind: DeviceKind) {
        backupReturn = step
        backupKind = kind
        restoreFrom = null
        message = ""
        step = Step.BACKUP
        loadBackups()
    }

    fun closeBackup() {
        restoreFrom = null
        importText = null
        exportingBackup = null
        exportPassword = ""
        step = backupReturn
    }

    private var backupsJob: Job? = null

    /** This device's backups first, then those of others of its kind (e.g. for a board that replaces one). */
    private fun loadBackups() {
        val dev = backupDevice
        val kind = backupKind
        backupsJob?.cancel()
        backupsJob = viewModelScope.launch {
            // the keys are decrypted as they are read: not on the main thread
            val all = withContext(Dispatchers.IO) { backupStore.all() }
            if (backupKind == kind) {
                backups = all.filter { it.kind == kind }.sortedBy { if (it.deviceKey == dev) 0 else 1 }
            }
        }
    }

    fun backupNow() = work { makeBackup(backupKind, false) ; message = str(R.string.msg_backup_done) }

    private suspend fun makeBackup(kind: DeviceKind, auto: Boolean): Backup {
        val b = when (kind) {
            DeviceKind.COMPANION -> {
                working = str(R.string.working_backup_companion)
                val items = CompanionBackup.read(companion)
                Backup(kind, deviceKey(kind), companionName, System.currentTimeMillis(), auto,
                    listOf(companionModel, companionVersion).filter { it.isNotEmpty() }.joinToString(" · "),
                    items, CompanionBackup.privateKey(companion))
            }
            DeviceKind.REPEATER -> {
                val r = repeater ?: throw Exception(str(R.string.msg_disconnected))
                if (!RepeaterBackup.supported(companion)) {
                    canReadRepeater = false
                    throw Exception(str(R.string.backup_no_capture))
                }
                canReadRepeater = true
                val items = readRepeater(r)
                Backup(kind, deviceKey(kind), r.name, System.currentTimeMillis(), auto, info?.ident?.replace('|', ' ') ?: "", items)
            }
        }
        val saved = withContext(Dispatchers.IO) { backupStore.save(b) }
        if (step == Step.BACKUP) loadBackups()
        return saved
    }

    /** Its console settings, and the admin password the app logged in with (the repeater does not give it). */
    private suspend fun readRepeater(r: Contact): List<BackupItem> {
        val items = try {
            RepeaterBackup.read(companion, r, info != null) { i, n -> working = str(R.string.working_backup_repeater, i, n) }
        } catch (e: NoAnswer) {
            throw Exception(str(R.string.backup_no_answer))
        }
        val pw = savedPassword(r) ?: return items
        return items + BackupItem(GROUP_SETTING, RepeaterBackup.PASSWORD, RepeaterBackup.PASSWORD, pw)
    }

    /**
     * Before every flash or update: a backup. Null when it is made; else why not (it goes on the
     * 'are you sure', the flash itself still can go).
     */
    private suspend fun autoBackup(kind: DeviceKind): String? {
        if (deviceKey(kind).isEmpty()) return null
        return try {
            makeBackup(kind, true)
            null
        } catch (e: kotlinx.coroutines.CancellationException) {
            throw e
        } catch (e: Exception) {
            str(R.string.warn_no_backup, e.message ?: "")
        }
    }

    /** 'Restore': reads the device now and shows what differs, all ticked. */
    fun startRestore(b: Backup) = work {
        val now = when (b.kind) {
            DeviceKind.COMPANION -> {
                working = str(R.string.working_backup_companion)
                CompanionBackup.read(companion)
            }
            DeviceKind.REPEATER -> {
                val r = repeater ?: return@work
                if (!RepeaterBackup.supported(companion)) throw Exception(str(R.string.backup_no_capture))
                readRepeater(r)
            }
        }
        // the identity only with its private key (a file imported without its password has none)
        val rows = BackupDiff.diff(b.items, now).filter { it.item.group != BackupItem.GROUP_KEY || b.privateKey != null }
        restoreChecked.clear()
        // the identity only when the user ticks it: it changes who the device is for the network
        for (r in rows) restoreChecked[r.item.ref] = r.item.group != BackupItem.GROUP_KEY
        restoreRows = rows
        restoreFrom = b
        if (rows.isEmpty()) message = str(R.string.restore_same)
    }

    fun closeRestore() {
        restoreFrom = null
        restoreRows = emptyList()
    }

    fun applyRestore() = work {
        val b = restoreFrom ?: return@work
        val items = restoreRows.map { it.item }.filter { restoreChecked[it.ref] == true }
        if (items.isEmpty()) return@work
        working = str(R.string.working_restore)
        val res: RestoreResult = when (b.kind) {
            DeviceKind.COMPANION -> {
                val r = CompanionBackup.apply(companion, items, b.privateKey)
                // a new identity or name: the companion as it is now
                runCatching {
                    companion.start()
                    companionName = companion.nodeName.ifEmpty { companionName }
                    loadRepeaters()
                }
                r
            }
            DeviceKind.REPEATER -> {
                val r = repeater ?: return@work
                val res = RepeaterBackup.apply(companion, r, items) { i, n -> working = str(R.string.working_restore_repeater, i, n) }
                // the next login is with the restored admin password
                items.firstOrNull { it.id == RepeaterBackup.PASSWORD && res.failed.none { f -> f.first.id == it.id } }
                    ?.let { savePassword(r, it.value) }
                res
            }
        }
        restoreFrom = null
        restoreRows = emptyList()
        loadBackups()   // a restored identity makes other backups this device's
        val restart = b.kind == DeviceKind.REPEATER && items.any { it.id in RepeaterBackup.NEEDS_RESTART } &&
            res.failed.none { it.first.id in RepeaterBackup.NEEDS_RESTART }
        message = if (res.failed.isEmpty()) str(R.string.restore_done, res.done)
            else str(R.string.restore_partly, res.done, res.failed.joinToString(", ") { it.first.label.ifEmpty { it.first.id } })
        if (restart) message += "\n" + str(R.string.restore_restart)
        messageIsError = res.failed.isNotEmpty()
    }

    fun deleteBackup(b: Backup) {
        backupStore.delete(b)
        loadBackups()
    }

    /** 'Save as file': with a [password] all of it encrypted; without one no key, channels or passwords. */
    fun exportBackup(b: Backup, uri: Uri, password: String) = work {
        val text = withContext(Dispatchers.Default) { PasswordBox.export(b, password) }
        withContext(Dispatchers.IO) {
            ctx.contentResolver.openOutputStream(uri, "wt")!!.use { it.write(text.toByteArray()) }
        }
        message = str(if (password.isEmpty()) R.string.msg_export_no_key else R.string.msg_export_done)
    }

    /** A backup file: an encrypted one waits for its password ([importText]). */
    fun pickImport(uri: Uri) = work {
        val text = withContext(Dispatchers.IO) { ctx.contentResolver.openInputStream(uri)!!.use { String(it.readBytes()) } }
        val locked = try {
            PasswordBox.needsPassword(text)
        } catch (e: Exception) {
            throw Exception(str(R.string.err_backup_file))
        }
        if (locked) importText = text else importBackup(text, "")
    }

    fun importWithPassword(password: String) = work {
        val text = importText ?: return@work
        importBackup(text, password)
        importText = null
    }

    private suspend fun importBackup(text: String, password: String) {
        val b = try {
            withContext(Dispatchers.Default) { PasswordBox.import(text, password) }
        } catch (e: WrongPassword) {
            throw Exception(str(R.string.err_backup_password))
        } catch (e: Exception) {
            throw Exception(str(R.string.err_backup_file))
        }
        if (b.kind != backupKind) throw Exception(str(R.string.err_backup_kind))
        withContext(Dispatchers.IO) { backupStore.save(b) }
        loadBackups()
        message = str(R.string.msg_import_done, b.deviceName)
    }

    // ---- a new build on GitHub ----

    /** The workflow input the board is picked in: Offline lets the user pick any role (repeater/companion/room_server). */
    val buildRole get() = when {
        step == Step.OFFLINE -> fleetState.offlineBuildRole
        step == Step.FLASH && flashTarget == FlashTarget.COMPANION -> "companion"
        else -> "repeater"
    }

    /** Switches target device role in the Offline -> Firmware custom GitHub build panel. */
    fun selectOfflineBuildRole(role: String) {
        if (fleetState.offlineBuildRole == role) return
        fleetState.offlineBuildRole = role
        buildBoard = ""
        buildBoardAuto = false
        if (buildInputs.any { it.name == role }) {
            buildInputsRole = role
            pickAutoBoard()
        } else if (hasToken) {
            loadBuildForm()
        }
    }

    private fun api(): GitHubApi = GitHubApi(tokens.get() ?: run {
        hasToken = false
        throw Exception(str(R.string.build_err_token))
    }, repo)

    private fun syncTokenFlags() {
        hasToken = tokens.has
        groupToken = tokens.usesGroup
        hasOwnToken = tokens.hasOwn
        hasBuiltInGroupToken = tokens.hasBuiltInGroup
        hasSavedGroupToken = tokens.hasSavedGroup
    }

    /** Checks the token with GitHub (it must read the workflow and its runs), then keeps it encrypted. */
    fun saveToken(token: String) = work {
        val t = token.trim()
        if (t.isEmpty()) return@work
        working = str(R.string.working_token)
        val a = GitHubApi(t, repo)
        try {
            withContext(Dispatchers.IO) {
                a.file(GitHubApi.WORKFLOW_PATH, a.defaultBranch())
                a.runs(GitHubApi.WORKFLOW)
            }
        } catch (e: io.github.n3tk0.loraota.github.GitHubError) {
            throw Exception(str(R.string.build_err_access, e.message ?: ""))
        }
        tokens.set(t)
        syncTokenFlags()
        message = str(R.string.msg_token_saved)
        if (step != Step.SETTINGS) readBuildForm()
    }

    fun forgetToken() {
        tokens.clear()
        syncTokenFlags()
        buildInputs = emptyList()
    }

    fun forgetSavedGroupToken() {
        tokens.clearSavedGroup()
        syncTokenFlags()
        buildInputs = emptyList()
    }

    /** The device the form is for: another repeater (or the companion) gets its own board. */
    private fun buildDevice() = when {
        step == Step.OFFLINE -> "offline:${fleetState.offlineBuildRole}"
        buildRole == "companion" -> companionDevice?.address ?: ""
        else -> repeater?.let { hex(it.publicKey) } ?: ""
    }

    /**
     * The boards and boxes of the workflow on GitHub, once per screen (repeater or companion); the board
     * again for another device, and when the running firmware (which names it) is found later.
     */
    fun loadBuildForm() {
        if (!hasToken) return
        if (buildInputs.isEmpty() || buildInputsRole != buildRole) {
            if (buildInputs.any { it.name == buildRole }) {
                buildInputsRole = buildRole
                pickAutoBoard()
            } else {
                work { readBuildForm() }
            }
        } else if (buildFormFor != buildDevice() || buildBoard.isEmpty() || buildBoardAuto) {
            pickAutoBoard()
        }
    }

    private fun pickAutoBoard() {
        buildFormFor = buildDevice()
        buildBoard = autoBoard(boardChoices())
        buildBoardAuto = buildBoard.isNotEmpty()
    }

    fun reloadBuildForm() = work { readBuildForm() }

    /** A GitHub call of the build form: a refusal says what to do about the token. */
    private suspend fun <T> github(block: () -> T): T = try {
        withContext(Dispatchers.IO) { block() }
    } catch (e: io.github.n3tk0.loraota.github.GitHubError) {
        if (e.code != 401 && e.code != 403 && e.code != 404) throw e
        throw Exception(BuildWatcher.accessError(ctx, e, tokens.usesGroup))
    }

    private suspend fun readBuildForm() {
        working = str(R.string.working_build_form)
        val a = api()
        val inputs = github { WorkflowInputs.parse(a.file(GitHubApi.WORKFLOW_PATH, a.defaultBranch())) }
        if (inputs.none { it.name == buildRole }) throw Exception(str(R.string.build_err_workflow))
        buildInputs = inputs
        buildInputsRole = buildRole
        buildOptions.clear()
        for (i in inputs) if (i.isBoolean) buildOptions[i.name] = i.defaultOn
        pickAutoBoard()
    }

    /** The boards of the workflow's list for this screen. */
    fun boardChoices(): List<String> =
        buildInputs.firstOrNull { it.name == buildRole }?.options.orEmpty().filter { it != "-" }

    /** The board the device is: the one its firmware was built for, or the companion's model. */
    private fun autoBoard(choices: List<String>): String {
        if (buildRole == "room_server") return ""
        if (buildRole == "companion") {
            if (companionModel.isEmpty()) return ""
            return choices.firstOrNull { BoardCheck.fitsCompanion(it) && BoardCheck.matches(it, companionModel) == true } ?: ""
        }
        val sha = info?.imageSha?.let { hex(it) }
        val env = oldImage?.env ?: allFirmware.firstOrNull { it.sha256 == sha || it.sha256 == oldImage?.sha }?.env
        return env?.takeIf { it in choices } ?: ""
    }

    fun pickBuildBoard(board: String) {
        buildBoard = board
        buildBoardAuto = false
    }

    /** 'Build on GitHub': starts the workflow for the board with the boxes as ticked, then follows it. */
    fun startBuild() = work {
        val board = buildBoard
        if (board.isEmpty()) throw Exception(str(R.string.build_no_board))
        if (buildJob?.finished == false) throw Exception(str(R.string.build_busy_other))
        working = str(R.string.working_dispatch)
        val a = api()
        val withId = buildInputs.any { it.name == "request_id" }
        val id = if (withId) hex(kotlin.random.Random.nextBytes(4)) else ""
        val inputs = HashMap<String, String>()
        inputs[buildRole] = board
        for ((k, v) in buildOptions) inputs[k] = v.toString()
        if (tokens.usesGroup) inputs.remove(GROUP_HIDDEN)   // the workflow's default: no pre-release
        if (withId) inputs["request_id"] = id
        val at = System.currentTimeMillis()
        github { a.dispatch(GitHubApi.WORKFLOW, a.defaultBranch(), inputs) }
        BuildWatcher.begin(ctx, BuildJob(repo, buildRole, board, id, at))
    }

    fun closeBuild() = BuildWatcher.dismiss(ctx)

    fun forgetBuild() = BuildWatcher.forget(ctx)

    /** Its files are on the phone now: in the releases list, and chosen when this screen asked for them. */
    private suspend fun buildFinished(j: BuildJob) {
        builds = withContext(Dispatchers.IO) { Releases.builds(ctx.filesDir) }
        refreshCacheStats()
        if (step == Step.OFFLINE) loadOfflineData()
        val made = builds.filter { it.release == FirmwareFile.BUILD_PREFIX + j.runNumber && it.env == j.board }
        if (busy || j.role != buildRole) return
        if (step == Step.UPDATE && firmwareTab == 0 && loraSource == Source.BUILD) {
            val f = made.firstOrNull { it.hasLoraOta && it in newCandidates() } ?: return
            loraSource = Source.RELEASES
            pickRelease(f)
        } else if (flashSource == Source.BUILD && (step == Step.FLASH || (step == Step.UPDATE && firmwareTab == 1))) {
            val f = made.firstOrNull { it.hasDfu } ?: return
            flashSource = Source.RELEASES
            flashUpstream = false
            if (f in flashCandidates()) pickFlashRelease(f)
        }
    }

    // ---- settings, app self-update, and cache ----

    fun openSettings(checkNow: Boolean = false) {
        if (step != Step.SETTINGS) settingsReturn = step
        message = ""
        step = Step.SETTINGS
        refreshCacheStats()
        if (checkNow) checkAppUpdate(manual = true)
    }

    fun closeSettings() {
        fleetState.showFieldPrepDialog = false
        step = settingsReturn
        if (step == Step.OFFLINE) loadOfflineData()
    }

    fun updateAutoCheck(enabled: Boolean) {
        autoUpdateCheck = enabled
        AppPrefs.setAutoUpdateCheck(ctx, enabled)
    }

    private var updateCheckJob: Job? = null
    private var updateDownloadJob: Job? = null

    fun checkAppUpdate(manual: Boolean = true) {
        if (checkingAppUpdate) return
        updateCheckJob?.cancel()
        checkingAppUpdate = true
        if (manual) appUpdateError = null
        updateCheckJob = viewModelScope.launch {
            try {
                val rel = withContext(Dispatchers.IO) { AppUpdater.checkLatest(repo, tokens.get(), includePrerelease = BuildConfig.IS_PRERELEASE) }
                val prevTag = availableUpdate?.tag
                latestAppRelease = rel
                val newer = rel != null && AppUpdater.isNewer(rel.version, BuildConfig.VERSION_NAME)
                availableUpdate = if (newer) rel else null
                if (rel?.tag != prevTag) downloadedApk = null
                if (newer) updateBannerDismissed = false
                appUpdateChecked = true
                appUpdateError = null
                AppPrefs.setLastUpdateCheck(ctx)
            } catch (e: CancellationException) {
                throw e
            } catch (e: Exception) {
                if (manual) appUpdateError = releasesWhy(e)
            } finally {
                checkingAppUpdate = false
            }
        }
    }

    /**
     * Downloads the available APK into cache/updates/, verifies its versionCode is not a downgrade,
     * synchronously flushes the group token preference on IO, and sets [pendingInstallApk] so
     * SettingsScreen launches the installer with the live Activity Context (safe across rotation).
     */
    fun downloadAppUpdate(keepGroupToken: Boolean) {
        val rel = availableUpdate ?: latestAppRelease ?: return
        if (appUpdateDownloading) return
        updateDownloadJob?.cancel()
        appUpdateDownloading = true
        appUpdateProgress = 0
        appUpdateError = null
        downloadedApk = null
        pendingInstallApk = null
        updateDownloadJob = viewModelScope.launch {
            try {
                val dir = java.io.File(ctx.cacheDir, "updates")
                val safeName = rel.apkName.replace(Regex("[^A-Za-z0-9._-]"), "_").ifEmpty { "LoraOta-update.apk" }
                val dest = java.io.File(dir, safeName)
                withContext(Dispatchers.IO) {
                    AppUpdater.downloadApk(rel, dest, tokens.get(), { isActive }) { pct ->
                        viewModelScope.launch(Dispatchers.Main) { appUpdateProgress = pct }
                    }
                    @Suppress("DEPRECATION")
                    val pkgInfo = ctx.packageManager.getPackageArchiveInfo(dest.absolutePath, 0)
                    if (pkgInfo == null || pkgInfo.packageName != ctx.packageName) {
                        dest.delete()
                        throw Exception(str(R.string.err_update_bad_apk))
                    }
                    val archiveCode = androidx.core.content.pm.PackageInfoCompat.getLongVersionCode(pkgInfo)
                    if (archiveCode < BuildConfig.VERSION_CODE.toLong()) {
                        dest.delete()
                        throw Exception("APK versionCode ($archiveCode) < installed (${BuildConfig.VERSION_CODE})")
                    }
                    if (tokens.hasBuiltInGroup || tokens.hasSavedGroup) {
                        if (keepGroupToken) {
                            if (!tokens.persistGroupToken()) {
                                dest.delete()
                                throw Exception(str(R.string.err_update_token))
                            }
                        } else tokens.clearSavedGroup()
                    }
                }
                syncTokenFlags()
                downloadedApk = dest
                pendingInstallApk = dest
            } catch (e: CancellationException) {
                throw e
            } catch (e: Exception) {
                appUpdateError = releasesWhy(e)
            } finally {
                appUpdateDownloading = false
            }
        }
    }

    /** Installs [apk] via a PackageInstaller session; the outcome comes back through [AppUpdater.installResults]. */
    fun installAppUpdate(apk: java.io.File) {
        viewModelScope.launch {
            try {
                withContext(Dispatchers.IO) { AppUpdater.installApk(ctx, apk) }
            } catch (e: CancellationException) {
                throw e
            } catch (e: Exception) {
                appUpdateError = e.message ?: e.toString()
            }
        }
    }

    fun refreshCacheStats() {
        viewModelScope.launch {
            val (count, bytes) = withContext(Dispatchers.IO) { Releases.cacheStats(ctx.filesDir) }
            cacheFileCount = count
            cacheBytes = bytes
        }
    }

    fun clearFirmwareCache() = work {
        withContext(Dispatchers.IO) {
            Releases.clearCache(ctx.filesDir)
            java.io.File(ctx.cacheDir, "updates").deleteRecursively()
        }
        downloadedApk = null
        pendingInstallApk = null
        builds = emptyList()
        releases = emptyList()
        olderFound = emptyList()
        releasesLoadedAt = 0L
        upstreamLoadedAt = 0L
        refreshCacheStats()
        message = str(R.string.settings_cache_cleared)
    }

    // ---- repeater 4-tab navigation ----

    fun selectRepeaterTab(tab: RepeaterTab) {
        if ((busy || reconnecting) && tab != repeaterTab) return
        repeaterTab = tab
        if (tab == RepeaterTab.HEALTH) {
            val dev = deviceKey(DeviceKind.REPEATER)
            if (repeaterHealthLoadedFor != dev || healthKind != DeviceKind.REPEATER) {
                resetHealthState(DeviceKind.REPEATER)
                refreshHealth()
            }
        }
    }

    // ---- interactive CLI console (repeater remote & companion local) ----

    fun openCompanionCli() {
        cliReturn = step
        cliKind = DeviceKind.COMPANION
        message = ""
        step = Step.CLI
    }

    fun closeCli() {
        step = cliReturn
    }

    fun clearCliLog(isCompanion: Boolean) {
        if (isCompanion) toolsState.companionCliLog.clear() else toolsState.repeaterCliLog.clear()
    }

    fun addCliSnippet(cmd: String) = toolsState.addSnippet(cmd)

    fun removeCliSnippet(cmd: String) = toolsState.removeSnippet(cmd)

    fun sendCliCommand(rawCommand: String, isCompanion: Boolean) = work {
        val cmd = rawCommand.trim()
        if (cmd.isEmpty()) return@work
        toolsState.addCliHistory(isCompanion, cmd)
        toolsState.appendCli(isCompanion, cmd, CliLineKind.COMMAND)
        working = cmd
        val t0 = System.currentTimeMillis()

        if (isCompanion) {
            if (CliHelper.expectsNoReply(cmd)) {
                try {
                    companion.cli(cmd)
                } catch (e: CancellationException) {
                    throw e
                } catch (_: Exception) {
                    // no-reply commands (reboot/poweroff) disconnect before answering
                }
                toolsState.appendCli(true, str(R.string.cli_sent_no_reply), CliLineKind.INFO)
                return@work
            }
            try {
                val reply = companion.cli(cmd).trim()
                val rtt = (System.currentTimeMillis() - t0).coerceAtLeast(1L)
                toolsState.appendCli(true, reply.ifEmpty { "OK" }, CliLineKind.REPLY, durationMs = rtt)
            } catch (e: CancellationException) {
                throw e
            } catch (e: Exception) {
                toolsState.appendCli(true, e.message ?: e.toString(), CliLineKind.ERROR)
            }
            return@work
        }

        val r = repeater ?: return@work
        if (canReadRepeater == null) {
            canReadRepeater = runCatching { RepeaterBackup.supported(companion) }.getOrDefault(false)
        }
        if (CliHelper.expectsNoReply(cmd)) {
            companion.sendRemoteCli(r, cmd)
            if (cmd.equals("reboot", ignoreCase = true) || cmd.equals("clkreboot", ignoreCase = true)) {
                toolsState.needsRelogin = true
            }
            toolsState.appendCli(false, str(R.string.cli_sent_no_reply), CliLineKind.INFO)
            return@work
        }
        if (canReadRepeater != true) {
            // Stock companion firmware: fire-and-forget command without captured reply
            companion.sendRemoteCli(r, cmd)
            toolsState.appendCli(false, str(R.string.cli_sent_stock_companion), CliLineKind.INFO)
            return@work
        }
        val reply = try {
            RepeaterBackup.ask(companion, r, cmd)
        } finally {
            withContext(NonCancellable) { runCatching { companion.captureCli(0) } }
        }
        val rtt = (System.currentTimeMillis() - t0).coerceAtLeast(1L)
        if (reply == null) {
            toolsState.appendCli(false, str(R.string.cli_timeout), CliLineKind.ERROR, durationMs = rtt)
        } else {
            toolsState.appendCli(false, reply.trim(), CliLineKind.REPLY, durationMs = rtt)
            // the repeater took the new admin password: keep the saved one in step with it
            val newPw = CliHelper.newPasswordOf(cmd)
            if (newPw != null && reply.trim().removePrefix("> ").startsWith("password now", ignoreCase = true) &&
                hasSavedPassword(r)) {
                savePassword(r, newPw)
            }
        }
    }

    // ---- repeater quick actions, relogin & neighbors ----

    /** Re-authenticates to the currently open repeater using its saved Keystore password (e.g. after reboot). */
    fun reloginCurrentRepeater() = work {
        val r = repeater ?: return@work
        val pw = savedPassword(r)
        if (pw.isNullOrEmpty()) throw Exception(str(R.string.relogin_no_password))
        val learned = signIn(r, pw)
        if (learned != null) repeater = learned
        toolsState.needsRelogin = false
        inspect(learned ?: r, learned?.hops)
        message = str(R.string.relogin_done)
    }

    /** Sets repeater UTC clock directly from the phone's epoch time (`time <epoch>`), independent of companion RTC drift. */
    fun syncRepeaterClock() = work {
        val r = repeater ?: return@work
        if (canReadRepeater == null) {
            canReadRepeater = runCatching { RepeaterBackup.supported(companion) }.getOrDefault(false)
        }
        val cmd = "time ${System.currentTimeMillis() / 1000L}"
        working = str(R.string.qa_clock_sync)
        if (canReadRepeater != true) {
            companion.sendRemoteCli(r, cmd)
            toolsState.lastClockReply = str(R.string.cli_sent_stock_companion)
            message = str(R.string.qa_clock_sent)
            return@work
        }
        val reply = try {
            RepeaterBackup.ask(companion, r, cmd)?.trim()
        } finally {
            withContext(NonCancellable) { runCatching { companion.captureCli(0) } }
        }
        when {
            reply == null -> throw Exception(str(R.string.health_no_answer))
            reply.contains("cannot go backwards", ignoreCase = true) -> {
                toolsState.lastClockReply = reply
                message = str(R.string.qa_clock_already_ahead)
            }
            reply.startsWith("OK", ignoreCase = true) -> {
                toolsState.lastClockReply = reply
                message = reply
            }
            else -> {
                toolsState.lastClockReply = reply
                throw Exception(reply)
            }
        }
    }

    fun sendRepeaterAdvert(flood: Boolean = true) = work {
        val r = repeater ?: return@work
        val cmd = if (flood) "advert" else "advert.zerohop"
        if (canReadRepeater == null) {
            canReadRepeater = runCatching { RepeaterBackup.supported(companion) }.getOrDefault(false)
        }
        working = str(R.string.qa_advert)
        if (canReadRepeater != true) {
            companion.sendRemoteCli(r, cmd)
            message = str(R.string.qa_advert_sent)
            return@work
        }
        val reply = try {
            RepeaterBackup.ask(companion, r, cmd)?.trim()
        } finally {
            withContext(NonCancellable) { runCatching { companion.captureCli(0) } }
        }
        message = reply ?: str(R.string.qa_advert_sent)
    }

    fun rebootRepeater() = work {
        val r = repeater ?: return@work
        working = str(R.string.qa_reboot)
        companion.sendRemoteCli(r, "reboot")
        toolsState.needsRelogin = true
        message = str(R.string.qa_reboot_sent)
    }

    /** Lets the neighbors card ask again after an error. */
    fun clearNeighborsError() { toolsState.neighborsError = null }

    fun loadNeighbors() = work {
        val r = repeater ?: return@work
        if (canReadRepeater == null) {
            canReadRepeater = runCatching { RepeaterBackup.supported(companion) }.getOrDefault(false)
        }
        if (canReadRepeater != true) {
            toolsState.neighborsError = str(R.string.health_no_capture)
            toolsState.neighborsLoaded = true
            return@work
        }
        working = str(R.string.working_neighbors)
        val reply = try {
            RepeaterBackup.ask(companion, r, "neighbors")
        } catch (e: kotlinx.coroutines.CancellationException) {
            throw e
        } catch (e: Exception) {
            // set an error so the card does not ask again in a loop
            toolsState.neighborsError = e.message ?: e.toString()
            return@work
        } finally {
            withContext(NonCancellable) { runCatching { companion.captureCli(0) } }
        }
        if (reply == null) {
            toolsState.neighborsError = str(R.string.health_no_answer)
            toolsState.neighborsLoaded = true
            return@work
        }
        toolsState.neighborsError = null
        val parsedNeighbors = NeighborsParser.parse(reply, allContactsByKey)
        toolsState.neighbors.clear()
        toolsState.neighbors.addAll(parsedNeighbors)
        toolsState.neighborsLoaded = true
        fleetState.recordObservedNeighbors(hex(r.publicKey), parsedNeighbors)
    }

    /** Removes a stale neighbor entry from the repeater's neighbor table (`neighbor.remove <hex>`). */
    fun removeNeighbor(prefixHex: String) = work {
        val r = repeater ?: return@work
        val cmd = "neighbor.remove $prefixHex"
        working = cmd
        if (canReadRepeater != true) {
            companion.sendRemoteCli(r, cmd)
            toolsState.neighbors.removeAll { it.prefixHex == prefixHex }
            fleetState.recordObservedNeighbors(hex(r.publicKey), toolsState.neighbors)
            message = str(R.string.neighbors_removed)
            return@work
        }
        val reply = try {
            RepeaterBackup.ask(companion, r, cmd)?.trim()
        } finally {
            withContext(NonCancellable) { runCatching { companion.captureCli(0) } }
        }
        if (reply == null) throw Exception(str(R.string.health_no_answer))
        if (!reply.startsWith("OK", ignoreCase = true)) throw Exception(reply)
        toolsState.neighbors.removeAll { it.prefixHex == prefixHex }
        fleetState.recordObservedNeighbors(hex(r.publicKey), toolsState.neighbors)
        message = str(R.string.neighbors_removed)
    }

    /**
     * Runs a 5-probe live link/range test (`clock` CLI) against the current repeater,
     * measuring delivery success rate and min/avg/max RTT (ms).
     */
    fun runRepeaterPingTest(count: Int = 5) = work {
        val r = repeater ?: return@work
        if (canReadRepeater == null) {
            canReadRepeater = runCatching { RepeaterBackup.supported(companion) }.getOrDefault(false)
        }
        if (canReadRepeater != true) throw Exception(str(R.string.health_no_capture))
        toolsState.pingSamples.clear()
        toolsState.pingRunning = true
        try {
            for (seq in 1..count) {
                working = "${str(R.string.ping_title)} ($seq/$count)"
                val t0 = System.currentTimeMillis()
                val reply = try {
                    RepeaterBackup.ask(companion, r, "clock")?.removePrefix("> ")?.trim()
                } finally {
                    withContext(NonCancellable) { runCatching { companion.captureCli(0) } }
                }
                val rtt = (System.currentTimeMillis() - t0).coerceAtLeast(1L)
                if (reply != null) {
                    toolsState.pingSamples.add(io.github.n3tk0.loraota.tools.PingSample(seq, true, rtt, reply))
                } else {
                    toolsState.pingSamples.add(io.github.n3tk0.loraota.tools.PingSample(seq, false, null, str(R.string.cli_timeout)))
                }
                if (seq < count) delay(350L)
            }
        } finally {
            toolsState.pingRunning = false
        }
    }

    // ---- visual repeater settings editor, config profiles & tempradio ----

    fun saveConfigProfile(name: String, settingsSource: Map<String, String> = toolsState.settingsDraft) {
        if (toolsState.saveProfile(name, settingsSource)) {
            message = str(R.string.profile_saved, name.trim())
            messageIsError = false
        }
    }

    fun saveBackupAsProfile(name: String, b: Backup) {
        val map = b.items.filter { it.group == BackupItem.GROUP_SETTING }.associate { it.id to it.value }
        saveConfigProfile(name, map)
    }

    fun applyConfigProfile(profile: io.github.n3tk0.loraota.tools.ConfigProfile) {
        var applied = 0
        for ((k, v) in profile.settings) {
            if (k in io.github.n3tk0.loraota.tools.ConfigProfileLogic.EXCLUDED_KEYS) continue
            if (k in toolsState.settings) {
                toolsState.settingsDraft[k] = v
                applied++
            }
        }
        message = str(R.string.profile_applied, profile.name, applied)
        messageIsError = false
    }

    fun deleteConfigProfile(name: String) = toolsState.deleteProfile(name)

    fun loadRepeaterSettings() = work {
        val r = repeater ?: return@work
        if (canReadRepeater == null) {
            canReadRepeater = runCatching { RepeaterBackup.supported(companion) }.getOrDefault(false)
        }
        if (canReadRepeater != true) throw Exception(str(R.string.health_no_capture))
        working = str(R.string.working_read_settings)
        val items = try {
            RepeaterBackup.read(companion, r, withOta = false) { i, total ->
                toolsState.settingsProgress = i to total
                working = "${str(R.string.working_read_settings)} ($i/$total)"
            }
        } finally {
            toolsState.settingsProgress = null
        }
        toolsState.settings.clear()
        toolsState.settingsDraft.clear()
        for (item in items) {
            toolsState.settings[item.id] = item.value
            toolsState.settingsDraft[item.id] = item.value
        }
        toolsState.settingsLoaded = true
        message = str(R.string.rset_loaded, items.size)
    }

    fun updateRepeaterSettingDraft(key: String, value: String) {
        toolsState.settingsDraft[key] = value
    }

    fun resetRepeaterSettingsDraft() {
        toolsState.settingsDraft.clear()
        toolsState.settingsDraft.putAll(toolsState.settings)
    }

    fun changedRepeaterSettings(): Map<String, String> =
        toolsState.settingsDraft.filter { (k, v) -> toolsState.settings[k] != v }

    fun saveRepeaterSettings() = work {
        val r = repeater ?: return@work
        val changed = changedRepeaterSettings()
        if (changed.isEmpty()) return@work
        val normalized = changed.mapValues { (k, v) ->
            io.github.n3tk0.loraota.tools.RepeaterSettingsValidator.normalizeForSave(k, v)
        }
        val items = normalized.map { (k, v) -> BackupItem(GROUP_SETTING, k, k, v) }
        val result = try {
            RepeaterBackup.apply(companion, r, items) { i, total ->
                toolsState.settingsProgress = i to total
                working = "${str(R.string.working_save_settings)} ($i/$total)"
            }
        } finally {
            toolsState.settingsProgress = null
        }
        val failedKeys = result.failed.map { it.first.id }.toSet()
        for ((k, v) in normalized) {
            if (k !in failedKeys) {
                toolsState.settings[k] = v
                toolsState.settingsDraft[k] = v
            }
        }
        if (result.failed.isEmpty()) {
            val needsRestart = changed.keys.any { it in RepeaterBackup.NEEDS_RESTART }
            message = if (needsRestart) str(R.string.rset_saved_restart, result.done)
            else str(R.string.rset_saved, result.done)
        } else {
            val firstErr = result.failed.first()
            message = str(R.string.rset_partial, result.done, result.failed.size, "${firstErr.first.id}: ${firstErr.second}")
            messageIsError = true
        }
    }

    /** Sends `tempradio <freq>,<bw>,<sf>,<cr>,<mins>` so the repeater tests new radio params safely and reverts automatically. */
    fun applyTempRadio(command: String) = work {
        val r = repeater ?: return@work
        if (canReadRepeater == null) {
            canReadRepeater = runCatching { RepeaterBackup.supported(companion) }.getOrDefault(false)
        }
        working = command
        if (canReadRepeater != true) {
            companion.sendRemoteCli(r, command)
            message = str(R.string.tempradio_sent)
            return@work
        }
        val reply = try {
            RepeaterBackup.ask(companion, r, command)?.trim()
        } finally {
            withContext(NonCancellable) { runCatching { companion.captureCli(0) } }
        }
        if (reply == null) throw Exception(str(R.string.health_no_answer))
        if (!reply.startsWith("OK", ignoreCase = true)) throw Exception(reply)
        message = reply
    }

    // ---- fleet view (pins) & batch health sweep ----

    fun isPinned(contact: Contact) = hex(contact.publicKey) in fleetState.pinnedKeys

    fun togglePin(contact: Contact) = fleetState.togglePin(hex(contact.publicKey))

    fun openSweep() {
        message = ""
        fleetState.sweepEntries.clear()
        fleetState.sweepFinished = false
        fleetState.sweepRunning = false
        for (c in repeaters) {
            // only direct repeaters are pre-ticked: a flood login to a far one costs much more airtime
            if (hasSavedPassword(c) && c.hops == 0) {
                val k = hex(c.publicKey)
                if (k !in fleetState.sweepPicked) fleetState.sweepPicked[k] = true
            }
        }
        step = Step.SWEEP
    }

    fun closeSweep() {
        job?.cancel()
        fleetState.sweepRunning = false
        step = Step.REPEATER
    }

    fun resetSweep() {
        fleetState.sweepEntries.clear()
        fleetState.sweepFinished = false
        fleetState.sweepRunning = false
    }

    /** Opens a repeater directly on the Health tab from a finished Health Sweep row. */
    fun openRepeaterFromSweep(keyHex: String) {
        if (busy || fleetState.sweepRunning) return
        val contact = repeaters.firstOrNull { hex(it.publicKey) == keyHex } ?: return
        val pw = savedPassword(contact)
        if (pw.isNullOrEmpty()) {
            step = Step.REPEATER
            loginSheet = contact
        } else {
            login(contact, pw, initialTab = RepeaterTab.HEALTH)
        }
    }

    fun startSweep() = work {
        if (canReadRepeater == null) {
            canReadRepeater = runCatching { RepeaterBackup.supported(companion) }.getOrDefault(false)
        }
        if (canReadRepeater != true) throw Exception(str(R.string.health_no_capture))
        val selected = repeaters.filter { hasSavedPassword(it) && fleetState.sweepPicked[hex(it.publicKey)] == true }
        if (selected.isEmpty()) return@work
        fleetState.sweepEntries.clear()
        for (c in selected) fleetState.sweepEntries.add(SweepEntry(hex(c.publicKey), c.name))
        runSweepIndices(fleetState.sweepEntries.indices.toList())
    }

    /** Re-runs Health Sweep on entries that failed or remained pending after a stopped sweep. */
    fun retryFailedSweep() = work {
        if (canReadRepeater != true) throw Exception(str(R.string.health_no_capture))
        val failedIndices = fleetState.sweepEntries.indices.filter {
            val st = fleetState.sweepEntries[it].status
            st == SweepStatus.FAILED || st == SweepStatus.PENDING
        }
        if (failedIndices.isEmpty()) return@work
        for (i in failedIndices) {
            fleetState.sweepEntries[i] = fleetState.sweepEntries[i].copy(status = SweepStatus.PENDING, detail = "")
        }
        runSweepIndices(failedIndices)
    }

    private suspend fun runSweepIndices(indices: List<Int>) {
        fleetState.sweepRunning = true
        fleetState.sweepFinished = false
        try {
            for ((stepNum, idx) in indices.withIndex()) {
                val existing = fleetState.sweepEntries.getOrNull(idx) ?: continue
                val c = repeaters.firstOrNull { hex(it.publicKey) == existing.key }
                if (c == null) {
                    fleetState.sweepEntries[idx] = existing.copy(status = SweepStatus.FAILED, detail = str(R.string.health_no_answer))
                    continue
                }
                val pw = savedPassword(c)
                if (pw.isNullOrEmpty()) {
                    fleetState.sweepEntries[idx] = existing.copy(status = SweepStatus.FAILED, detail = str(R.string.q_login_once))
                    continue
                }
                fleetState.sweepEntries[idx] = existing.copy(status = SweepStatus.RUNNING, detail = str(R.string.working_login))
                working = "${c.name} (${stepNum + 1}/${indices.size})"
                val entry = try {
                    val learned = signIn(c, pw) ?: c
                    var syncedClock = false
                    val line = try {
                        if (fleetState.sweepSyncClock) {
                            val epoch = System.currentTimeMillis() / 1000L
                            val clkReply = RepeaterBackup.ask(companion, learned, "time $epoch")?.trim()
                            syncedClock = clkReply != null && (clkReply.startsWith("OK", ignoreCase = true) ||
                                clkReply.contains("cannot go backwards", ignoreCase = true))
                        }
                        RepeaterBackup.ask(companion, learned, SelfTest.COMMAND)?.removePrefix("> ")?.trim()
                    } finally {
                        withContext(NonCancellable) { runCatching { companion.captureCli(0) } }
                    }
                    if (line == null) {
                        SweepEntry(existing.key, c.name, SweepStatus.FAILED, str(R.string.health_no_answer))
                    } else {
                        val parsed = SelfTest.parse(line)
                        if (parsed == null) {
                            SweepEntry(existing.key, c.name, SweepStatus.WARN, str(R.string.health_no_selftest))
                        } else {
                            val before = withContext(Dispatchers.IO) { healthStore.records(existing.key) }.lastOrNull()?.let { SelfTest.parse(it.line) }
                            val updated = withContext(Dispatchers.IO) {
                                healthStore.add(existing.key, line)
                                healthStore.records(existing.key)
                            }
                            updated.lastOrNull()?.let { fleetState.fleetHealth[existing.key] = it }
                            val warns = HealthCheck.warnings(parsed, before)
                            val probs = HealthCheck.problems(warns)
                            val st = when {
                                probs.any { it.severity == io.github.n3tk0.loraota.ota.HealthSeverity.DANGER } -> SweepStatus.DANGER
                                probs.any { it.severity == io.github.n3tk0.loraota.ota.HealthSeverity.WARN } || !parsed.ok -> SweepStatus.WARN
                                else -> SweepStatus.OK
                            }
                            val batt = buildList {
                                parsed.batteryMv?.let { add(String.format(java.util.Locale.ROOT, "%.2f V", it / 1000.0)) }
                                parsed.batteryPercent?.let { add("$it%") }
                            }.joinToString(" · ")
                            val baseDetail = if (st == SweepStatus.OK) str(R.string.health_ok)
                            else parsed.problems.joinToString(", ").ifEmpty { str(R.string.sweep_check_warnings) }
                            val detail = if (syncedClock) "$baseDetail · ${str(R.string.sweep_clock_synced)}" else baseDetail
                            SweepEntry(existing.key, c.name, st, detail, batt, warns)
                        }
                    }
                } catch (e: CancellationException) {
                    throw e
                } catch (e: io.github.n3tk0.loraota.ble.CompanionError) {
                    throw e
                } catch (e: Exception) {
                    SweepEntry(existing.key, c.name, SweepStatus.FAILED, e.message ?: e.toString())
                } finally {
                    withContext(NonCancellable) { runCatching { companion.restorePath(c) } }
                }
                fleetState.sweepEntries[idx] = entry
            }
            fleetState.sweepFinished = true
        } finally {
            for (i in fleetState.sweepEntries.indices) {
                if (fleetState.sweepEntries[i].status == SweepStatus.RUNNING) {
                    fleetState.sweepEntries[i] = fleetState.sweepEntries[i].copy(status = SweepStatus.FAILED, detail = str(R.string.msg_stopped))
                }
            }
            fleetState.sweepRunning = false
        }
    }

    // ---- offline viewer (saved backups, past health checks & offline firmware hub without companion) ----

    private var offlineLoadJob: Job? = null

    fun openOffline() {
        if (step != Step.OFFLINE) offlineReturn = step
        message = ""
        step = Step.OFFLINE
        loadOfflineData()
    }

    fun closeOffline() {
        fleetState.showFieldPrepDialog = false
        step = offlineReturn
    }

    fun loadOfflineData() {
        offlineLoadJob?.cancel()
        fleetState.offlineLoading = true
        offlineLoadJob = viewModelScope.launch {
            try {
                val (healthMap, allBackups, cachedFw) = withContext(Dispatchers.IO) {
                    val h = healthStore.allRecords()
                    val b = backupStore.all()
                    val fw = Releases.cachedFirmwares(repo, ctx.filesDir).map { f ->
                        CachedFirmwareItem(
                            firmware = f,
                            hasCachedOta = Releases.isFileCached(f, ctx.filesDir),
                            hasCachedDfu = Releases.isDfuCached(f, ctx.filesDir),
                            otaIsEsp32 = Releases.cachedOtaIsEsp32(f, ctx.filesDir),
                        )
                    }
                    Triple(h, b, fw)
                }
                fleetState.offlineHealth.clear()
                fleetState.offlineHealth.putAll(healthMap)
                fleetState.offlineBackups.clear()
                fleetState.offlineBackups.addAll(allBackups)
                fleetState.offlineFirmwares.clear()
                fleetState.offlineFirmwares.addAll(cachedFw)
                for (b in allBackups) {
                    if (b.deviceKey.isNotEmpty() && b.deviceName.isNotBlank() && b.deviceKey !in fleetState.knownDeviceNames) {
                        fleetState.knownDeviceNames[b.deviceKey] = b.deviceName
                    }
                }
                refreshCacheStats()
            } finally {
                if (offlineLoadJob === coroutineContext[Job]) fleetState.offlineLoading = false
            }
        }
    }

    fun deleteOfflineBackup(b: Backup) {
        offlineLoadJob?.cancel()
        viewModelScope.launch {
            withContext(Dispatchers.IO) { backupStore.delete(b) }
            fleetState.offlineBackups.remove(b)
            loadOfflineData()
        }
    }

    /** Imports an external `.json` backup file directly into `Offline -> Backups` without needing a connected device. */
    fun importOfflineBackup(uri: Uri) = work {
        val text = withContext(Dispatchers.IO) {
            ctx.contentResolver.openInputStream(uri)?.use { String(it.readBytes(), Charsets.UTF_8) }
        } ?: throw Exception(str(R.string.err_backup_file))
        if (runCatching { PasswordBox.needsPassword(text) }.getOrElse { throw Exception(str(R.string.err_backup_file)) }) {
            importText = text
            importPassword = ""
            return@work
        }
        finishOfflineBackupImport(text, "")
    }

    fun importOfflineBackupWithPassword() = work {
        val text = importText ?: return@work
        finishOfflineBackupImport(text, importPassword)
    }

    private suspend fun finishOfflineBackupImport(text: String, password: String) {
        val b = try {
            withContext(Dispatchers.Default) { PasswordBox.import(text, password) }
        } catch (_: io.github.n3tk0.loraota.backup.WrongPassword) {
            throw Exception(str(R.string.err_backup_password))
        } catch (_: Exception) {
            importText = null
            throw Exception(str(R.string.err_backup_file))
        }
        importText = null
        importPassword = ""
        withContext(Dispatchers.IO) { backupStore.save(b) }
        if (b.deviceKey.isNotEmpty() && b.deviceName.isNotBlank()) {
            fleetState.rememberDeviceName(b.deviceKey, b.deviceName)
        }
        loadOfflineData()
        message = str(R.string.msg_import_done, b.deviceName)
    }

    /** Exports the entire user workspace (backups, health, encrypted passwords, pins, snippets, profiles, neighbors) to [uri]. */
    fun exportWorkspaceArchive(uri: Uri, password: String) = work {
        val json = withContext(Dispatchers.IO) {
            val allBackups = backupStore.all()
            val allHealth = healthStore.allRecords()
            val pwMap = LinkedHashMap<String, String>()
            for (k in passwordStore.keys()) {
                passwordStore.get(k)?.let { pwMap[k] = it }
            }
            val data = io.github.n3tk0.loraota.tools.WorkspaceData(
                backups = allBackups,
                healthRecords = allHealth,
                passwords = pwMap,
                pinnedKeys = fleetState.pinnedKeys.toList(),
                cliSnippets = toolsState.customCliSnippets.toList(),
                profiles = toolsState.configProfiles.toList(),
                deviceNames = fleetState.knownDeviceNames.toMap(),
                observedNeighbors = fleetState.observedNeighbors.toMap(),
            )
            io.github.n3tk0.loraota.tools.WorkspaceBundle.export(data, password)
        }
        withContext(Dispatchers.IO) {
            ctx.contentResolver.openOutputStream(uri, "wt")?.use { it.write(json.toByteArray(Charsets.UTF_8)) }
                ?: throw Exception(str(R.string.offline_fw_export_failed))
        }
        message = str(R.string.workspace_exported)
    }

    /** Reads a workspace archive file; prompts for password if encrypted, or restores immediately if unencrypted. */
    fun importWorkspaceArchive(uri: Uri) = work {
        val text = withContext(Dispatchers.IO) {
            ctx.contentResolver.openInputStream(uri)?.use { String(it.readBytes(), Charsets.UTF_8) }
        } ?: throw Exception(str(R.string.err_backup_file))
        val encrypted = runCatching { io.github.n3tk0.loraota.tools.WorkspaceBundle.needsPassword(text) }
            .getOrElse { throw Exception(str(R.string.err_backup_file)) }
        if (encrypted) {
            fleetState.pendingWorkspaceImportJson = text
            fleetState.workspaceArchivePassword = ""
            return@work
        }
        finishWorkspaceImport(text, "")
    }

    fun confirmEncryptedWorkspaceImport() = work {
        val text = fleetState.pendingWorkspaceImportJson ?: return@work
        finishWorkspaceImport(text, fleetState.workspaceArchivePassword)
    }

    private suspend fun finishWorkspaceImport(text: String, password: String) {
        val data = try {
            withContext(Dispatchers.Default) { io.github.n3tk0.loraota.tools.WorkspaceBundle.import(text, password) }
        } catch (_: io.github.n3tk0.loraota.backup.WrongPassword) {
            throw Exception(str(R.string.err_backup_password))
        } catch (_: Exception) {
            fleetState.pendingWorkspaceImportJson = null
            throw Exception(str(R.string.err_backup_file))
        }
        fleetState.pendingWorkspaceImportJson = null
        fleetState.workspaceArchivePassword = ""
        val unsavedPasswords = withContext(Dispatchers.IO) {
            for (b in data.backups) runCatching { backupStore.save(b) }
            for ((devKey, records) in data.healthRecords) {
                val existingTimes = healthStore.records(devKey).map { it.time }.toHashSet()
                for (r in records) {
                    if (r.time !in existingTimes) healthStore.add(devKey, r.line, r.time)
                }
            }
            data.passwords.count { (k, pw) -> !passwordStore.set(k, pw) }
        }
        savedKeys.clear()
        savedKeys.addAll(passwordStore.keys())
        fleetState.mergePins(data.pinnedKeys)
        fleetState.rememberDeviceNames(data.deviceNames)
        fleetState.mergeObservedNeighbors(data.observedNeighbors)
        toolsState.replaceSnippets((toolsState.customCliSnippets + data.cliSnippets).distinct())
        toolsState.mergeProfiles(data.profiles)
        val latest = withContext(Dispatchers.IO) { healthStore.allLatest() }
        fleetState.fleetHealth.clear()
        fleetState.fleetHealth.putAll(latest)
        loadOfflineData()
        message = str(
            R.string.workspace_imported,
            data.backups.size,
            data.healthRecords.size,
            data.passwords.size,
            data.profiles.size,
        ) + if (unsavedPasswords > 0) "\n" + str(R.string.workspace_passwords_not_saved, unsavedPasswords) else ""
    }

    fun deleteOfflineFirmware(f: FirmwareFile) {
        offlineLoadJob?.cancel()
        viewModelScope.launch {
            withContext(Dispatchers.IO) { Releases.removeCachedFirmware(ctx.filesDir, f) }
            builds = withContext(Dispatchers.IO) { Releases.builds(ctx.filesDir) }
            loadOfflineData()
            message = str(R.string.offline_fw_deleted)
            messageIsError = false
        }
    }

    /** Exports an already-cached firmware binary (.bin or DFU .zip/.bin) to a user-chosen SAF file. */
    fun exportCachedFirmware(f: FirmwareFile, dfu: Boolean, uri: android.net.Uri) = work {
        val bytes = withContext(Dispatchers.IO) { Releases.readCachedFile(f, dfu, ctx.filesDir) }
            ?: throw Exception(str(R.string.offline_fw_export_failed))
        withContext(Dispatchers.IO) {
            ctx.contentResolver.openOutputStream(uri, "wt")?.use { it.write(bytes) }
                ?: throw Exception(str(R.string.offline_fw_export_failed))
        }
        message = str(R.string.offline_fw_exported)
    }

    // ---- field preparation ("Подготви за терен" with board picker) & shareable diagnostics ----

    /** Fetches the release catalog and opens the board selection dialog with known boards pre-selected. */
    fun openFieldPrepPicker() = work {
        fleetState.fieldPrepProgress = str(R.string.field_prep_listing)
        working = str(R.string.field_prep_listing)
        try {
            Releases.apiToken = tokens.get()
            val listing = withContext(Dispatchers.IO) { Releases.listDetailed(repo, ctx.filesDir) }
            releases = listing.files
            releasesLoadedAt = System.currentTimeMillis()
            val all = listing.files
            if (all.isEmpty()) throw Exception(str(R.string.field_prep_empty))

            // Keep newest firmware entry per unique board env
            val latestByEnv = LinkedHashMap<String, FirmwareFile>()
            for (f in all) {
                if (f.env !in latestByEnv && (f.hasLoraOta || f.hasDfu)) latestByEnv[f.env] = f
            }
            if (latestByEnv.isEmpty()) throw Exception(str(R.string.field_prep_empty))

            val knownEnvs = LinkedHashSet<String>()
            builds.mapTo(knownEnvs) { it.env }
            oldImage?.env?.let { knownEnvs.add(it) }
            newImage?.env?.let { knownEnvs.add(it) }
            flashImage?.env?.let { knownEnvs.add(it) }
            if (companionModel.isNotEmpty()) {
                latestByEnv.keys.firstOrNull { BoardCheck.fitsCompanion(it) && BoardCheck.matches(it, companionModel) == true }
                    ?.let { knownEnvs.add(it) }
            }

            val cachedStatus = withContext(Dispatchers.IO) {
                latestByEnv.mapValues { (_, f) -> Releases.isAnyCached(f, ctx.filesDir) }
            }

            fleetState.fieldPrepCandidates.clear()
            fleetState.fieldPrepCandidates.addAll(latestByEnv.values.sortedBy { it.env })
            fleetState.fieldPrepAlreadyCached.clear()
            fleetState.fieldPrepAlreadyCached.putAll(cachedStatus)
            fleetState.fieldPrepPicked.clear()
            val preChecked = knownEnvs.filter { it in latestByEnv }
            if (preChecked.isNotEmpty()) {
                for (env in preChecked) fleetState.fieldPrepPicked[env] = true
            } else {
                for (f in fleetState.fieldPrepCandidates.filter { it.hasLoraOta }.take(6)) {
                    fleetState.fieldPrepPicked[f.env] = true
                }
            }
            fleetState.showFieldPrepDialog = true
        } finally {
            fleetState.fieldPrepProgress = null
        }
    }

    /** Downloads the user-selected boards from the Field Preparation dialog into the local offline cache. */
    fun runFieldPrepDownload() = work {
        fleetState.showFieldPrepDialog = false
        val toDownload = fleetState.fieldPrepCandidates.filter { fleetState.fieldPrepPicked[it.env] == true }
        if (toDownload.isEmpty()) return@work
        try {
            Releases.apiToken = tokens.get()
            var downloaded = 0
            var failed = 0
            var boardsDone = 0
            for ((i, f) in toDownload.withIndex()) {
                val before = downloaded
                val stepText = str(R.string.field_prep_downloading, i + 1, toDownload.size, f.env.replace('_', ' '))
                fleetState.fieldPrepProgress = stepText
                working = stepText
                withContext(Dispatchers.IO) {
                    if (f.hasLoraOta) {
                        runCatching { Releases.download(f, ctx.filesDir) }
                            .onSuccess { downloaded++ }
                            .onFailure { if (it is CancellationException) throw it else failed++ }
                    }
                    // ESP32 repeaters share the same .bin for LoRa OTA and Wi-Fi DFU (sha256 == dfuSha256)
                    if (f.hasDfu && (!f.hasLoraOta || f.dfuSha256 != f.sha256)) {
                        runCatching { Releases.downloadDfu(f, ctx.filesDir) }
                            .onSuccess { downloaded++ }
                            .onFailure { if (it is CancellationException) throw it else failed++ }
                    }
                }
                if (downloaded > before) boardsDone++
            }
            message = if (failed == 0) str(R.string.field_prep_done, downloaded, boardsDone)
            else str(R.string.field_prep_done_partial, downloaded, boardsDone, failed)
            messageIsError = failed > 0
        } finally {
            fleetState.fieldPrepProgress = null
            refreshCacheStats()
            if (step == Step.OFFLINE) loadOfflineData()
        }
    }

    fun buildDiagnosticReport(): String = DiagnosticReport.build(
        appVersion = BuildConfig.VERSION_NAME,
        versionCode = BuildConfig.VERSION_CODE,
        isGroupApk = BuildConfig.IS_GROUP_APK,
        androidVersion = "${Build.VERSION.RELEASE} (API ${Build.VERSION.SDK_INT})",
        deviceModel = "${Build.MANUFACTURER} ${Build.MODEL}",
        companionName = companionName,
        companionModel = companionModel,
        companionVersion = companionVersion,
        companionOta = companionOta,
        bleLastError = link.lastError,
        repeaterName = repeater?.name,
        repeaterIdent = info?.ident,
        logs = log.toList(),
        cliHistory = (companionCliLog + repeaterCliLog).map { "[${it.kind}] ${it.text}" },
    )

    companion object {
        // 250 kHz fits the 869.4-869.65 MHz sub-band (10 % duty) only centred on 869.525
        val SIDE_PRESETS = listOf(
            SidePreset("7", "250", "869.525", R.string.preset_fast),
            SidePreset("7", "125", null, R.string.preset_default),
            SidePreset("9", "125", null, R.string.preset_long),
        )
    }

    // ---- the repeater's battery chemistry, for the status card's bar (kept per repeater) ----
    private val chemPrefs get() = ctx.getSharedPreferences("battery_chem", Context.MODE_PRIVATE)
    var batteryChem by mutableStateOf(BatteryChem.LI_ION)
        private set

    fun loadBatteryChem(r: Contact?) {
        batteryChem = r?.let { BatteryChem.byName(chemPrefs.getString(hex(it.publicKey), null)) } ?: BatteryChem.LI_ION
    }

    fun chooseBatteryChem(c: BatteryChem) {
        batteryChem = c
        repeater?.let { chemPrefs.edit().putString(hex(it.publicKey), c.name).apply() }
    }

    override fun onCleared() {
        scanJob?.cancel()
        reconnectJob?.cancel()
        updateCheckJob?.cancel()
        updateDownloadJob?.cancel()
        runCatching {
            (ctx.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter?.bluetoothLeScanner?.stopScan(scanCallback)
        }
        OtaService.stop(ctx)
        link.close()
    }
}
