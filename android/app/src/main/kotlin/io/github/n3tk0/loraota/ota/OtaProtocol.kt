package io.github.n3tk0.loraota.ota

import io.github.n3tk0.loraota.R
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.ceil
import kotlin.math.pow

/** The repeater's side, see src/helpers/LoraOta.h in the firmware. */
object OtaProtocol {
    const val REQ_TYPE = 0x40
    const val OP_INFO = 1
    const val OP_BEGIN = 2
    const val OP_DATA = 3
    const val OP_STATUS = 4
    const val OP_END = 5
    const val OP_ABORT = 6
    const val OP_PAUSE = 7

    const val STATE_IDLE = 0
    const val STATE_RECEIVING = 1
    const val STATE_READY = 2
    const val STATE_FAILED = 3
    const val STATE_PAUSED = 4

    const val FLAG_ENABLED = 1
    const val FLAG_UNCONFIRMED = 2

    const val BEGIN_IN_PLACE = 1   // the admin agrees to an update "in place" (see Info.inPlace)

    const val PLATFORM_NRF52 = 1
    const val PLATFORM_ESP32 = 2

    /** The repeater's status code as a message for the user. */
    fun statusRes(code: Int): Int = when (code) {
        1 -> R.string.st_state
        2 -> R.string.st_too_big
        3 -> R.string.st_patch
        4 -> R.string.st_hash
        5 -> R.string.st_unsupported
        6 -> R.string.st_flash
        7 -> R.string.st_busy
        8 -> R.string.st_not_direct
        9 -> R.string.st_bad_arg
        10 -> R.string.st_old_image
        11 -> R.string.st_bad_image
        12 -> R.string.st_disabled
        13 -> R.string.st_no_ram
        else -> R.string.st_unknown
    }

    class Info(val platform: Int, val state: Int, val flags: Int, val imageSize: Int, val imageSha: ByteArray,
               val maxNew: Int, val maxInPlace: Int, val ident: String) {
        val enabled get() = flags and FLAG_ENABLED != 0
        /**
         * An image this big only goes "in place": the repeater has no room for the old and the new one
         * together, so a power cut while it copies leaves it with no firmware.
         */
        fun inPlace(newSize: Int) = newSize > maxNew && newSize <= maxInPlace
        fun fits(newSize: Int) = newSize <= maxNew || newSize <= maxInPlace
        val unconfirmed get() = flags and FLAG_UNCONFIRMED != 0
    }
    class Status(val state: Int, val nextSeq: Int, val received: Int, val written: Int)
    class Reply(val op: Int, val status: Int, val data: ByteBuffer)

    private fun le(size: Int) = ByteBuffer.allocate(size).order(ByteOrder.LITTLE_ENDIAN)

    fun info() = byteArrayOf(REQ_TYPE.toByte(), OP_INFO.toByte())
    fun status() = byteArrayOf(REQ_TYPE.toByte(), OP_STATUS.toByte())
    fun end() = byteArrayOf(REQ_TYPE.toByte(), OP_END.toByte())
    fun abort() = byteArrayOf(REQ_TYPE.toByte(), OP_ABORT.toByte())
    fun pause() = byteArrayOf(REQ_TYPE.toByte(), OP_PAUSE.toByte())

    fun begin(patchSize: Int, newSize: Int, newSha: ByteArray, oldSha: ByteArray, chunk: Int,
              radio: SideRadio?, timeoutSecs: Int, inPlace: Boolean = false): ByteArray {
        val b = le(2 + 4 + 4 + 32 + 32 + 1 + 4 + 4 + 1 + 1 + 2 + 1)
        b.put(REQ_TYPE.toByte()).put(OP_BEGIN.toByte())
        b.putInt(patchSize).putInt(newSize).put(newSha).put(oldSha).put(chunk.toByte())
        b.putInt(radio?.freqKhz ?: 0).putInt(radio?.bwHz ?: 0)
        b.put((radio?.sf ?: 0).toByte()).put((radio?.cr ?: 0).toByte())
        b.putShort(timeoutSecs.toShort())
        b.put((if (inPlace) BEGIN_IN_PLACE else 0).toByte())
        return b.array()
    }

    fun data(seq: Int, bytes: ByteArray): ByteArray {
        val b = le(4 + bytes.size)
        b.put(REQ_TYPE.toByte()).put(OP_DATA.toByte()).putShort(seq.toShort()).put(bytes)
        return b.array()
    }

    fun parse(resp: ByteArray): Reply? {
        if (resp.size < 2) return null
        val b = ByteBuffer.wrap(resp).order(ByteOrder.LITTLE_ENDIAN)
        return Reply(b.get().toInt() and 0xff, b.get().toInt() and 0xff, b)
    }

    fun parseInfo(r: Reply): Info {
        val b = r.data
        val platform = b.get().toInt() and 0xff
        val state = b.get().toInt() and 0xff
        val flags = b.get().toInt() and 0xff
        val size = b.int
        val sha = ByteArray(32).also { b.get(it) }
        val maxNew = b.int
        val maxInPlace = b.int
        val rest = ByteArray(b.remaining()).also { b.get(it) }
        val end = rest.indexOf(0).let { if (it < 0) rest.size else it }
        return Info(platform, state, flags, size, sha, maxNew, maxInPlace, String(rest, 0, end, Charsets.UTF_8))
    }

    fun parseStatus(r: Reply): Status {
        val b = r.data
        // BUSY, NOT_DIRECT and the like come with no state
        if (b.remaining() < 11) throw OtaFailed(statusRes(r.status))
        return Status(b.get().toInt() and 0xff, b.short.toInt() and 0xffff, b.int, b.int)
    }
}

/** Radio settings of the side channel the update runs on. */
data class SideRadio(val freqKhz: Int, val bwHz: Int, val sf: Int, val cr: Int) {
    val freqMhz get() = freqKhz / 1000.0
    val bwKhz get() = bwHz / 1000.0

    fun cliArgs(): String = "%.3f %s %d %d".format(java.util.Locale.ROOT, freqMhz,
        if (bwHz % 1000 == 0) (bwHz / 1000).toString() else "%.1f".format(java.util.Locale.ROOT, bwKhz), sf, cr)

    /** Seconds on air for a LoRa packet of [bytes] (explicit header, CRC, MeshCore's preamble). */
    fun airtime(bytes: Int): Double {
        val tsym = 2.0.pow(sf) / bwHz
        val de = if (tsym > 0.016) 1 else 0
        val preamble = if (sf <= 8) 32 else 16   // RadioLibWrapper::preambleLengthForSF
        val payload = 8 + maxOf(ceil((8.0 * bytes - 4 * sf + 28 + 16) / (4.0 * (sf - 2 * de))).toInt() * cr, 0)
        return (preamble + 4.25) * tsym + payload * tsym
    }

    companion object {
        // EU 869.40-869.65 MHz (10 % duty cycle), below the 869.618 MHz / 62.5 kHz main channel
        val DEFAULT = SideRadio(869470, 125000, 7, 5)
    }
}

/** Bytes a request of [dataLen] bytes takes on air: header, path length, hashes, MAC, timestamp, AES blocks. */
fun requestPacketSize(dataLen: Int): Int = 2 + 4 + ((4 + dataLen + 15) / 16) * 16
