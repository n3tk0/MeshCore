package io.github.n3tk0.loraota.flash

import android.annotation.SuppressLint
import android.app.Activity
import android.bluetooth.BluetoothManager
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import io.github.n3tk0.loraota.MainActivity
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.delay
import no.nordicsemi.android.dfu.DfuBaseService
import no.nordicsemi.android.dfu.DfuProgressListenerAdapter
import no.nordicsemi.android.dfu.DfuServiceInitiator
import no.nordicsemi.android.dfu.DfuServiceListenerHelper
import java.io.File

/** The Nordic DFU library's service (declared in the manifest). */
class DfuService : DfuBaseService() {
    override fun getNotificationTarget(): Class<out Activity> = MainActivity::class.java
    override fun isDebug() = false
}

/** A device seen advertising its DFU mode: after `start ota` a MeshCore nRF52 node is "<BOARD>_OTA". */
class DfuDevice(val address: String, val name: String, val rssi: Int)

class FlashFailed(msg: String) : Exception(msg)

@SuppressLint("MissingPermission")
object BleDfu {
    /** Devices with a MeshCore DFU name (or the Adafruit bootloader's) seen within [ms], the strongest first. */
    suspend fun scan(ctx: Context, ms: Long): List<DfuDevice> {
        val scanner = (ctx.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter?.bluetoothLeScanner
            ?: throw FlashFailed("Bluetooth")
        val found = LinkedHashMap<String, DfuDevice>()
        val cb = object : ScanCallback() {
            override fun onScanResult(callbackType: Int, r: ScanResult) {
                val name = r.scanRecord?.deviceName ?: r.device.name ?: return
                if (name.endsWith("_OTA", ignoreCase = true) || name == "AdaDFU") {
                    synchronized(found) { found[r.device.address] = DfuDevice(r.device.address, name, r.rssi) }
                }
            }
        }
        scanner.startScan(null, ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(), cb)
        try {
            delay(ms)
        } finally {
            runCatching { scanner.stopScan(cb) }
        }
        return synchronized(found) { found.values.sortedByDescending { it.rssi } }
    }

    /**
     * Flashes the DFU .zip: the device's application jumps to its bootloader (legacy buttonless DFU, the
     * Adafruit BLEDfu service), then the image goes over. [progress] gets 0..100.
     */
    suspend fun flash(ctx: Context, address: String, name: String, zip: File, progress: (Int) -> Unit) {
        val done = CompletableDeferred<Unit>()
        val listener = object : DfuProgressListenerAdapter() {
            override fun onProgressChanged(deviceAddress: String, percent: Int, speed: Float, avgSpeed: Float,
                                           currentPart: Int, partsTotal: Int) = progress(percent)
            override fun onDfuCompleted(deviceAddress: String) { done.complete(Unit) }
            override fun onDfuAborted(deviceAddress: String) { done.completeExceptionally(FlashFailed("aborted")) }
            override fun onError(deviceAddress: String, error: Int, errorType: Int, message: String?) {
                done.completeExceptionally(FlashFailed(message ?: "DFU error $error"))
            }
        }
        // no address filter: the bootloader may come back on another address
        DfuServiceListenerHelper.registerProgressListener(ctx, listener)
        try {
            val starter = DfuServiceInitiator(address)
                .setDeviceName(name)
                .setKeepBond(true)   // the companion's PIN bond stays
                .setForceDfu(false)
                .setPacketsReceiptNotificationsEnabled(true)
                .setPacketsReceiptNotificationsValue(8)
                .setUnsafeExperimentalButtonlessServiceInSecureDfuEnabled(true)
                // a foreground service: Android does not stop it mid-write when the app goes to the background
                .setForeground(true)
                .setDisableNotification(false)
                .setZip(zip.path)
            DfuServiceInitiator.createDfuNotificationChannel(ctx)
            val controller = starter.start(ctx, DfuService::class.java)
            try {
                done.await()
            } catch (e: CancellationException) {
                controller.abort()
                throw e
            }
        } finally {
            DfuServiceListenerHelper.unregisterProgressListener(ctx, listener)
        }
    }
}
