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
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withTimeout
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
    }

    val frames = Channel<ByteArray>(Channel.UNLIMITED)
    @Volatile var onDisconnect: (() -> Unit)? = null

    @Volatile private var gatt: BluetoothGatt? = null
    @Volatile private var rx: BluetoothGattCharacteristic? = null
    @Volatile private var connected: CompletableDeferred<Boolean>? = null
    @Volatile private var written: CompletableDeferred<Boolean>? = null
    @Volatile private var servicesDiscovered = false
    private val writeLock = Mutex()
    /** The link being closed by [closeAndRefresh]: its disconnect is awaited there, not closed here. */
    @Volatile private var closing: Pair<BluetoothGatt, CompletableDeferred<Unit>>? = null

    private val callback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            if (g != gatt && closing?.first != g) {
                if (newState == BluetoothProfile.STATE_DISCONNECTED) runCatching { g.close() }
                return
            }
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                if (status != BluetoothGatt.GATT_SUCCESS) {
                    connected?.complete(false)
                    return
                }
                if (!g.requestMtu(247)) {
                    if (!g.discoverServices()) connected?.complete(false)
                }
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                connected?.complete(false)
                written?.complete(false)
                val c = closing
                if (c != null && c.first == g) {
                    c.second.complete(Unit)
                } else {
                    runCatching { g.close() }
                }
                if (gatt == g) {
                    gatt = null
                    rx = null
                    servicesDiscovered = false
                    onDisconnect?.invoke()
                }
            }
        }

        override fun onMtuChanged(g: BluetoothGatt, mtu: Int, status: Int) {
            if (g != gatt) return
            if (!servicesDiscovered) {
                servicesDiscovered = true
                if (!g.discoverServices()) connected?.complete(false)
            }
        }

        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            if (g != gatt) return
            if (status != BluetoothGatt.GATT_SUCCESS) {
                connected?.complete(false)
                return
            }
            val service = g.getService(SERVICE)
            val tx = service?.getCharacteristic(TX)
            val rxChar = service?.getCharacteristic(RX)
            rx = rxChar
            if (tx == null || rxChar == null) {
                connected?.complete(false)
                return
            }
            g.setCharacteristicNotification(tx, true)
            val d = tx.getDescriptor(CCCD)
            if (d == null) {
                connected?.complete(false)
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
            if (!ok) connected?.complete(false)
        }

        override fun onDescriptorWrite(g: BluetoothGatt, d: BluetoothGattDescriptor, status: Int) {
            if (g != gatt) return
            connected?.complete(status == BluetoothGatt.GATT_SUCCESS)
        }

        override fun onCharacteristicWrite(g: BluetoothGatt, c: BluetoothGattCharacteristic, status: Int) {
            if (g != gatt) return
            written?.complete(status == BluetoothGatt.GATT_SUCCESS)
        }

        @Deprecated("Deprecated in Java")
        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic) {
            if (g != gatt) return
            @Suppress("DEPRECATION")
            if (c.uuid == TX) {
                val v = c.value ?: return
                frames.trySend(v.copyOf())
            }
        }

        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic, value: ByteArray) {
            if (g != gatt) return
            if (c.uuid == TX) frames.trySend(value.copyOf())
        }
    }

    suspend fun connect(device: BluetoothDevice): Boolean {
        close()
        servicesDiscovered = false
        val done = CompletableDeferred<Boolean>()
        connected = done
        val g = device.connectGatt(context, false, callback, BluetoothDevice.TRANSPORT_LE)
            ?: run {
                connected = null
                return false
            }
        gatt = g
        // pairing with the companion's PIN may take a while
        return try {
            withTimeout(60_000) { done.await() }
        } catch (e: Exception) {
            if (e is CancellationException) {
                close()
                throw e
            }
            close()
            false
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
        val g = gatt
        gatt = null
        rx = null
        servicesDiscovered = false
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
        val g = gatt ?: return
        gatt = null
        rx = null
        servicesDiscovered = false
        connected?.complete(false)
        written?.complete(false)
        val done = CompletableDeferred<Unit>()
        closing = g to done
        try {
            runCatching { g.disconnect() }
            withTimeoutOrNull(2_500) { done.await() }
        } finally {
            closing = null
            runCatching { g.close() }
        }
    }
}

/** Clears Android's cached services of [gatt]'s device (hidden API, as the DFU library does). */
fun refreshGattCache(gatt: BluetoothGatt): Boolean =
    runCatching { gatt.javaClass.getMethod("refresh").invoke(gatt) as Boolean }.getOrDefault(false)
