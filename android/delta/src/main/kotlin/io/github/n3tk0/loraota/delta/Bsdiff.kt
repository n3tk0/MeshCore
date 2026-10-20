package io.github.n3tk0.loraota.delta

/**
 * bsdiff (Colin Percival, BSD-2-Clause) matching: a suffix array of the old image (qsufsort), then
 * runs of "new = old + small differences" (diff) and plain new bytes (extra).
 */
internal object Bsdiff {

    class Control(val diffLen: Int, val extraLen: Int, val seek: Int, val newPos: Int, val oldPos: Int)

    fun suffixArray(old: ByteArray): IntArray {
        val n = old.size
        val i = IntArray(n + 1)
        val v = IntArray(n + 1)
        val buckets = IntArray(256)
        for (b in old) buckets[b.toInt() and 0xff]++
        for (k in 1 until 256) buckets[k] += buckets[k - 1]
        for (k in 255 downTo 1) buckets[k] = buckets[k - 1]
        buckets[0] = 0
        for (k in 0 until n) i[++buckets[old[k].toInt() and 0xff]] = k
        i[0] = n
        for (k in 0 until n) v[k] = buckets[old[k].toInt() and 0xff]
        v[n] = 0
        for (k in 1 until 256) if (buckets[k] == buckets[k - 1] + 1) i[buckets[k]] = -1
        i[0] = -1

        var h = 1
        while (i[0] != -(n + 1)) {
            var len = 0
            var k = 0
            while (k < n + 1) {
                if (i[k] < 0) {
                    len -= i[k]
                    k -= i[k]
                } else {
                    if (len != 0) i[k - len] = -len
                    len = v[i[k]] + 1 - k
                    split(i, v, k, len, h)
                    k += len
                    len = 0
                }
            }
            if (len != 0) i[k - len] = -len
            h += h
        }
        for (k in 0 until n + 1) i[v[k]] = k
        return i
    }

    /**
     * The qsufsort split, with its recursion on a heap stack: firmware padding (long runs of 0xFF) made
     * it tens of thousands of calls deep. The work runs in the same order as the recursive version:
     * left part, then the equal group's numbers, then the right part.
     */
    private fun split(I: IntArray, V: IntArray, start0: Int, len0: Int, h: Int) {
        // entries of three ints: kind (0 = split [a, a+b), 1 = number the equal group [a, b)), a, b
        var stack = IntArray(3 * 64)
        var top = 0
        fun push(kind: Int, a: Int, b: Int) {
            if (top + 3 > stack.size) stack = stack.copyOf(stack.size * 2)
            stack[top] = kind; stack[top + 1] = a; stack[top + 2] = b
            top += 3
        }
        push(0, start0, len0)
        while (top > 0) {
            top -= 3
            if (stack[top] == 1) {
                val jj = stack[top + 1]
                val kk = stack[top + 2]
                for (q in 0 until kk - jj) V[I[jj + q]] = kk - 1
                if (jj == kk - 1) I[jj] = -1
                continue
            }
            val start = stack[top + 1]
            val len = stack[top + 2]
            splitStep(I, V, start, len, h)?.let { (jj, kk) ->
                // pushed in reverse: popped left first, as the recursion ran them
                if (start + len > kk) push(0, kk, start + len - kk)
                push(1, jj, kk)
                if (jj > start) push(0, start, jj - start)
            }
        }
    }

    /** One split level: sorts a small group in place (null), or partitions a large one and returns its equal range. */
    private fun splitStep(I: IntArray, V: IntArray, start: Int, len: Int, h: Int): Pair<Int, Int>? {
        if (len < 16) {
            var k = start
            while (k < start + len) {
                var j = 1
                var x = V[I[k] + h]
                var m = 1
                while (k + m < start + len) {
                    if (V[I[k + m] + h] < x) {
                        x = V[I[k + m] + h]
                        j = 0
                    }
                    if (V[I[k + m] + h] == x) {
                        val tmp = I[k + j]; I[k + j] = I[k + m]; I[k + m] = tmp
                        j++
                    }
                    m++
                }
                for (q in 0 until j) V[I[k + q]] = k + j - 1
                if (j == 1) I[k] = -1
                k += j
            }
            return null
        }

        val x = V[I[start + len / 2] + h]
        var jj = 0
        var kk = 0
        for (m in start until start + len) {
            if (V[I[m] + h] < x) jj++
            if (V[I[m] + h] == x) kk++
        }
        jj += start
        kk += jj

        var m = start
        var j = 0
        var k = 0
        while (m < jj) {
            if (V[I[m] + h] < x) {
                m++
            } else if (V[I[m] + h] == x) {
                val tmp = I[m]; I[m] = I[jj + j]; I[jj + j] = tmp
                j++
            } else {
                val tmp = I[m]; I[m] = I[kk + k]; I[kk + k] = tmp
                k++
            }
        }
        while (jj + j < kk) {
            if (V[I[jj + j] + h] == x) {
                j++
            } else {
                val tmp = I[jj + j]; I[jj + j] = I[kk + k]; I[kk + k] = tmp
                k++
            }
        }

        return Pair(jj, kk)
    }

    private fun matchLen(old: ByteArray, oldPos: Int, new: ByteArray, newPos: Int): Int {
        var l = 0
        while (oldPos + l < old.size && newPos + l < new.size && old[oldPos + l] == new[newPos + l]) l++
        return l
    }

    private fun compare(old: ByteArray, oldPos: Int, new: ByteArray, newPos: Int): Int {
        val n = minOf(old.size - oldPos, new.size - newPos)
        for (q in 0 until n) {
            val a = old[oldPos + q].toInt() and 0xff
            val b = new[newPos + q].toInt() and 0xff
            if (a != b) return a - b
        }
        return 0
    }

    // returns length, position in out[0]
    private fun search(I: IntArray, old: ByteArray, new: ByteArray, newPos: Int, st0: Int, en0: Int, pos: IntArray): Int {
        var st = st0
        var en = en0
        while (en - st >= 2) {
            val x = st + (en - st) / 2
            if (compare(old, I[x], new, newPos) < 0) st = x else en = x
        }
        val x = matchLen(old, I[st], new, newPos)
        val y = matchLen(old, I[en], new, newPos)
        return if (x > y) {
            pos[0] = I[st]; x
        } else {
            pos[0] = I[en]; y
        }
    }

    fun diff(old: ByteArray, new: ByteArray, I: IntArray = suffixArray(old)): List<Control> {
        val out = ArrayList<Control>()
        val oldSize = old.size
        val newSize = new.size
        var scan = 0
        var len = 0
        val posBox = IntArray(1)
        var pos = 0
        var lastScan = 0
        var lastPos = 0
        var lastOffset = 0

        while (scan < newSize) {
            var oldScore = 0
            scan += len
            var scsc = scan
            while (scan < newSize) {
                len = search(I, old, new, scan, 0, oldSize, posBox)
                pos = posBox[0]
                while (scsc < scan + len) {
                    if (scsc + lastOffset < oldSize && old[scsc + lastOffset] == new[scsc]) oldScore++
                    scsc++
                }
                if ((len == oldScore && len != 0) || len > oldScore + 8) break
                if (scan + lastOffset < oldSize && old[scan + lastOffset] == new[scan]) oldScore--
                scan++
            }

            if (len != oldScore || scan == newSize) {
                var s = 0
                var sf = 0
                var lenF = 0
                var q = 0
                while (lastScan + q < scan && lastPos + q < oldSize) {
                    if (old[lastPos + q] == new[lastScan + q]) s++
                    q++
                    if (s * 2 - q > sf * 2 - lenF) {
                        sf = s; lenF = q
                    }
                }

                var lenB = 0
                if (scan < newSize) {
                    s = 0
                    var sb = 0
                    q = 1
                    while (scan >= lastScan + q && pos >= q) {
                        if (old[pos - q] == new[scan - q]) s++
                        if (s * 2 - q > sb * 2 - lenB) {
                            sb = s; lenB = q
                        }
                        q++
                    }
                }

                if (lastScan + lenF > scan - lenB) {
                    val overlap = (lastScan + lenF) - (scan - lenB)
                    s = 0
                    var ss = 0
                    var lens = 0
                    for (r in 0 until overlap) {
                        if (new[lastScan + lenF - overlap + r] == old[lastPos + lenF - overlap + r]) s++
                        if (new[scan - lenB + r] == old[pos - lenB + r]) s--
                        if (s > ss) {
                            ss = s; lens = r + 1
                        }
                    }
                    lenF += lens - overlap
                    lenB -= lens
                }

                val extraLen = (scan - lenB) - (lastScan + lenF)
                val seek = (pos - lenB) - (lastPos + lenF)
                out.add(Control(lenF, extraLen, seek, lastScan, lastPos))
                lastScan = scan - lenB
                lastPos = pos - lenB
                lastOffset = pos - scan
            }
        }
        return out
    }
}
