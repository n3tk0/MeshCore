package io.github.n3tk0.loraota.ota

import io.github.n3tk0.loraota.delta.DetoolsPatch
import java.nio.file.Files
import kotlin.random.Random
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertFailsWith

class ReleasesCacheTest {
    private val data = Random(1).nextBytes(5000)
    private fun file(url: String) = FirmwareFile("r1", "Xiao_nrf52_repeater", "v1", "x.bin", data.size,
        hex(DetoolsPatch.sha256(data)), url)

    /** On site with no internet: a .bin downloaded before comes from the phone. */
    @Test
    fun keptBinNeedsNoInternet() {
        val dir = Files.createTempDirectory("ota").toFile()
        val bin = dir.resolve("firmware").apply { mkdirs() }.resolve("${file("").sha256}.bin")
        bin.writeBytes(data)
        assertContentEquals(data, Releases.download(file("http://127.0.0.1:1/none"), dir))
    }

    /** A damaged kept file is not used: it is dropped and fetched again (here: no network, so it fails). */
    @Test
    fun damagedKeptBinIsNotUsed() {
        val dir = Files.createTempDirectory("ota").toFile()
        val bin = dir.resolve("firmware").apply { mkdirs() }.resolve("${file("").sha256}.bin")
        bin.writeBytes(data.copyOf().also { it[10] = (it[10] + 1).toByte() })
        assertFailsWith<Exception> { Releases.download(file("http://127.0.0.1:1/none"), dir) }
        kotlin.test.assertFalse(bin.exists())
    }
}
