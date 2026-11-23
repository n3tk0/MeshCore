package io.github.n3tk0.loraota.ble

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothProfile
import android.bluetooth.BluetoothStatusCodes
import android.content.Context
import android.os.Build
import android.os.Handler
import android.os.Looper
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.delay
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withTimeoutOrNull
import java.util.UUID

/**
 * The companion's Bluetooth link (Nordic UART service): one write = one command frame,
 * one notification = one reply or push frame.
 */
@SuppressLint("MissingPermission")
class CompanionLink(private val context: Context) {
    companion object {
        val SERVICE: UUID = UUID.fromString("6E400001-B5A3-F393-E0A9-E50E24DCCA9E")
        val RX: UUID = UUID.fromString("6E400002-B5A3-F393-E0A9-E50E24DCCA9E")   // we write
        val TX: UUID = UUID.fromString("6E400003-B5A3-F393-E0A9-E50E24DCCA9E")   // it notifies
        val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
        private const val GATT_ERROR_133 = 133
        private const val MTU_FALLBACK_MS = 1_500L
    }

    val frames = Channel<ByteArray>(Channel.UNLIMITED)
    @Volatile var onDisconnect: (() -> Unit)? = null
    /** Human-readable reason for the last failed [connect] attempt, empty on success. */
    @Volatile var lastError: String = ""
        private set
    /** Last GATT status code from a failed [connect] attempt (0 when not a GATT status error). */
    @Volatile var lastStatus: Int = 0
        private set

    private val gattLock = Any()
    private val mainHandler = Handler(Looper.getMainLooper())
    @Volatile private var mtuFallback: Runnable? = null
    @Volatile private var gatt: BluetoothGatt? = null
    @Volatile private var rx: BluetoothGattCharacteristic? = null
    @Volatile private var connected: CompletableDeferred<Boolean>? = null
    @Volatile private var written: CompletableDeferred<Boolean>? = null
    @Volatile private var servicesDiscovered = false
    /** True only after CCCD enable succeeded; prevents firing onDisconnect on failed initial connects. */
    @Volatile private var established = false
    private val writeLock = Mutex()
    /** The link being closed by [closeAndRefresh]: its disconnect is awaited there, not closed here. */
    @Volatile private var closing: Pair<BluetoothGatt, CompletableDeferred<Unit>>? = null

    private fun failConnect(reason: String, status: Int = 0) {
        cancelMtuFallback()
        lastError = reason
        lastStatus = status
        connected?.complete(false)
    }

    private fun cancelMtuFallback() {
        mtuFallback?.let { mainHandler.removeCallbacks(it) }
        mtuFallback = null
    }

    private fun startDiscovery(g: BluetoothGatt) {
        cancelMtuFallback()
        synchronized(gattLock) {
            if (g != gatt || servicesDiscovered) return
            servicesDiscovered = true
        }
        if (!g.discoverServices()) failConnect("discoverServices")
    }

    private val callback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            val isCurrent: Boolean
            val closingMatch: CompletableDeferred<Unit>?
            synchronized(gattLock) {
                isCurrent = (g == gatt)
                closingMatch = closing?.takeIf { it.first == g }?.second
            }
            if (!isCurrent) {
                if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                    if (closingMatch != null) closingMatch.complete(Unit) else runCatching { g.close() }
                }
                return
            }
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                if (status != BluetoothGatt.GATT_SUCCESS) {
                    failConnect("GATT $status", status)
                    return
                }
                if (!g.requestMtu(247)) {
                    startDiscovery(g)
                } else {
                    // Android 14/15 may skip onMtuChanged if MTU was already negotiated on the link
                    cancelMtuFallback()
                    val fallback = Runnable { startDiscovery(g) }
                    mtuFallback = fallback
                    mainHandler.postDelayed(fallback, MTU_FALLBACK_MS)
                }
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                if (connected?.isCompleted == false) {
                    failConnect(if (status != BluetoothGatt.GATT_SUCCESS) "GATT $status" else "disconnected", status)
                }
                written?.complete(false)
                runCatching { g.close() }
                val notify = synchronized(gattLock) {
                    if (gatt == g) {
                        val wasEstablished = established
                        gatt = null
                        rx = null
                        servicesDiscovered = false
                        established = false
                        wasEstablished
                    } else false
                }
                if (notify) onDisconnect?.invoke()
            }
        }

        override fun onMtuChanged(g: BluetoothGatt, mtu: Int, status: Int) {
            if (synchronized(gattLock) { g != gatt }) return
            startDiscovery(g)
        }

        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            if (synchronized(gattLock) { g != gatt }) return
            if (status != BluetoothGatt.GATT_SUCCESS) {
                failConnect("services GATT $status", status)
                return
            }
            val service = g.getService(SERVICE)
            val tx = service?.getCharacteristic(TX)
            val rxChar = service?.getCharacteristic(RX)
            rx = rxChar
            if (tx == null || rxChar == null) {
                failConnect("no UART service")
                return
            }
            g.setCharacteristicNotification(tx, true)
            val d = tx.getDescriptor(CCCD)
            if (d == null) {
                failConnect("no CCCD")
                return
            }
            val ok = if (Build.VERSION.SDK_INT >= 33) {
                g.writeDescriptor(d, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE) == BluetoothStatusCodes.SUCCESS
            } else {
                @Suppress("DEPRECATION")
                d.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                @Suppress("DEPRECATION")
                g.writeDescriptor(d)
            }
            if (!ok) failConnect("CCCD write")
        }

        override fun onDescriptorWrite(g: BluetoothGatt, d: BluetoothGattDescriptor, status: Int) {
            if (synchronized(gattLock) { g != gatt }) return
            if (status == BluetoothGatt.GATT_SUCCESS) {
                synchronized(gattLock) {
                    if (g == gatt) established = true
                }
                lastError = ""
                lastStatus = 0
                connected?.complete(true)
            } else {
                failConnect("CCCD GATT $status", status)
            }
        }

        override fun onCharacteristicWrite(g: BluetoothGatt, c: BluetoothGattCharacteristic, status: Int) {
            if (synchronized(gattLock) { g != gatt }) return
            written?.complete(status == BluetoothGatt.GATT_SUCCESS)
        }

        @Deprecated("Deprecated in Java")
        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic) {
            if (synchronized(gattLock) { g != gatt }) return
            @Suppress("DEPRECATION")
            if (c.uuid == TX) {
                val v = c.value ?: return
                frames.trySend(v.copyOf())
            }
        }

        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic, value: ByteArray) {
            if (synchronized(gattLock) { g != gatt }) return
            if (c.uuid == TX) frames.trySend(value.copyOf())
        }
    }

    suspend fun connect(device: BluetoothDevice): Boolean {
        for (attempt in 1..2) {
            if (connectOnce(device)) return true
            if (attempt == 1 && lastStatus == GATT_ERROR_133) {
                delay(350)
                continue
            }
            return false
        }
        return false
    }

    private suspend fun connectOnce(device: BluetoothDevice): Boolean {
        close()
        lastError = ""
        lastStatus = 0
        val done = CompletableDeferred<Boolean>()
        connected = done
        synchronized(gattLock) {
            servicesDiscovered = false
            established = false
            val created = device.connectGatt(context, false, callback, BluetoothDevice.TRANSPORT_LE)
            gatt = created
            created
        } ?: run {
            connected = null
            lastError = "connectGatt null"
            return false
        }
        // pairing with the companion's PIN may take a while; timeout must NOT throw CancellationException
        return try {
            val ok = withTimeoutOrNull(60_000) { done.await() }
            if (ok == null) {
                lastError = "timeout"
                close()
                false
            } else {
                if (!ok) close()
                ok
            }
        } catch (e: CancellationException) {
            close()
            throw e
        }
    }

    suspend fun send(frame: ByteArray) = writeLock.withLock {
        val g = gatt ?: throw IllegalStateException("not connected")
        val c = rx ?: throw IllegalStateException("not connected")
        val done = CompletableDeferred<Boolean>()
        written = done
        val ok = if (Build.VERSION.SDK_INT >= 33) {
            g.writeCharacteristic(c, frame, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT) == BluetoothStatusCodes.SUCCESS
        } else {
            @Suppress("DEPRECATION")
            c.value = frame
            c.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
            @Suppress("DEPRECATION")
            g.writeCharacteristic(c)
        }
        // a timeout here is an error, not a cancellation: it must not look like the user's Stop
        if (!ok || withTimeoutOrNull(10_000) { done.await() } != true) throw IllegalStateException("Bluetooth write failed")
    }

    fun close() {
        cancelMtuFallback()
        val g = synchronized(gattLock) {
            val cur = gatt
            gatt = null
            rx = null
            servicesDiscovered = false
            established = false
            cur
        }
        connected?.complete(false)
        written?.complete(false)
        runCatching { g?.disconnect() }
        runCatching { g?.close() }
    }

    /**
     * Closes the link cleanly before the DFU service takes over: disconnect, wait for
     * STATE_DISCONNECTED (so the SoftDevice ends the connection and starts advertising again
     * instead of waiting for the 2 s supervision timeout), then close the GATT client.
     *
     * Note: do NOT call gatt.refresh() here before the buttonless jump! At this point the
     * companion is still running the application (whose cached services already include the
     * 0x1530 BLEDfu service). Calling gatt.refresh() on a bonded device right before g.close()
     * causes Android's subsequent direct connectGatt(autoConnect = false) in DfuBaseService
     * to fail immediately (~200 ms) with 0x85 (GATT_ERROR). The cache refresh is performed by
     * DfuService.refreshDeviceCache() after the buttonless jump into the bootloader.
     */
    suspend fun closeAndRefresh() {
        cancelMtuFallback()
        val done = CompletableDeferred<Unit>()
        val g = synchronized(gattLock) {
            val cur = gatt ?: return
            gatt = null
            rx = null
            servicesDiscovered = false
            established = false
            closing = cur to done
            cur
        }
        connected?.complete(false)
        written?.complete(false)
        try {
            runCatching { g.disconnect() }
            withTimeoutOrNull(2_500) { done.await() }
        } finally {
            synchronized(gattLock) { closing = null }
            runCatching { g.close() }
        }
    }
}

/** Clears Android's cached services of [gatt]'s device (hidden API, as the DFU library does). */
fun refreshGattCache(gatt: BluetoothGatt): Boolean =
    runCatching { gatt.javaClass.getMethod("refresh").invoke(gatt) as Boolean }.getOrDefault(false)
