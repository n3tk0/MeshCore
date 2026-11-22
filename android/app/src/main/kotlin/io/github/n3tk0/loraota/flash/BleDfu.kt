package io.github.n3tk0.loraota.flash

import android.annotation.SuppressLint
import android.app.Activity
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothManager
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.os.Build
import io.github.n3tk0.loraota.Lang
import io.github.n3tk0.loraota.MainActivity
import io.github.n3tk0.loraota.R
import io.github.n3tk0.loraota.ble.scanNeedsLocation
import io.github.n3tk0.loraota.ble.scanNeedsLocationPermission
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.delay
import no.nordicsemi.android.dfu.DfuBaseService
import no.nordicsemi.android.dfu.DfuLogListener
import no.nordicsemi.android.dfu.DfuProgressListenerAdapter
import no.nordicsemi.android.dfu.DfuServiceInitiator
import no.nordicsemi.android.dfu.DfuServiceListenerHelper
import java.io.File

/** The Nordic DFU library's service (declared in the manifest). */
class DfuService : DfuBaseService() {
    override fun getNotificationTarget(): Class<out Activity> = MainActivity::class.java
    override fun isDebug() = false

    /**
     * Always clear Android's GATT cache, bonded or not. The library skips it for a bonded device and relies on
     * the Service Changed indication; a companion (PIN bond) whose bootloader comes back on the same address
     * then gets the application's cached handles: the first write to the bootloader's DFU characteristics
     * fails with GATT INVALID HANDLE (seen on the Wio Tracker L1). A fresh discovery costs a second.
     */
    override fun refreshDeviceCache(gatt: BluetoothGatt, force: Boolean) = super.refreshDeviceCache(gatt, true)
}

/** A device seen advertising its DFU mode: after `start ota` a MeshCore nRF52 node is "<BOARD>_OTA". */
class DfuDevice(val address: String, val name: String, val rssi: Int)

open class FlashFailed(msg: String) : Exception(msg)

@SuppressLint("MissingPermission")
object BleDfu {
    /** Devices with a MeshCore DFU name (or the Adafruit bootloader's) seen within [ms], the strongest first. */
    suspend fun scan(ctx: Context, ms: Long): List<DfuDevice> {
        val scanner = (ctx.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter?.bluetoothLeScanner
            ?: throw FlashFailed("Bluetooth")
        if (scanNeedsLocationPermission(ctx)) throw FlashFailed(Lang.wrap(ctx).getString(R.string.msg_no_location_permission))
        if (scanNeedsLocation(ctx)) throw FlashFailed(Lang.wrap(ctx).getString(R.string.msg_location_off))
        val found = LinkedHashMap<String, DfuDevice>()
        var scanError: Int? = null
        val cb = object : ScanCallback() {
            override fun onScanResult(callbackType: Int, r: ScanResult) {
                val devName = try {
                    r.device.name
                } catch (_: SecurityException) {
                    null
                }
                val name = r.scanRecord?.deviceName ?: devName ?: return
                if (name.endsWith("_OTA", ignoreCase = true) || name == "AdaDFU") {
                    synchronized(found) { found[r.device.address] = DfuDevice(r.device.address, name, r.rssi) }
                }
            }

            override fun onScanFailed(errorCode: Int) {
                scanError = errorCode
            }
        }
        try {
            scanner.startScan(null, ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(), cb)
        } catch (e: SecurityException) {
            throw FlashFailed(Lang.wrap(ctx).getString(io.github.n3tk0.loraota.ble.scanPermissionMessage()))
        }
        try {
            delay(ms)
        } finally {
            runCatching { scanner.stopScan(cb) }
        }
        scanError?.let { code ->
            if (synchronized(found) { found.isEmpty() }) throw FlashFailed("BLE scan failed ($code)")
        }
        return synchronized(found) { found.values.sortedByDescending { it.rssi } }
    }

    /** The address a nRF52 bootloader advertises on after losing the app's peer data (as the DFU library does). */
    private fun incremented(address: String): String =
        address.substring(0, 15) + "%02X".format(java.util.Locale.US, (address.substring(15).toInt(16) + 1) and 0xFF)

    /**
     * Scans up to [ms] for the device on [address] or its bootloader on `address + 1` and returns the address
     * to flash: [address] when the device answers there, `address + 1` when only a bootloader there does (a
     * previous attempt left it in DFU mode), else [address]. Also, `connectGatt(autoConnect = false)` before
     * Android has seen an advertisement of the device often fails within 200 ms with 0x85 (GATT_ERROR).
     * Other devices' `*_OTA` / `AdaDFU` advertisements are ignored: they may be another node in DFU mode.
     */
    private suspend fun awaitAdvertising(ctx: Context, address: String, ms: Long, log: (String) -> Unit,
                                         fallback: String = address): String {
        val scanner = (ctx.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter?.bluetoothLeScanner
            ?: return fallback
        val plusOne = incremented(address.uppercase())
        val exact = CompletableDeferred<Unit>()
        val bootloader = CompletableDeferred<Unit>()
        val sawBootloader = java.util.concurrent.atomic.AtomicBoolean(false)
        val cb = object : ScanCallback() {
            override fun onScanResult(callbackType: Int, r: ScanResult) {
                val addr = r.device.address
                if (addr.equals(address, ignoreCase = true)) exact.complete(Unit)
                else if (addr.equals(plusOne, ignoreCase = true)) { sawBootloader.set(true); bootloader.complete(Unit) }
            }
        }
        runCatching {
            scanner.startScan(null, ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(), cb)
        }.onFailure { return fallback }
        val found = try {
            kotlinx.coroutines.withTimeoutOrNull(ms) {
                // the device's own address wins; once a bootloader on + 1 shows up, give the address only
                // a short grace (both should not advertise at once) instead of the whole timeout
                kotlinx.coroutines.selects.select<Unit> {
                    exact.onAwait {}
                    bootloader.onAwait {}
                }
                if (!exact.isCompleted) kotlinx.coroutines.withTimeoutOrNull(500) { exact.await() }
            }.let { exact.isCompleted }
        } finally {
            runCatching { scanner.stopScan(cb) }
        }
        return when {
            found -> address
            sawBootloader.get() -> plusOne.also { log("[app] bootloader found on $it, flashing there") }
            else -> fallback.also { log("[app] $address not seen advertising in ${ms / 1000} s, trying $it anyway") }
        }
    }

    /**
     * Flashes the DFU .zip: the device's application jumps to its bootloader (legacy buttonless DFU, the
     * Adafruit BLEDfu service), then the image goes over. [progress] gets 0..100, [log] the library's steps.
     * [bonded]: a companion (PIN bond kept); a repeater is never bonded.
     */
    suspend fun flash(ctx: Context, address: String, name: String, zip: File, bonded: Boolean, log: (String) -> Unit,
              progress: (Int) -> Unit) {
        try {
            val target = awaitAdvertising(ctx, address, 3_000, log)
            flashOnce(ctx, target, name, zip, bonded, log, progress)
        } catch (e: GattFailed) {
            // a GATT error (e.g. INVALID HANDLE from stale cached services, or 0x85 GATT_ERROR on connect):
            // the cache is cleared by now. Retry on the address the library last used: the bootloader may
            // have come up on the address + 1
            val again = e.address.ifEmpty { address }
            log("[app] ${e.message}: retrying once with fresh services on $again")
            progress(0)
            delay(2000)
            // scan around the original address (the device itself or its bootloader on + 1), not around
            // [again]: when that is already + 1 its own + 1 would be a different device
            val target = awaitAdvertising(ctx, address, 4_000, log, fallback = again)
            flashOnce(ctx, target, name, zip, bonded, log, progress)
        }
    }

    private class GattFailed(msg: String, val address: String) : FlashFailed(msg)

    private suspend fun flashOnce(ctx: Context, address: String, name: String, zip: File, bonded: Boolean,
                                  log: (String) -> Unit, progress: (Int) -> Unit) {
        // the forced scan for the bootloader's address + 1 needs Location on Android 7-11 as well
        if (scanNeedsLocationPermission(ctx)) throw FlashFailed(Lang.wrap(ctx).getString(R.string.msg_no_location_permission))
        if (scanNeedsLocation(ctx)) throw FlashFailed(Lang.wrap(ctx).getString(R.string.msg_location_off))
        val done = CompletableDeferred<Unit>()
        val listener = object : DfuProgressListenerAdapter() {
            override fun onProgressChanged(deviceAddress: String, percent: Int, speed: Float, avgSpeed: Float,
                                           currentPart: Int, partsTotal: Int) = progress(percent)
            override fun onDfuCompleted(deviceAddress: String) { done.complete(Unit) }
            override fun onDfuAborted(deviceAddress: String) { done.completeExceptionally(FlashFailed("aborted")) }
            override fun onError(deviceAddress: String, error: Int, errorType: Int, message: String?) {
                val msg = message ?: "DFU error $error"
                val commError = errorType == DfuBaseService.ERROR_TYPE_COMMUNICATION ||
                    errorType == DfuBaseService.ERROR_TYPE_COMMUNICATION_STATE
                done.completeExceptionally(if (commError) GattFailed(msg, deviceAddress) else FlashFailed(msg))
            }
        }
        val logs = DfuLogListener { _, _, message -> log(message) }
        // no address filter: the bootloader may come back on another address
        DfuServiceListenerHelper.registerProgressListener(ctx, listener)
        DfuServiceListenerHelper.registerLogListener(ctx, logs)
        try {
            val starter = DfuServiceInitiator(address)
                .setDeviceName(name)
                .setForceDfu(false)
                .setUnsafeExperimentalButtonlessServiceInSecureDfuEnabled(true)
                // after the jump the Xiao's bootloader did not answer on the app's address (log: connect to
                // the same address, GATT CONN TIMEOUT x3): it comes up on the address + 1 when it lost the
                // app's peer data. Scan for both, then the same address as before
                .setForceScanningForNewAddressInLegacyDfu(true)
                .setScanTimeout(10_000)   // the bootloader starts its SoftDevice first
            if (bonded) {
                starter.setKeepBond(true)   // the companion's PIN bond stays
                    .setPacketsReceiptNotificationsEnabled(true)
                    .setPacketsReceiptNotificationsValue(8)
            }
            // a repeater (never bonded): no receipts, no kept bond, as the nRF Device Firmware Update app
            starter
                // a foreground service: Android does not stop it mid-write when the app goes to the background
                .setForeground(true)
                .setDisableNotification(false)
                .setZip(zip.path)
            if (Build.VERSION.SDK_INT >= 26) DfuServiceInitiator.createDfuNotificationChannel(ctx)
            val controller = starter.start(ctx, DfuService::class.java)
            try {
                done.await()
            } catch (e: CancellationException) {
                controller.abort()
                throw e
            }
        } finally {
            DfuServiceListenerHelper.unregisterProgressListener(ctx, listener)
            DfuServiceListenerHelper.unregisterLogListener(ctx, logs)
        }
    }
}
