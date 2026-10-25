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
import io.github.n3tk0.loraota.ble.Contact
import io.github.n3tk0.loraota.delta.DeltaGenerator
import io.github.n3tk0.loraota.delta.DetoolsPatch
import io.github.n3tk0.loraota.flash.BleDfu
import io.github.n3tk0.loraota.flash.BoardCheck
import io.github.n3tk0.loraota.flash.FlashFailed
import io.github.n3tk0.loraota.flash.Role
import io.github.n3tk0.loraota.flash.WifiOta
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
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

enum class Step { COMPANION, REPEATER, UPDATE, FLASH, BACKUP, HEALTH, QUEUE }

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
    var flashUpstream by mutableStateOf(false)

    /** Our releases and the app's builds, the builds first (one file once: a build with `release` is both). */
    val allFirmware: List<FirmwareFile>
        get() = (builds + releases).distinctBy { it.env + it.sha256 + it.dfuSha256 }

    // a new build on GitHub ("Build Firmware for Boards")
    private val tokens = TokenStore(app)
    var hasToken by mutableStateOf(tokens.has)
    var groupToken by mutableStateOf(tokens.usesGroup)   // the group APK's own token: nothing to enter
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
    var flashDone by mutableStateOf(false)
    /** The DFU library's steps of the last Bluetooth flash, seconds from its start first: kept after a failure. */
    var flashLog by mutableStateOf<List<String>>(emptyList())
    var loginSheet by mutableStateOf<Contact?>(null)   // the repeater whose login sheet is open
    var firmwareTab by mutableStateOf(0)   // the repeater's firmware: 0 over LoRa, 1 over Bluetooth / Wi-Fi
    var companionModel by mutableStateOf("")
    var companionVersion by mutableStateOf("")
    private var companionDevice: BluetoothDevice? = null
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
    var updateBackupNote by mutableStateOf<String?>(null)   // in the update's 'are you sure': no backup, and why
    private var backupReturn = Step.REPEATER

    init {
        BuildWatcher.resume(app)
        viewModelScope.launch {
            BuildWatcher.state.collect { j ->
                val before = buildJob
                buildJob = j
                if (j?.finished == true && before != null && !before.finished) buildFinished(j)
                else if (j?.finished == true) builds = Releases.builds(ctx.filesDir)
            }
        }
        link.onDisconnect = {
            viewModelScope.launch {
                if (step != Step.COMPANION && !flashingCompanion) {
                    message = str(R.string.msg_disconnected)
                    messageIsError = true
                    loginSheet = null
                    step = Step.COMPANION
                }
            }
        }
    }

    private fun work(block: suspend () -> Unit) {
        if (busy) return
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
            if (devices.none { it.address == result.device.address }) devices.add(result.device)
        }
    }

    fun scan() {
        val scanner = (ctx.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter?.bluetoothLeScanner
        if (scanner == null) {
            message = str(R.string.msg_bluetooth_off)
            messageIsError = true
            return
        }
        devices.clear()
        val filter = ScanFilter.Builder().setServiceUuid(ParcelUuid(CompanionLink.SERVICE)).build()
        try {
            scanner.startScan(listOf(filter), ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(), scanCallback)
        } catch (e: SecurityException) {   // 'Nearby devices' not allowed
            message = str(R.string.msg_no_bt_permission)
            messageIsError = true
            return
        }
        scanning = true
        viewModelScope.launch {
            delay(10_000)
            runCatching { scanner.stopScan(scanCallback) }
            scanning = false
        }
    }

    fun connect(device: BluetoothDevice) = work {
        if (scanning) {
            runCatching {
                (ctx.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter?.bluetoothLeScanner?.stopScan(scanCallback)
            }
            scanning = false
        }
        companionModel = ""
        companionVersion = ""
        canReadRepeater = null   // another companion: asked again
        if (!link.connect(device)) throw Exception(str(R.string.msg_connect_failed))
        companion.start()
        companionDevice = device
        // left on the side channel by an update cut short; a stock companion has no such command
        companionOta = runCatching { companion.cli("tempradio off") }.isSuccess
        companionName = companion.nodeName.ifEmpty { device.name ?: device.address }
        runCatching { companion.deviceInfo() }.onSuccess { (model, version) ->
            companionModel = model
            companionVersion = version
        }
        loadRepeaters()
        step = Step.REPEATER
    }

    fun refreshRepeaters() = work { loadRepeaters() }

    private suspend fun loadRepeaters() {
        val list = companion.contacts().filter { it.isRepeater }
            .sortedWith(compareBy<Contact>({ it.hops ?: Int.MAX_VALUE }, { it.name.lowercase() }))
        repeaters.clear()
        // the ones next to us first: only those can be updated
        repeaters.addAll(list)
    }

    /** An update or a flash is writing: the screen shows only its progress, Back asks first. */
    val running get() = busy && (phase != null || flashPercent >= 0)

    /** The phone's Back and the arrow in the top bar. */
    fun back() {
        when (step) {
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
        step = Step.COMPANION
        devices.clear()   // a new connect waits for a new scan, after the clean-up
        viewModelScope.launch {
            job?.cancelAndJoin()   // the update's own clean-up (ABORT, normal radio) goes first
            busy = true
            try {
                restorePath()
                link.close()
            } finally {
                busy = false
            }
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
            runCatching { companion.restorePath(contact) }
            throw e
        }
    }

    fun login(contact: Contact, password: String) = work {
        val learned = signIn(contact, password)
        savePassword(contact, password)
        // the route just learned is the one to keep afterwards, not the older (longer) one
        repeater = learned ?: contact
        info = null
        status = null
        sensorBattery = null
        otaChecked = false
        oldImage = null
        newImage = null
        patch = null
        done = false
        sent = 0
        total = 0
        phase = null
        log.clear()
        firmwareTab = 0
        flashTarget = FlashTarget.REPEATER
        flashImage = null
        flashDone = false
        loginSheet = null
        step = Step.UPDATE
        // not read back: the pre-login route (reset for the login) says nothing about now
        inspect(learned ?: contact, learned?.hops)
    }

    /** Again the status and the OTA check, e.g. when an answer got lost. */
    fun recheck() = work {
        val old = repeater ?: return@work
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

    // admin passwords, per repeater, only on this phone (the app has no backup)
    private val passwords get() = ctx.getSharedPreferences("passwords", Context.MODE_PRIVATE)

    // which repeaters have one, read once: the list asks for every row
    private val savedKeys = mutableStateListOf<String>().apply { addAll(passwords.all.keys) }

    fun hasSavedPassword(contact: Contact) = hex(contact.publicKey) in savedKeys

    fun savedPassword(contact: Contact): String? = passwords.getString(hex(contact.publicKey), null)

    private fun savePassword(contact: Contact, password: String) {
        val key = hex(contact.publicKey)
        passwords.edit().putString(key, password).apply()
        if (key !in savedKeys) savedKeys.add(key)
    }

    fun forgetPassword(contact: Contact) {
        val key = hex(contact.publicKey)
        passwords.edit().remove(key).apply()
        savedKeys.remove(key)
    }

    /** `set ota on` in the repeater's console, then INFO to see that it took. */
    fun enableOta() = work {
        val r = repeater ?: return@work
        companion.sendRemoteCli(r, "set ota on")
        var i: OtaProtocol.Info? = null
        for (attempt in 1..3) {
            delay(3000)
            i = runCatching { session(r).info() }.getOrNull()
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

    private suspend fun restorePath() {
        val r = repeater ?: return
        runCatching { companion.restorePath(r) }
    }

    fun backToRepeaters() {
        step = Step.REPEATER
        viewModelScope.launch {
            job?.cancelAndJoin()
            restorePath()
            repeater = null
            info = null
            status = null
            sensorBattery = null
        }
    }

    // ---- firmware ----

    fun loadReleases() = viewModelScope.launch {
        val list = runCatching { withContext(Dispatchers.IO) { Releases.list(repo, ctx.filesDir) } }
        releases = list.getOrNull() ?: withContext(Dispatchers.IO) { Releases.cached(repo, ctx.filesDir) }
        if (list.isFailure) {
            val why = list.exceptionOrNull()?.message ?: ""
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
        val running = allFirmware.firstOrNull { it.sha256 == hex(i.imageSha) }
        if (running != null && oldImage == null) {
            runCatching { withContext(Dispatchers.IO) { Releases.download(running, ctx.filesDir) } }
                .onSuccess { oldImage = Image(running.file, it, running.env) }
        }
    }

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

    fun pickRelease(f: FirmwareFile) = work {
        val data = withContext(Dispatchers.IO) { Releases.download(f, ctx.filesDir) }
        newImage = Image(f.file, data, f.env)
        patch = null
    }

    fun pickFile(uri: Uri, isOld: Boolean) = work {
        val data = withContext(Dispatchers.IO) { ctx.contentResolver.openInputStream(uri)!!.use { it.readBytes() } }
        val img = Image(fileName(uri), data)
        if (isOld) {
            val i = info
            if (i != null && img.sha != hex(i.imageSha)) throw Exception(str(R.string.msg_not_running_firmware))
            oldImage = img
        } else {
            newImage = img
        }
        patch = null
    }

    /** The name the file has for the user (a content URI's last segment can be just an id). */
    private fun fileName(uri: Uri): String =
        runCatching {
            ctx.contentResolver.query(uri, arrayOf(android.provider.OpenableColumns.DISPLAY_NAME), null, null, null)?.use {
                if (it.moveToFirst()) it.getString(0) else null
            }
        }.getOrNull() ?: uri.lastPathSegment?.substringAfterLast('/') ?: "file"

    fun sideRadio(): SideRadio = SideRadio(
        ((freqMhz.toDoubleOrNull() ?: 869.47) * 1000).toInt(),
        ((bwKhz.toDoubleOrNull() ?: 125.0) * 1000).toInt(),
        sf.toIntOrNull() ?: 7,
        cr.toIntOrNull() ?: 5,
    )

    /** The new image only goes "in place" on this repeater: a power cut while it copies needs a visit. */
    fun inPlace(): Boolean {
        val n = newImage?.data?.size ?: return false
        return info?.inPlace(n) == true
    }

    /** Any other nRF52 update: the same final copy, so the same (smaller) risk note. */
    fun nrfCopy(): Boolean = newImage != null && info?.platform == OtaProtocol.PLATFORM_NRF52 && !inPlace()

    fun duty(): Double = if (ignoreAirLimit) 1.0 else ((dutyPercent.toDoubleOrNull() ?: 10.0) / 100.0).coerceIn(0.01, 1.0)

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
        if (patch == null) {
            val old = oldImage ?: return@work
            val new = newImage ?: return@work
            val i = info
            if (i != null && !i.fits(new.data.size)) throw Exception(str(R.string.msg_too_big))
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
            try {
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
            info = runCatching { session(r).info() }.getOrNull() ?: info
            message = str(R.string.msg_verified, info?.ident?.replace('|', ' ') ?: "")
        }
    }

    // ---- flash ----

    /** The companion's own flash screen, or (repeater) the Bluetooth / Wi-Fi tab of the repeater's screen. */
    fun openFlash(target: FlashTarget) {
        if (target == FlashTarget.COMPANION) flashReturn = step
        flashTarget = target
        flashImage = null
        flashPlan = null
        flashPercent = -1
        flashDone = false
        message = ""
        if (target == FlashTarget.COMPANION) step = Step.FLASH
        if (releases.isEmpty()) loadReleases()
    }

    private var flashReturn = Step.REPEATER   // where the companion's flash screen was opened from

    /** The repeater's Bluetooth / Wi-Fi tab. */
    fun showFlashTab() {
        firmwareTab = 1
        if (flashTarget != FlashTarget.REPEATER) openFlash(FlashTarget.REPEATER)
        else if (releases.isEmpty()) loadReleases()
    }

    fun closeFlash() {
        if (flashTarget == FlashTarget.COMPANION) {
            // after a flash (or a failed one) the companion is gone: back to the start
            if (flashDone || flashingCompanion) {
                flashingCompanion = false
                devices.clear()
                step = Step.COMPANION
            } else {
                // back where it was opened: the repeater's screen keeps its login and route
                flashTarget = FlashTarget.REPEATER
                flashImage = null
                step = flashReturn
            }
        } else if (flashDone) {
            backToRepeaters()   // it restarted with other firmware: log in again
        }
    }

    /** Releases that fit: the right role, and for a repeater whose chip is known the right file type. */
    fun flashCandidates(): List<FirmwareFile> {
        val platform = info?.platform
        val running = oldImage?.env
        return (if (flashUpstream) upstream else allFirmware).filter {
            it.hasDfu && when (flashTarget) {
                FlashTarget.COMPANION -> BoardCheck.fitsCompanion(it.env) && it.dfuIsZip
                FlashTarget.REPEATER -> BoardCheck.role(it.env) == Role.REPEATER && when (platform) {
                    OtaProtocol.PLATFORM_NRF52 -> it.dfuIsZip
                    OtaProtocol.PLATFORM_ESP32 -> !it.dfuIsZip
                    else -> true
                }
            }
        }.sortedBy {
            // the board it runs now first
            val same = if (flashTarget == FlashTarget.COMPANION) BoardCheck.matches(it.env, companionModel) == true
                else it.env == running
            if (same) 0 else 1
        }
    }

    /** 'Ours' / 'Upstream' above the releases of the flash screen. */
    fun showUpstream(on: Boolean) {
        flashUpstream = on
        if (on && upstream.isEmpty()) loadUpstream()
    }

    fun loadUpstream() = viewModelScope.launch {
        val list = runCatching { withContext(Dispatchers.IO) { Releases.list(Releases.UPSTREAM_REPO, ctx.filesDir) } }
        upstream = list.getOrNull() ?: withContext(Dispatchers.IO) { Releases.cached(Releases.UPSTREAM_REPO, ctx.filesDir) }
        if (list.isFailure) {
            message = str(if (upstream.isEmpty()) R.string.msg_releases_failed else R.string.msg_releases_offline,
                list.exceptionOrNull()?.message ?: "")
            messageIsError = upstream.isEmpty()
        }
    }

    fun pickFlashRelease(f: FirmwareFile) = work {
        val data = withContext(Dispatchers.IO) { Releases.downloadDfu(f, ctx.filesDir) }
        flashImage = FlashImage(f.dfu, data, f.env, f.version.ifEmpty { f.release })
    }

    fun pickFlashFile(uri: Uri) = work {
        val data = withContext(Dispatchers.IO) { ctx.contentResolver.openInputStream(uri)!!.use { it.readBytes() } }
        val name = fileName(uri)
        flashImage = if (name.endsWith(".uf2", ignoreCase = true)) {
            // the USB drive's file: made into the Bluetooth DFU .zip here
            val zip = runCatching { withContext(Dispatchers.Default) { io.github.n3tk0.loraota.flash.Uf2.toDfuZip(data) } }
                .getOrElse { throw Exception(str(R.string.err_flash_uf2, it.message ?: "")) }
            FlashImage(name.dropLast(4) + ".zip", zip, null)
        } else FlashImage(name, data, null)
    }

    /** 'Flash': checks the file, wakes the repeater's DFU mode and finds it, then asks before it writes. */
    fun flashCheck() = work {
        val img = flashImage ?: return@work
        val warnings = ArrayList<String>()
        val zipData = img.data.size > 2 && img.data[0] == 'P'.code.toByte() && img.data[1] == 'K'.code.toByte()
        if (img.isZip != zipData) throw Exception(str(R.string.err_flash_bad_file))
        if (img.name.endsWith("-merged.bin", ignoreCase = true)) throw Exception(str(R.string.err_flash_need_bin))
        val env = img.env
        if (env == null) warnings.add(str(R.string.warn_board_unknown))

        if (flashTarget == FlashTarget.COMPANION) {
            if (!img.isZip) throw Exception(str(R.string.err_flash_need_zip))
            val dev = companionDevice ?: return@work
            if (env != null && !BoardCheck.fitsCompanion(env)) warnings.add(str(R.string.warn_not_companion, env))
            if (env != null && BoardCheck.matches(env, companionModel) == false) {
                warnings.add(str(R.string.warn_board, companionModel, env))
            }
            autoBackup(DeviceKind.COMPANION)?.let { warnings.add(it) }
            flashPlan = FlashPlan(img, "$companionName (${dev.address})", dev.address, warnings, companionName)
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
            message = str(if (flashTarget == FlashTarget.COMPANION) R.string.msg_flash_done_companion else R.string.msg_flash_done)
        }
    }

    private suspend fun flashBle(p: FlashPlan) {
        val address = p.address ?: return
        val zip = java.io.File(ctx.cacheDir, "flash.zip")
        withContext(Dispatchers.IO) { zip.writeBytes(p.image.data) }
        if (flashTarget == FlashTarget.COMPANION) {
            // the DFU library needs the device to itself
            flashingCompanion = true
            link.close()
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
        BleDfu.flash(ctx, address, p.name, zip, flashTarget == FlashTarget.COMPANION, log) { flashPercent = it }
    }

    private suspend fun flashWifi(img: FlashImage) {
        val r = repeater ?: return
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
        if (releases.isEmpty()) loadReleases()
    }

    fun closeQueue() {
        job?.cancel()
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
        queue?.retryFailed()
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
            val running = allFirmware.firstOrNull { it.sha256 == hex(i.imageSha) }
                ?: return QueueResult.SKIPPED to str(R.string.q_unknown_firmware)
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
            OtaService.start(ctx, notifyTitle(), str(R.string.phase_start))
            try {
                session(r).run(old, new, p, false)
            } catch (x: OtaFailed) {
                val why = str(x.res, *x.args.toTypedArray())
                return QueueResult.FAILED to if (x.resumable) str(R.string.q_failed_resumable, why) else why
            } finally {
                phase = null
                OtaService.stop(ctx)
            }
            val after = runCatching { session(r).info() }.getOrNull()
            val ident = after?.ident?.replace('|', ' ') ?: target.label
            return QueueResult.DONE to (if (noBackup != null) "$ident · $noBackup" else ident)
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
    var historyMissing by mutableStateOf(false)    // the node keeps no history (stock or older firmware)
    var battSave by mutableStateOf<BattSave?>(null)        // the repeater's `battsave` (null: not read / older firmware)
    var battCapacity by mutableStateOf<BattCapacity?>(null)
    var battGauge by mutableStateOf(false)                  // an INA219 counts the charge: capacity matters, voltage is exact
    var battChecked by mutableStateOf(false)                // the person says the voltage reading was checked
    private var healthReturn = Step.UPDATE

    val healthDeviceName get() = if (healthKind == DeviceKind.COMPANION) companionName else repeater?.name ?: ""

    fun openHealth(kind: DeviceKind) {
        healthReturn = step
        healthKind = kind
        selfTest = null
        selfTestMissing = false
        healthWarnings = emptyList()
        historyCache.clear()
        historyMissing = false
        historyField = HistorySeries.F_BATT_PCT
        historyRing = HistorySeries.RING_DAY
        battSave = null
        battCapacity = null
        battGauge = false
        battChecked = false
        message = ""
        val dev = deviceKey(kind)
        healthRecords = emptyList()
        step = Step.HEALTH
        viewModelScope.launch { healthRecords = withContext(Dispatchers.IO) { healthStore.records(dev) } }
        refreshHealth()
    }

    fun closeHealth() {
        job?.cancel()
        step = healthReturn
    }

    /** The self test (a repeater), then the battery's day and week (for 'drains faster than usual') and the series shown. */
    fun refreshHealth() = work {
        val kind = healthKind
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
                    val dev = deviceKey(kind)
                    val before = healthRecords.lastOrNull()?.let { SelfTest.parse(it.line) }
                    healthRecords = withContext(Dispatchers.IO) {
                        healthStore.add(dev, line)
                        healthStore.records(dev)
                    }
                    selfTest = t
                    warnings.addAll(HealthCheck.warnings(t, before))
                }
            }
        }
        historyCache.clear()
        historyMissing = false
        working = str(R.string.working_history)
        val day = loadSeries(HistorySeries.RING_DAY, HistorySeries.F_BATT_PCT)
        if (day != null) {
            // the week only when the last 6 h have enough samples for 'falls faster' (saves 1-3 packets)
            if (day.values.takeLast(36).count { it != null } >= 7) {
                val week = loadSeries(HistorySeries.RING_WEEK, HistorySeries.F_BATT_PCT)
                if (week != null && HealthCheck.fasterDrain(day.values, week.values)) warnings.add(HealthWarning.FAST_DRAIN)
            }
            // a node that does not measure the battery %: the first value it does measure
            historyField = HistorySeries.pick(historyField, day.fields)
            loadSeries(historyRing, historyField)
        } else {
            historyMissing = true
        }
        healthWarnings = warnings
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

    /** Commands to the repeater's console, then `battsave` read again; a reply other than OK is shown. */
    private fun battCommands(vararg commands: String) = work {
        val r = repeater ?: return@work
        try {
            for (c in commands) {
                working = c
                val reply = RepeaterBackup.ask(companion, r, c)?.removePrefix("> ")?.trim()
                    ?: throw Exception(str(R.string.health_no_answer))
                if (!reply.startsWith("OK")) throw Exception(reply)
            }
            readBattery(r, battGauge)
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
    fun setBattCapacity(mah: Int) = battCommands("${BattCapacity.COMMAND} $mah")

    fun showHistory(ring: Int, field: Int) {
        historyRing = ring
        historyField = field
        if (historyMissing || historyCache.containsKey(ring to field)) return
        work {
            working = str(R.string.working_history)
            loadSeries(ring, field)
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
        if (kind == DeviceKind.REPEATER && canReadRepeater == null) {
            viewModelScope.launch { canReadRepeater = runCatching { RepeaterBackup.supported(companion) }.getOrDefault(false) }
        }
    }

    fun closeBackup() {
        restoreFrom = null
        importText = null
        step = backupReturn
    }

    /** This device's backups first, then those of others of its kind (e.g. for a board that replaces one). */
    private fun loadBackups() {
        val dev = backupDevice
        val kind = backupKind
        viewModelScope.launch {
            // the keys are decrypted as they are read: not on the main thread
            val all = withContext(Dispatchers.IO) { backupStore.all() }
            backups = all.filter { it.kind == kind }.sortedBy { if (it.deviceKey == dev) 0 else 1 }
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

    /** The workflow input the board is picked in: the companion's own flash screen builds a companion. */
    val buildRole get() = if (step == Step.FLASH && flashTarget == FlashTarget.COMPANION) "companion" else "repeater"

    private fun api(): GitHubApi = GitHubApi(tokens.get() ?: run {
        hasToken = false
        throw Exception(str(R.string.build_err_token))
    }, repo)

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
        hasToken = true
        groupToken = false
        message = str(R.string.msg_token_saved)
        readBuildForm()
    }

    fun forgetToken() {
        tokens.clear()
        hasToken = tokens.has   // the group's token stays
        groupToken = tokens.usesGroup
        buildInputs = emptyList()
    }

    /** The device the form is for: another repeater (or the companion) gets its own board. */
    private fun buildDevice() = if (buildRole == "companion") companionDevice?.address ?: ""
        else repeater?.let { hex(it.publicKey) } ?: ""

    /**
     * The boards and boxes of the workflow on GitHub, once per screen (repeater or companion); the board
     * again for another device, and when the running firmware (which names it) is found later.
     */
    fun loadBuildForm() {
        if (!hasToken) return
        if (buildInputs.isEmpty() || buildInputsRole != buildRole) {
            work { readBuildForm() }
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
    private fun buildFinished(j: BuildJob) {
        builds = Releases.builds(ctx.filesDir)
        val made = builds.filter { it.release == FirmwareFile.BUILD_PREFIX + j.runNumber && it.env == j.board }
        if (busy || j.role != buildRole) return
        if (step == Step.UPDATE && firmwareTab == 0 && loraSource == Source.BUILD) {
            val f = made.firstOrNull { it.hasLoraOta && it in newCandidates() } ?: return
            loraSource = Source.RELEASES
            pickRelease(f)
        } else if (flashSource == Source.BUILD && (step == Step.FLASH || firmwareTab == 1)) {
            val f = made.firstOrNull { it.hasDfu } ?: return
            flashSource = Source.RELEASES
            flashUpstream = false
            if (f in flashCandidates()) pickFlashRelease(f)
        }
    }

    override fun onCleared() {
        OtaService.stop(ctx)
        link.close()
    }
}
