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
        scanning = true
        val filter = ScanFilter.Builder().setServiceUuid(ParcelUuid(CompanionLink.SERVICE)).build()
        scanner.startScan(listOf(filter), ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(), scanCallback)
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
        repeaters.addAll(companion.contacts().filter { it.isRepeater }.sortedBy { it.name.lowercase() })
        step = Step.REPEATER
    }

    fun disconnect() {
        step = Step.COMPANION
        viewModelScope.launch {
            job?.cancelAndJoin()   // the update's own clean-up (ABORT, normal radio) goes first
            restorePath()
            link.close()
        }
    }

    // ---- repeater ----

    fun login(contact: Contact, password: String) = work {
        // log in by flood, so both sides learn the route: the update only goes when it has no hop
        companion.resetPath(contact)
        try {
            if (!companion.login(contact, password)) throw Exception(str(R.string.msg_not_admin))
            delay(4000)   // the companion sends its route back to the repeater
            val hops = companion.contact(contact.publicKey)?.outPathLen ?: 0xFF
            if (hops == 0xFF || (hops and 63) != 0) {
                throw Exception(if (hops == 0xFF) str(R.string.msg_no_route) else str(R.string.msg_not_direct, hops and 63))
            }
        } catch (e: Exception) {
            runCatching { companion.restorePath(contact) }
            throw e
        }
        repeater = contact
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

    private fun session(contact: Contact) = OtaSession(companion, contact, sideRadio(), budget.also { it.duty = duty() },
        { log.add(it) }, { p, until -> phase = p; waitUntil = until }) { s, t ->
        sent = s
        total = t
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
        val list = runCatching { withContext(Dispatchers.IO) { Releases.list(repo) } }
        releases = list.getOrDefault(emptyList())
        if (list.isFailure) {
            message = str(R.string.msg_releases_failed, list.exceptionOrNull()?.message ?: "")
            messageIsError = true
        }
        // the firmware the repeater runs, if a release has it
        val i = info ?: return@launch
        val running = releases.firstOrNull { it.sha256 == hex(i.imageSha) }
        if (running != null && oldImage == null) {
            runCatching { withContext(Dispatchers.IO) { Releases.download(running) } }
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
        val data = withContext(Dispatchers.IO) { Releases.download(f) }
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

    fun duty(): Double = ((dutyPercent.toDoubleOrNull() ?: 10.0) / 100.0).coerceIn(0.01, 1.0)

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
            if (i != null && new.data.size > i.maxNew) throw Exception(str(R.string.msg_too_big))
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
            try {
                session(r).run(old.data, new.data, p)
            } catch (e: OtaFailed) {
                resumable = e.resumable
                throw e
            } catch (e: kotlinx.coroutines.CancellationException) {
                resumable = false
                throw e
            } finally {
                phase = null
            }
            resumable = false
            done = true
            info = runCatching { session(r).info() }.getOrNull() ?: info
            message = str(R.string.msg_verified, info?.ident?.replace('|', ' ') ?: "")
        }
    }

    override fun onCleared() {
        link.close()
    }
}
