package io.github.n3tk0.loraota.ota

import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * The link to a direct repeater, measured with a TRACE (CMD_SEND_TRACE_PATH in examples/companion_radio/MyMesh.cpp).
 *
 * The companion sends a direct trace packet with the repeater's hash as its path. The repeater matches its hash,
 * appends the SNR it heard the packet with and sends it on (src/Mesh.cpp, onRecvPacket). The companion hears that
 * retransmission, finds the path used up and reports it (onTraceRecv, PUSH_CODE_TRACE_DATA): the path SNR byte is what the
 * repeater heard us with (outgoing), the extra last byte is what we heard the repeater with (incoming). All are int8, 4 * dB.
 */
object LinkTrace {
    const val CMD_SEND_TRACE_PATH = 36
    const val RESP_SENT = 6
    const val PUSH_TRACE_DATA = 0x89

    /** One request on the air: header, path length, tag + auth + flags, and the path (one hash). */
    fun packetBytes(hashBytes: Int) = 2 + 9 + hashBytes

    fun le(v: Int): ByteArray = ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN).putInt(v).array()

    /** [cmd][tag 4][auth 4][flags][path]; flags' low 2 bits are log2 of the hash size. */
    fun command(tag: Int, auth: Int, flags: Int, path: ByteArray): ByteArray =
        byteArrayOf(CMD_SEND_TRACE_PATH.toByte()) + le(tag) + le(auth) + byteArrayOf(flags.toByte()) + path

    class Sent(val tag: Int, val estTimeoutMs: Long)

    /** RESP_CODE_SENT: [6][is flood][tag 4][est timeout ms 4]; null when it is another frame. */
    fun parseSent(f: ByteArray): Sent? {
        if (f.size < 10 || (f[0].toInt() and 0xff) != RESP_SENT) return null
        val b = ByteBuffer.wrap(f, 2, 8).order(ByteOrder.LITTLE_ENDIAN)
        return Sent(b.int, b.int.toLong() and 0xffffffffL)
    }

    /** What the companion pushes: path hashes, the SNR each hop heard (dB), and the SNR it heard the last hop with. */
    class Data(val tag: Int, val flags: Int, val hashes: ByteArray, val snrs: List<Double>, val finalSnr: Double)

    /** PUSH_CODE_TRACE_DATA: [0x89][0][path len][flags][tag 4][auth 4][hashes][snrs][final snr]; null when malformed. */
    fun parseData(f: ByteArray): Data? {
        if (f.size < 13 || (f[0].toInt() and 0xff) != PUSH_TRACE_DATA) return null
        val pathLen = f[2].toInt() and 0xff
        val flags = f[3].toInt() and 0xff
        val snrCount = pathLen shr (flags and 3)
        if (f.size < 12 + pathLen + snrCount + 1) return null
        val tag = ByteBuffer.wrap(f, 4, 4).order(ByteOrder.LITTLE_ENDIAN).int
        val hashes = f.copyOfRange(12, 12 + pathLen)
        val snrs = List(snrCount) { f[12 + pathLen + it].toInt() / 4.0 }
        return Data(tag, flags, hashes, snrs, f[12 + pathLen + snrCount].toInt() / 4.0)
    }

    /** The push's tag without parsing the rest (to match it while waiting). */
    fun tagOf(f: ByteArray): Int? =
        if (f.size >= 8 && (f[0].toInt() and 0xff) == PUSH_TRACE_DATA) ByteBuffer.wrap(f, 4, 4).order(ByteOrder.LITTLE_ENDIAN).int else null

    /** How long to wait for the push: the companion's estimate and a margin, not under 3 s and not over 8 s. */
    fun waitMs(estTimeoutMs: Long): Long = (estTimeoutMs + 1500).coerceIn(3000, 8000)

    /** One reading of a direct link, in dB; a side the trace did not bring is null. */
    class Reading(val outSnr: Double?, val inSnr: Double?)

    /** A direct repeater's trace (one hop): the repeater's hop SNR is outgoing, the last byte is incoming. */
    fun reading(d: Data, expectedHashes: ByteArray): Reading? =
        if (d.snrs.size == 1 && d.hashes.contentEquals(expectedHashes)) Reading(d.snrs[0], d.finalSnr) else null

    /** The trace hash size for the companion's path hash mode (0, 1, 2): the firmware's entry is 1 shl flags bytes, so 1, 2 or 4. */
    fun hashSizeForMode(mode: Int): Int = when (mode) {
        0 -> 1
        1 -> 2
        else -> 4
    }

    /** The flags' low 2 bits for a hash size of 1, 2 or 4 bytes: log2 of it (the firmware reads them as a shift). */
    fun flagsForHashSize(hashSize: Int): Int = Integer.numberOfTrailingZeros(hashSize).coerceIn(0, 2)

    /** True when [f] is the push for [tag] that went through [expectedHashes] (another node with the same short hash is not ours). */
    fun isAnswer(f: ByteArray, tag: Int, expectedHashes: ByteArray): Boolean {
        if (tagOf(f) != tag) return false
        val d = parseData(f) ?: return false
        return d.hashes.contentEquals(expectedHashes)
    }

    /** A trace only for a repeater with no hop: with hops (or no known route) it would be the first hop's, not the repeater's. */
    fun canTrace(routeHops: Int?): Boolean = routeHops == 0

    /** A trace goes only when the hour's time on air has room for it (the same budget as the update's). */
    fun fitsBudget(budget: AirtimeBudget, secs: Double): Boolean = budget.waitMs(secs) == 0L
}

/** How a bar count is coloured: red, orange, or the side's own colour; grey when nothing is known. */
enum class BarTier { NONE, RED, ORANGE, OWN }

/** What a reading looks like to people: 5 bars from the SNR, and a colour tier from the bar count. */
object LinkQuality {
    const val MAX_BARS = 5

    /** 5 bars from 10 dB, 4 from 5, 3 from 0, 2 from -7.5, 1 below; 0 when it is not known. */
    fun bars(snr: Double?): Int = when {
        snr == null -> 0
        snr >= 10.0 -> 5
        snr >= 5.0 -> 4
        snr >= 0.0 -> 3
        snr >= -7.5 -> 2
        else -> 1
    }

    /** 1 bar is red, 2 and 3 are orange, 4 and 5 keep the side's own colour; 0 is none (grey). */
    fun tier(bars: Int): BarTier = when {
        bars <= 0 -> BarTier.NONE
        bars == 1 -> BarTier.RED
        bars <= 3 -> BarTier.ORANGE
        else -> BarTier.OWN
    }
}

/** One trace in the follow mode (a side that did not come is null). */
class LinkSample(val timeMs: Long, val out: Double?, val inn: Double?)

class LinkStat(val min: Double, val avg: Double, val max: Double, val count: Int)

/** The last readings of a followed link, with min / avg / max of each direction. */
class LinkFollow(private val limit: Int = 40) {
    private val list = ArrayList<LinkSample>()
    val samples: List<LinkSample> get() = list

    companion object {
        const val INTERVAL_MS = 10_000L
        const val DURATION_MS = 120_000L
        val TICKS = (DURATION_MS / INTERVAL_MS).toInt()

        fun stat(values: List<Double>): LinkStat? =
            if (values.isEmpty()) null else LinkStat(values.min(), values.average(), values.max(), values.size)
    }

    fun add(s: LinkSample) {
        list.add(s)
        while (list.size > limit) list.removeAt(0)
    }

    fun clear() = list.clear()

    fun outStat(): LinkStat? = stat(list.mapNotNull { it.out })
    fun inStat(): LinkStat? = stat(list.mapNotNull { it.inn })
}
