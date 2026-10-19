package io.github.n3tk0.loraota.ble

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothProfile
import android.content.Context
import android.os.Build
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
    var onDisconnect: (() -> Unit)? = null

    private var gatt: BluetoothGatt? = null
    private var rx: BluetoothGattCharacteristic? = null
    private var connected: CompletableDeferred<Boolean>? = null
    private var written: CompletableDeferred<Boolean>? = null
    private val writeLock = Mutex()

    private val callback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                g.requestMtu(247)
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                connected?.complete(false)
                written?.complete(false)
                g.close()
                if (gatt == g) {
                    gatt = null
                    onDisconnect?.invoke()
                }
            }
        }

        override fun onMtuChanged(g: BluetoothGatt, mtu: Int, status: Int) {
            g.discoverServices()
        }

        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            val service = g.getService(SERVICE)
            val tx = service?.getCharacteristic(TX)
            rx = service?.getCharacteristic(RX)
            if (tx == null || rx == null) {
                connected?.complete(false)
                return
            }
            g.setCharacteristicNotification(tx, true)
            val d = tx.getDescriptor(CCCD)
            if (Build.VERSION.SDK_INT >= 33) {
                g.writeDescriptor(d, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
            } else {
                @Suppress("DEPRECATION")
                d.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                @Suppress("DEPRECATION")
                g.writeDescriptor(d)
            }
        }

        override fun onDescriptorWrite(g: BluetoothGatt, d: BluetoothGattDescriptor, status: Int) {
            connected?.complete(status == BluetoothGatt.GATT_SUCCESS)
        }

        override fun onCharacteristicWrite(g: BluetoothGatt, c: BluetoothGattCharacteristic, status: Int) {
            written?.complete(status == BluetoothGatt.GATT_SUCCESS)
        }

        @Deprecated("Deprecated in Java")
        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic) {
            @Suppress("DEPRECATION")
            if (c.uuid == TX) frames.trySend(c.value.copyOf())
        }

        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic, value: ByteArray) {
            if (c.uuid == TX) frames.trySend(value.copyOf())
        }
    }

    suspend fun connect(device: BluetoothDevice): Boolean {
        close()
        val done = CompletableDeferred<Boolean>()
        connected = done
        gatt = device.connectGatt(context, false, callback, BluetoothDevice.TRANSPORT_LE)
        // pairing with the companion's PIN may take a while
        return try {
            withTimeout(60_000) { done.await() }
        } catch (e: Exception) {
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
            g.writeCharacteristic(c, frame, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT) == android.bluetooth.BluetoothStatusCodes.SUCCESS
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
        g?.disconnect()
        g?.close()
    }
}
