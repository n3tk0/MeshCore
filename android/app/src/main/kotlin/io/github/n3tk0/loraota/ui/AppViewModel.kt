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
import io.github.n3tk0.loraota.R
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
    private val link = CompanionLink(app)
    private val companion = CompanionRadio(link)

    var step by mutableStateOf(Step.COMPANION)
    var busy by mutableStateOf(false)
    var message by mutableStateOf("")
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

    var sent by mutableStateOf(0)
    var total by mutableStateOf(0)
    var done by mutableStateOf(false)
    private var job: Job? = null

    init {
        link.onDisconnect = {
            viewModelScope.launch {
                if (step != Step.COMPANION) {
                    message = ctx.getString(R.string.msg_disconnected)
                    step = Step.COMPANION
                }
            }
        }
    }

    private fun work(block: suspend () -> Unit) {
        if (busy) return
        busy = true
        message = ""
        job = viewModelScope.launch {
            try {
                block()
            } catch (e: kotlinx.coroutines.CancellationException) {
                message = ctx.getString(R.string.msg_stopped)
            } catch (e: Exception) {
                message = e.message ?: e.toString()
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
            message = ctx.getString(R.string.msg_bluetooth_off)
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
        if (!link.connect(device)) throw Exception(ctx.getString(R.string.msg_connect_failed))
        companion.start()
        companionName = companion.nodeName.ifEmpty { device.name ?: device.address }
        repeaters.clear()
        repeaters.addAll(companion.contacts().filter { it.isRepeater }.sortedBy { it.name.lowercase() })
        step = Step.REPEATER
    }

    fun disconnect() {
        stop()
        viewModelScope.launch { restorePath() }
        link.close()
        step = Step.COMPANION
    }

    // ---- repeater ----

    fun login(contact: Contact, password: String) = work {
        if (!companion.login(contact, password)) throw Exception(ctx.getString(R.string.msg_not_admin))
        companion.setZeroHop(contact)
        repeater = contact
        val session = OtaSession(companion, contact, sideRadio(), duty(), {}, { _, _ -> })
        val i = session.info()
        info = i
        oldImage = null
        newImage = null
        patch = null
        done = false
        log.clear()
        step = Step.UPDATE
        loadReleases()
    }

    private suspend fun restorePath() {
        val r = repeater ?: return
        runCatching { companion.restorePath(r) }
    }

    fun backToRepeaters() {
        stop()
        viewModelScope.launch { restorePath() }
        repeater = null
        info = null
        step = Step.REPEATER
    }

    // ---- firmware ----

    fun loadReleases() = viewModelScope.launch {
        val list = runCatching { withContext(Dispatchers.IO) { Releases.list(repo) } }
        releases = list.getOrDefault(emptyList())
        if (list.isFailure) message = ctx.getString(R.string.msg_releases_failed, list.exceptionOrNull()?.message ?: "")
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
        return releases.filter { (env == null || it.env == env) && it.sha256 != oldImage?.sha }
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
            if (i != null && img.sha != hex(i.imageSha)) throw Exception(ctx.getString(R.string.msg_not_running_firmware))
            oldImage = img
        } else {
            newImage = img
        }
        patch = null
    }

    fun makeDelta() = work {
        val old = oldImage ?: return@work
        val new = newImage ?: return@work
        val i = info
        if (i != null && new.data.size > i.maxNew) throw Exception(ctx.getString(R.string.msg_too_big))
        patch = withContext(Dispatchers.Default) { DeltaGenerator.create(old.data, new.data) }
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
        val r = repeater ?: return null
        return OtaSession(companion, r, sideRadio(), duty(), {}, { _, _ -> }).estimateMinutes(p.size)
    }

    fun send() = work {
        val r = repeater ?: return@work
        val p = patch ?: return@work
        val old = oldImage ?: return@work
        val new = newImage ?: return@work
        done = false
        log.clear()
        val session = OtaSession(companion, r, sideRadio(), duty(), { log.add(it) }) { s, t ->
            sent = s
            total = t
        }
        session.run(old.data, new.data, p)
        done = true
        message = ctx.getString(R.string.msg_done)
    }

    /** After the restart: asks the repeater on the normal channel which firmware it runs now. */
    fun verify() = work {
        val r = repeater ?: return@work
        val i = OtaSession(companion, r, sideRadio(), duty(), {}, { _, _ -> }).info()
        info = i
        message = if (newImage != null && hex(i.imageSha) == newImage!!.sha) {
            ctx.getString(R.string.msg_verified, i.ident)
        } else {
            ctx.getString(R.string.msg_not_verified, i.ident)
        }
    }

    override fun onCleared() {
        link.close()
    }
}
