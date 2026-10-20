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
import android.os.ParcelUuid
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
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
import io.github.n3tk0.loraota.ble.CompanionRadio
import io.github.n3tk0.loraota.ble.CompanionLink
import io.github.n3tk0.loraota.ble.Contact
import io.github.n3tk0.loraota.delta.DeltaGenerator
import io.github.n3tk0.loraota.delta.DetoolsPatch
import io.github.n3tk0.loraota.ota.FirmwareFile
import io.github.n3tk0.loraota.ota.OtaProtocol
import io.github.n3tk0.loraota.ota.OtaSession
import io.github.n3tk0.loraota.ota.Releases
import io.github.n3tk0.loraota.ota.SideRadio
import io.github.n3tk0.loraota.ota.hex
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

enum class Step { COMPANION, REPEATER, UPDATE }

/** A firmware image the user has: from a release or from a file. */
class Image(val name: String, val data: ByteArray, val env: String? = null) {
    val sha: String = hex(DetoolsPatch.sha256(data))
}

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
    val log = mutableStateListOf<String>()

    // companion
    val devices = mutableStateListOf<BluetoothDevice>()
    var scanning by mutableStateOf(false)
    var companionName by mutableStateOf("")

    // repeater
    val repeaters = mutableStateListOf<Contact>()
    var repeater by mutableStateOf<Contact?>(null)
    var info by mutableStateOf<OtaProtocol.Info?>(null)

    // firmware
    var releases by mutableStateOf<List<FirmwareFile>>(emptyList())
    var oldImage by mutableStateOf<Image?>(null)
    var newImage by mutableStateOf<Image?>(null)
    var patch by mutableStateOf<ByteArray?>(null)
    var repo by mutableStateOf(Releases.DEFAULT_REPO)

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

    init {
        link.onDisconnect = {
            viewModelScope.launch {
                if (step != Step.COMPANION) {
                    message = str(R.string.msg_disconnected)
                    messageIsError = true
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
                job = null
            }
        }
    }

    fun stop() {
        job?.cancel()
    }

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
        if (!link.connect(device)) throw Exception(str(R.string.msg_connect_failed))
        companion.start()
        runCatching { companion.cli("tempradio off") }   // left on the side channel by an update cut short
        companionName = companion.nodeName.ifEmpty { device.name ?: device.address }
        repeaters.clear()
        // the ones next to us first: only those can be updated
        repeaters.addAll(companion.contacts().filter { it.isRepeater }
            .sortedWith(compareBy<Contact>({ it.hops ?: Int.MAX_VALUE }, { it.name.lowercase() })))
        step = Step.REPEATER
    }

    fun disconnect() {
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

    fun login(contact: Contact, password: String) = work {
        // log in by flood, so both sides learn the route: the update only goes when it has no hop
        companion.resetPath(contact)
        val learned = try {
            if (!companion.login(contact, password)) throw Exception(str(R.string.msg_not_admin))
            delay(4000)   // the companion sends its route back to the repeater
            val now = companion.contact(contact.publicKey)
            val hops = now?.outPathLen ?: 0xFF
            if (now == null || hops == 0xFF || (hops and 63) != 0) {
                throw Exception(if (hops == 0xFF) str(R.string.msg_no_route) else str(R.string.msg_not_direct, hops and 63))
            }
            now
        } catch (e: Exception) {
            runCatching { companion.restorePath(contact) }
            throw e
        }
        savePassword(contact, password)
        // the direct route just learned is the one to keep afterwards, not the older (longer) one
        repeater = learned
        val i = session(contact).info()
        info = i
        oldImage = null
        newImage = null
        patch = null
        done = false
        sent = 0
        total = 0
        phase = null
        resumable = i.state == OtaProtocol.STATE_PAUSED
        log.clear()
        step = Step.UPDATE
        loadReleases()
    }

    // admin passwords, per repeater, only on this phone (the app has no backup)
    private val passwords get() = ctx.getSharedPreferences("passwords", Context.MODE_PRIVATE)

    fun savedPassword(contact: Contact): String? = passwords.getString(hex(contact.publicKey), null)

    private fun savePassword(contact: Contact, password: String) {
        passwords.edit().putString(hex(contact.publicKey), password).apply()
    }

    fun forgetPassword(contact: Contact) {
        passwords.edit().remove(hex(contact.publicKey)).apply()
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
        }
    }

    // ---- firmware ----

    fun loadReleases() = viewModelScope.launch {
        val list = runCatching { withContext(Dispatchers.IO) { Releases.list(repo, ctx.filesDir) } }
        releases = list.getOrNull() ?: withContext(Dispatchers.IO) { Releases.cached(repo, ctx.filesDir) }
        if (list.isFailure) {
            val why = list.exceptionOrNull()?.message ?: ""
            if (releases.isNotEmpty()) {
                message = str(R.string.msg_releases_offline, why)   // no internet on site: the list kept last time
                messageIsError = false
            } else {
                message = str(R.string.msg_releases_failed, why)
                messageIsError = true
            }
        }
        // the firmware the repeater runs, if a release has it
        val i = info ?: return@launch
        val running = releases.firstOrNull { it.sha256 == hex(i.imageSha) }
        if (running != null && oldImage == null) {
            runCatching { withContext(Dispatchers.IO) { Releases.download(running, ctx.filesDir) } }
                .onSuccess { oldImage = Image(running.file, it, running.env) }
        }
    }

    /** Releases for the same board as the one the repeater runs (all when that is not known). */
    fun newCandidates(): List<FirmwareFile> {
        val env = oldImage?.env ?: releases.firstOrNull { it.sha256 == oldImage?.sha }?.env
        val nrf = info?.platform == OtaProtocol.PLATFORM_NRF52
        return releases.filter {
            // unknown board: at least the same chip family (the repeater also refuses an image for another chip)
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
        val name = uri.lastPathSegment?.substringAfterLast('/') ?: "file.bin"
        val img = Image(name, data)
        if (isOld) {
            val i = info
            if (i != null && img.sha != hex(i.imageSha)) throw Exception(str(R.string.msg_not_running_firmware))
            oldImage = img
        } else {
            newImage = img
        }
        patch = null
    }

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

    override fun onCleared() {
        OtaService.stop(ctx)
        link.close()
    }
}
