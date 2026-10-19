package io.github.n3tk0.loraota.delta

import java.io.File
import kotlin.random.Random
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertTrue

class DeltaTest {

    private fun firmwareLike(rnd: Random, size: Int): ByteArray {
        val b = ByteArray(size)
        rnd.nextBytes(b)
        // padding runs and repeated tables, like in real images
        for (start in 0 until size step 9000) {
            for (k in start until minOf(size, start + 700)) b[k] = 0xff.toByte()
        }
        for (k in size / 2 until minOf(size, size / 2 + 3000)) b[k] = b[k - 1500]
        return b
    }

    private fun changed(rnd: Random, old: ByteArray): ByteArray {
        val out = java.io.ByteArrayOutputStream()
        var p = 0
        while (p < old.size) {
            val n = minOf(old.size - p, 2000 + rnd.nextInt(4000))
            out.write(old, p, n)
            p += n
            when (rnd.nextInt(4)) {
                0 -> out.write(rnd.nextBytes(1 + rnd.nextInt(40)))   // inserted code
                1 -> p = minOf(old.size, p + rnd.nextInt(30))         // removed code
                else -> { }
            }
        }
        val b = out.toByteArray()
        // addresses that moved: small changes all over
        for (k in 0 until b.size / 200) b[rnd.nextInt(b.size)] = rnd.nextInt(256).toByte()
        return b
    }

    @Test
    fun heatshrinkRoundTrip() {
        val rnd = Random(1)
        for (size in listOf(0, 1, 2, 3, 255, 256, 257, 5000)) {
            val data = ByteArray(size) { if (rnd.nextInt(3) == 0) rnd.nextInt(256).toByte() else 0 }
            assertContentEquals(data, Heatshrink.decompress(Heatshrink.compress(data)).copyOf(size))
        }
    }

    @Test
    fun patchRoundTrip() {
        val rnd = Random(2)
        val old = firmwareLike(rnd, 120_000)
        val new = changed(rnd, old)
        val patch = DeltaGenerator.create(old, new)
        assertContentEquals(new, DetoolsPatch.apply(old, patch))
        assertTrue(patch.size < new.size / 3, "delta ${patch.size} for ${new.size}")
    }

    @Test
    fun edgeCases() {
        val rnd = Random(3)
        val a = rnd.nextBytes(3000)
        for ((old, new) in listOf(
            ByteArray(0) to a, a to ByteArray(0), a to a, a to a.copyOf(1500),
            ByteArray(4000) to ByteArray(4000) { 1 }, a to a.reversedArray()
        )) {
            assertContentEquals(new, DetoolsPatch.apply(old, DeltaGenerator.create(old, new)))
        }
    }

    /** Writes a small old/new/patch set for the firmware's unit test (test/test_lora_ota). */
    @Test
    fun writeFirmwareFixture() {
        val dir = System.getProperty("fixtureDir") ?: return
        val rnd = Random(4)
        val old = firmwareLike(rnd, 6000)
        val new = changed(rnd, old)
        val patch = DeltaGenerator.create(old, new)
        File(dir).mkdirs()
        File(dir, "old.bin").writeBytes(old)
        File(dir, "new.bin").writeBytes(new)
        File(dir, "patch.bin").writeBytes(patch)
    }
}
