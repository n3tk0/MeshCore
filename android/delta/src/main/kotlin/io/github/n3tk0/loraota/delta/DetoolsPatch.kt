package io.github.n3tk0.loraota.delta

import java.io.ByteArrayOutputStream
import java.security.MessageDigest

/**
 * detools "sequential" patch with heatshrink compression, the format the repeater applies
 * (lib/detools in the firmware):
 *
 *   header byte (type 0 << 4 | compression 4), to_size (size)
 *   heatshrink( header byte, dfpatch_size 0, then for each run:
 *               diff_size, diff bytes (new - old), extra_size, extra bytes, adjustment (signed) )
 *
 * Sizes: first byte = continue bit 0x80, sign bit 0x40, 6 low bits; next bytes = continue bit + 7 bits.
 */
object DetoolsPatch {
    private const val PATCH_TYPE_SEQUENTIAL = 0
    private const val COMPRESSION_HEATSHRINK = 4

    class CorruptPatch(msg: String) : Exception(msg)

    private fun packSize(out: ByteArrayOutputStream, value: Int) {
        var v = if (value < 0) -value.toLong() else value.toLong()
        var b = (v and 0x3f).toInt()
        if (value < 0) b = b or 0x40
        v = v ushr 6
        if (v != 0L) b = b or 0x80
        out.write(b)
        while (v != 0L) {
            b = (v and 0x7f).toInt()
            v = v ushr 7
            if (v != 0L) b = b or 0x80
            out.write(b)
        }
    }

    fun create(old: ByteArray, new: ByteArray): ByteArray {
        val body = ByteArrayOutputStream()
        packSize(body, 0)   // dfpatch_size: no data format patch
        if (new.isNotEmpty()) {
            for (c in Bsdiff.diff(old, new)) {
                packSize(body, c.diffLen)
                for (k in 0 until c.diffLen) body.write((new[c.newPos + k] - old[c.oldPos + k]) and 0xff)
                packSize(body, c.extraLen)
                body.write(new, c.newPos + c.diffLen, c.extraLen)
                packSize(body, c.seek)
            }
        }

        val out = ByteArrayOutputStream()
        out.write((PATCH_TYPE_SEQUENTIAL shl 4) or COMPRESSION_HEATSHRINK)
        packSize(out, new.size)
        if (new.isNotEmpty()) {
            out.write(((Heatshrink.WINDOW_BITS - 4) shl 4) or (Heatshrink.LOOKAHEAD_BITS - 3))
            out.write(Heatshrink.compress(body.toByteArray()))
        }
        return out.toByteArray()
    }

    /** The same work the repeater does; used to check a patch before it goes on air. */
    fun apply(old: ByteArray, patch: ByteArray): ByteArray {
        if (patch.isEmpty()) throw CorruptPatch("empty")
        val type = (patch[0].toInt() ushr 4) and 7
        val compression = patch[0].toInt() and 0xf
        if (type != PATCH_TYPE_SEQUENTIAL || compression != COMPRESSION_HEATSHRINK) throw CorruptPatch("type")
        var p = 1
        var toSize = 0
        run {
            var b = patch[p++].toInt() and 0xff
            toSize = b and 0x3f
            var shift = 6
            while (b and 0x80 != 0) {
                b = patch[p++].toInt() and 0xff
                toSize = toSize or ((b and 0x7f) shl shift)
                shift += 7
            }
        }
        if (toSize == 0) return ByteArray(0)
        val hs = patch[p++].toInt() and 0xff
        if ((hs ushr 4) + 4 != Heatshrink.WINDOW_BITS || (hs and 0xf) + 3 != Heatshrink.LOOKAHEAD_BITS) {
            throw CorruptPatch("heatshrink parameters")
        }
        val body = Heatshrink.decompress(patch.copyOfRange(p, patch.size))
        var q = 0
        fun size(): Int {
            if (q >= body.size) throw CorruptPatch("short")
            var b = body[q++].toInt() and 0xff
            val neg = b and 0x40 != 0
            var v = (b and 0x3f).toLong()
            var shift = 6
            while (b and 0x80 != 0) {
                if (q >= body.size) throw CorruptPatch("short")
                b = body[q++].toInt() and 0xff
                v = v or ((b and 0x7f).toLong() shl shift)
                shift += 7
            }
            return (if (neg) -v else v).toInt()
        }
        if (size() != 0) throw CorruptPatch("data format patch")
        val to = ByteArray(toSize)
        var toPos = 0
        var fromPos = 0
        while (toPos < toSize) {
            val diff = size()
            if (diff < 0 || toPos + diff > toSize || fromPos + diff > old.size || q + diff > body.size) throw CorruptPatch("diff")
            for (k in 0 until diff) to[toPos + k] = (body[q + k] + old[fromPos + k]).toByte()
            q += diff; toPos += diff; fromPos += diff
            val extra = size()
            if (extra < 0 || toPos + extra > toSize || q + extra > body.size) throw CorruptPatch("extra")
            System.arraycopy(body, q, to, toPos, extra)
            q += extra; toPos += extra
            fromPos += size()
            if (fromPos < 0 || fromPos > old.size) throw CorruptPatch("adjustment")
        }
        return to
    }

    fun sha256(data: ByteArray): ByteArray = MessageDigest.getInstance("SHA-256").digest(data)
}

/**
 * Makes the delta on a thread with a large stack: the suffix sort recurses deeply on repetitive data
 * (padding in firmware images).
 */
object DeltaGenerator {
    fun create(old: ByteArray, new: ByteArray): ByteArray {
        var result: ByteArray? = null
        var error: Throwable? = null
        val t = Thread(null, {
            try {
                val patch = DetoolsPatch.create(old, new)
                if (!DetoolsPatch.apply(old, patch).contentEquals(new)) throw IllegalStateException("delta check failed")
                result = patch
            } catch (e: Throwable) {
                error = e
            }
        }, "delta", 256L shl 20)
        t.start()
        t.join()
        error?.let { throw it }
        return result!!
    }
}
