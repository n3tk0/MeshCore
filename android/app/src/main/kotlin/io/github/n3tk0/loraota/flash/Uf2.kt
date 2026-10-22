package io.github.n3tk0.loraota.flash

import java.io.ByteArrayOutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

/**
 * A .uf2 for the nRF52 bootloader's USB drive made into the .zip its Bluetooth DFU takes (what
 * adafruit-nrfutil `dfu genpkg` makes from the .hex): the application only, any SoftDevice.
 */
object Uf2 {
    private const val MAGIC0 = 0x0A324655
    private const val MAGIC1 = 0x9E5D5157.toInt()
    private const val MAGIC_END = 0x0AB16F30
    private const val FLAG_NOT_MAIN_FLASH = 0x1
    private const val FLAG_FAMILY = 0x2000
    private const val FAMILY_NRF52840 = 0xADA52840.toInt()
    private const val FAMILY_NRF52 = 0x1B57745F

    /** Where an application may sit: after the SoftDevice (S132/S140 v6: 0x26000, v7: 0x27000), before the bootloader. */
    private const val APP_MIN = 0x19000
    private const val APP_END = 0xF4000

    /** The application image: the blocks' data, gaps filled with 0xFF. */
    fun image(uf2: ByteArray): ByteArray {
        if (uf2.isEmpty() || uf2.size % 512 != 0) throw IllegalArgumentException("not a .uf2")
        val parts = ArrayList<Pair<Int, ByteArray>>()
        val b = ByteBuffer.wrap(uf2).order(ByteOrder.LITTLE_ENDIAN)
        for (off in uf2.indices step 512) {
            if (b.getInt(off) != MAGIC0 || b.getInt(off + 4) != MAGIC1 || b.getInt(off + 508) != MAGIC_END) {
                throw IllegalArgumentException("not a .uf2")
            }
            val flags = b.getInt(off + 8)
            if (flags and FLAG_NOT_MAIN_FLASH != 0) continue
            val family = b.getInt(off + 28)
            if (flags and FLAG_FAMILY != 0 && family != FAMILY_NRF52840 && family != FAMILY_NRF52) {
                throw IllegalArgumentException("not for an nRF52")
            }
            val addr = b.getInt(off + 12)
            val size = b.getInt(off + 16)
            if (size <= 0 || size > 476) throw IllegalArgumentException("not a .uf2")
            parts.add(addr to uf2.copyOfRange(off + 32, off + 32 + size))
        }
        if (parts.isEmpty()) throw IllegalArgumentException("empty .uf2")
        val start = parts.minOf { it.first }
        val end = parts.maxOf { it.first + it.second.size }
        // a bootloader or SoftDevice .uf2 cannot go this way
        if (start < APP_MIN || end > APP_END) throw IllegalArgumentException("not an application")
        val out = ByteArray(end - start) { 0xFF.toByte() }
        for ((addr, data) in parts) data.copyInto(out, addr - start)
        return out
    }

    /** CRC-16/CCITT-FALSE, as the legacy init packet has it. */
    fun crc16(data: ByteArray): Int {
        var crc = 0xFFFF
        for (x in data) {
            crc = ((crc ushr 8) and 0xFF) or ((crc shl 8) and 0xFFFF)
            crc = crc xor (x.toInt() and 0xFF)
            crc = crc xor ((crc and 0xFF) ushr 4)
            crc = crc xor ((crc shl 12) and 0xFFFF)
            crc = crc xor (((crc and 0xFF) shl 5) and 0xFFFF)
        }
        return crc
    }

    fun toDfuZip(uf2: ByteArray): ByteArray {
        val bin = image(uf2)
        val crc = crc16(bin)
        // device type 0x0052 (Adafruit), revision and version any, one SoftDevice: any (0xFFFE), CRC
        val dat = ByteBuffer.allocate(14).order(ByteOrder.LITTLE_ENDIAN)
            .putShort(0x0052).putShort(-1).putInt(-1).putShort(1).putShort(0xFFFE.toShort()).putShort(crc.toShort())
            .array()
        val manifest = """{"manifest":{"application":{"bin_file":"firmware.bin","dat_file":"firmware.dat",""" +
            """"init_packet_data":{"application_version":4294967295,"device_revision":65535,"device_type":82,""" +
            """"firmware_crc16":$crc,"softdevice_req":[65534]}},"dfu_version":0.5}}"""
        val bytes = ByteArrayOutputStream()
        ZipOutputStream(bytes).use { z ->
            for ((name, data) in listOf("firmware.bin" to bin, "firmware.dat" to dat, "manifest.json" to manifest.toByteArray())) {
                z.putNextEntry(ZipEntry(name))
                z.write(data)
                z.closeEntry()
            }
        }
        return bytes.toByteArray()
    }
}
