package io.github.n3tk0.loraota.flash

import io.github.n3tk0.loraota.delta.DetoolsPatch
import io.github.n3tk0.loraota.ota.hex
import java.io.ByteArrayOutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertNull

class PickedImageTest {
    private fun uf2(start: Int, data: ByteArray, block: Int = 256): ByteArray {
        val n = (data.size + block - 1) / block
        val out = ByteBuffer.allocate(n * 512).order(ByteOrder.LITTLE_ENDIAN)
        for (i in 0 until n) {
            val part = data.copyOfRange(i * block, minOf(data.size, (i + 1) * block))
            out.position(i * 512)
            out.putInt(0x0A324655).putInt(0x9E5D5157.toInt()).putInt(0x2000).putInt(start + i * block)
                .putInt(part.size).putInt(i).putInt(n).putInt(0xADA52840.toInt()).put(part)
            out.position(i * 512 + 508)
            out.putInt(0x0AB16F30)
        }
        return out.array()
    }

    private fun zip(vararg e: Pair<String, ByteArray>): ByteArray {
        val b = ByteArrayOutputStream()
        ZipOutputStream(b).use { z -> for ((n, d) in e) { z.putNextEntry(ZipEntry(n)); z.write(d); z.closeEntry() } }
        return b.toByteArray()
    }

    // 300 bytes: the uf2 pads the second block with zeros... the image is 512 bytes
    private val bin = ByteArray(300) { (it * 7 + 1).toByte() }
    private val sha = hex(DetoolsPatch.sha256(bin))

    @Test
    fun uf2IsTrimmedToTheRunningSize() {
        val u = uf2(0x26000, bin.copyOf(512))   // padded to 256
        assertContentEquals(bin, PickedImage.running(u, 300, sha))
        assertNull(PickedImage.running(u, 301, sha))
    }

    @Test
    fun zipGivesTheApplicationBin() {
        val m = """{"manifest":{"application":{"bin_file":"app.bin","dat_file":"app.dat"}}}""".toByteArray()
        val z = zip("other.bin" to ByteArray(5), "app.bin" to bin, "manifest.json" to m)
        assertContentEquals(bin, PickedImage.app(z))
        assertContentEquals(bin, PickedImage.running(z, 300, sha))
        assertContentEquals(bin, PickedImage.app(zip("x/firmware.bin" to bin)))
    }

    @Test
    fun plainBinAndMismatch() {
        assertContentEquals(bin, PickedImage.running(bin, 300, sha))
        assertNull(PickedImage.running(bin, 300, "00".repeat(32)))
        assertNull(PickedImage.running(zip("a.txt" to ByteArray(3)), 300, sha))
    }
}
