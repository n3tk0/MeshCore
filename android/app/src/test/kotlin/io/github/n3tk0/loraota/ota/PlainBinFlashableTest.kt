package io.github.n3tk0.loraota.ota

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class PlainBinFlashableTest {
    private val binOnly = FirmwareFile("r", "heltec_v3_repeater", "v1", "a.bin", 100, "aa", "u")
    private val withDfu = binOnly.copy(dfu = "a.zip", dfuSha256 = "bb")

    @Test
    fun esp32ImageMagic() {
        assertTrue(Releases.isEsp32Image(ByteArray(64).also { it[0] = 0xE9.toByte() }))
        // nRF52 .bin: first word is the initial stack pointer (0x2003xxxx little endian)
        assertFalse(Releases.isEsp32Image(byteArrayOf(0x00, 0x00, 0x04, 0x20) + ByteArray(60)))
        assertFalse(Releases.isEsp32Image(null))
    }

    @Test
    fun platformOrImageDecides() {
        assertTrue(binOnly.plainBinFlashable(OtaProtocol.PLATFORM_ESP32))
        assertFalse(binOnly.plainBinFlashable(OtaProtocol.PLATFORM_NRF52, esp32Image = true))
        assertTrue(binOnly.plainBinFlashable(null, esp32Image = true))
        assertFalse(binOnly.plainBinFlashable(null))
        assertFalse(withDfu.plainBinFlashable(OtaProtocol.PLATFORM_ESP32))
    }
}
