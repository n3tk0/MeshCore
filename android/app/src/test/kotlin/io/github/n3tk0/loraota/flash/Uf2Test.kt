package io.github.n3tk0.loraota.flash

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Test
import java.io.ByteArrayInputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.zip.ZipInputStream

class Uf2Test {
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

    @Test
    fun crcIsCcittFalse() {
        assertEquals(0x29B1, Uf2.crc16("123456789".toByteArray()))
    }

    @Test
    fun imageAndZip() {
        val app = ByteArray(1000) { (it * 7).toByte() }
        assertArrayEquals(app, Uf2.image(uf2(0x27000, app)))
        val files = HashMap<String, ByteArray>()
        ZipInputStream(ByteArrayInputStream(Uf2.toDfuZip(uf2(0x27000, app)))).use { z ->
            while (true) {
                val e = z.nextEntry ?: break
                files[e.name] = z.readBytes()
            }
        }
        assertArrayEquals(app, files["firmware.bin"])
        val crc = Uf2.crc16(app)
        val dat = files["firmware.dat"]!!
        assertEquals(14, dat.size)
        assertEquals(crc, (dat[12].toInt() and 0xff) or ((dat[13].toInt() and 0xff) shl 8))
        assertEquals(true, String(files["manifest.json"]!!).contains("\"firmware_crc16\":$crc"))
    }

    @Test(expected = IllegalArgumentException::class)
    fun aBootloaderIsRefused() {
        Uf2.image(uf2(0xF4000, ByteArray(512)))
    }
}
