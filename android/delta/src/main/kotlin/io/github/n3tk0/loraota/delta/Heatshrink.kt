package io.github.n3tk0.loraota.delta

import java.io.ByteArrayOutputStream

/**
 * heatshrink (LZSS) with the parameters the repeater decoder is built with: window 2^8, lookahead 2^7.
 * Bit stream, most significant bit first: literal = 1 + 8 bits; back reference = 0 + (offset - 1) in
 * 8 bits + (length - 1) in 7 bits. The last byte is padded with zero bits.
 */
object Heatshrink {
    const val WINDOW_BITS = 8
    const val LOOKAHEAD_BITS = 7
    private const val WINDOW = 1 shl WINDOW_BITS
    private const val MAX_LEN = 1 shl LOOKAHEAD_BITS
    private const val MIN_LEN = 2   // 16 bits for a reference, 18 for two literals

    private class BitWriter {
        val out = ByteArrayOutputStream()
        private var cur = 0
        private var n = 0

        fun bits(value: Int, count: Int) {
            for (b in count - 1 downTo 0) {
                cur = (cur shl 1) or ((value ushr b) and 1)
                if (++n == 8) {
                    out.write(cur)
                    cur = 0
                    n = 0
                }
            }
        }

        fun finish(): ByteArray {
            if (n > 0) out.write(cur shl (8 - n))
            return out.toByteArray()
        }
    }

    fun compress(input: ByteArray): ByteArray {
        val w = BitWriter()
        // newest position for each 2-byte prefix, and the previous one with the same prefix
        val head = IntArray(1 shl 16) { -1 }
        val prev = IntArray(input.size)
        fun key(p: Int) = ((input[p].toInt() and 0xff) shl 8) or (input[p + 1].toInt() and 0xff)
        fun insert(p: Int) {
            if (p + 1 >= input.size) return
            val k = key(p)
            prev[p] = head[k]
            head[k] = p
        }

        var i = 0
        while (i < input.size) {
            var bestLen = 0
            var bestOff = 0
            val maxLen = minOf(MAX_LEN, input.size - i)
            if (maxLen >= MIN_LEN) {
                var j = head[key(i)]
                while (j >= 0 && i - j <= WINDOW) {
                    var l = 0
                    while (l < maxLen && input[j + l] == input[i + l]) l++
                    if (l > bestLen) {
                        bestLen = l
                        bestOff = i - j
                        if (l == maxLen) break
                    }
                    j = prev[j]
                }
            }
            if (bestLen >= MIN_LEN) {
                w.bits(0, 1)
                w.bits(bestOff - 1, WINDOW_BITS)
                w.bits(bestLen - 1, LOOKAHEAD_BITS)
                for (p in i until i + bestLen) insert(p)
                i += bestLen
            } else {
                w.bits(1, 1)
                w.bits(input[i].toInt() and 0xff, 8)
                insert(i)
                i++
            }
        }
        return w.finish()
    }

    fun decompress(input: ByteArray): ByteArray {
        val out = ByteArrayOutputStream()
        val window = ByteArray(WINDOW)
        var head = 0
        var bitPos = 0
        val totalBits = input.size * 8
        fun bits(count: Int): Int {
            if (bitPos + count > totalBits) return -1
            var v = 0
            repeat(count) {
                val b = (input[bitPos ushr 3].toInt() ushr (7 - (bitPos and 7))) and 1
                v = (v shl 1) or b
                bitPos++
            }
            return v
        }
        fun push(b: Int) {
            out.write(b)
            window[head and (WINDOW - 1)] = b.toByte()
            head++
        }
        while (true) {
            val tag = bits(1)
            if (tag < 0) break
            if (tag == 1) {
                val b = bits(8)
                if (b < 0) break
                push(b)
            } else {
                val off = bits(WINDOW_BITS)
                if (off < 0) break
                val len = bits(LOOKAHEAD_BITS)
                if (len < 0) break
                repeat(len + 1) { push(window[(head - (off + 1)) and (WINDOW - 1)].toInt() and 0xff) }
            }
        }
        return out.toByteArray()
    }
}
