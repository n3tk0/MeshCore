package io.github.n3tk0.loraota.ota

import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * A repeater's status (REQ_TYPE_GET_STATUS): every MeshCore repeater answers it, also one without LoRa OTA.
 * The layout is RepeaterStats in examples/simple_repeater/MyMesh.h.
 */
class RepeaterStatus(
    val battMv: Int,
    val txQueue: Int,
    val noiseFloor: Int,
    val lastRssi: Int,
    val packetsRecv: Long,
    val packetsSent: Long,
    val airTimeSecs: Long,
    val upTimeSecs: Long,
    val lastSnr: Double?,   // null: an older repeater that does not send it
) {
    companion object {
        const val REQ_TYPE_GET_STATUS = 0x01

        fun request() = byteArrayOf(REQ_TYPE_GET_STATUS.toByte(), 0, 0, 0, 0)

        /** The response after its tag; null when it is too short to be a status. */
        fun parse(data: ByteArray): RepeaterStatus? {
            if (data.size < 24) return null
            val b = ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN)
            fun u32(at: Int) = b.getInt(at).toLong() and 0xffffffffL
            return RepeaterStatus(
                battMv = b.getShort(0).toInt() and 0xffff,
                txQueue = b.getShort(2).toInt() and 0xffff,
                noiseFloor = b.getShort(4).toInt(),
                lastRssi = b.getShort(6).toInt(),
                packetsRecv = u32(8),
                packetsSent = u32(12),
                airTimeSecs = u32(16),
                upTimeSecs = u32(20),
                lastSnr = if (data.size >= 44) b.getShort(42) / 4.0 else null,
            )
        }
    }
}
