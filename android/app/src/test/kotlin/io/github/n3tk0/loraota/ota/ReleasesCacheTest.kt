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

    @Test
    fun cachedFirmwaresListsBuildsReadCachedFileAndRemovesCleanly() {
        val dir = Files.createTempDirectory("ota-offline").toFile()
        val dfuBytes = Random(2).nextBytes(2048)
        val entry = FirmwareFile(
            release = "build-42",
            env = "Heltec_v3_room_server",
            version = "v1.18.0-abc1234",
            file = "",
            size = 0,
            sha256 = "",
            url = "",
            dfu = "Heltec_v3_room_server-v1.18.0-abc1234.bin",
            dfuSize = dfuBytes.size,
            dfuSha256 = hex(DetoolsPatch.sha256(dfuBytes)),
            dfuUrl = "",
        )
        Releases.addBuild(dir, listOf(entry), mapOf(entry.dfu to dfuBytes))

        val listed = Releases.cachedFirmwares(Releases.DEFAULT_REPO, dir)
        kotlin.test.assertEquals(1, listed.size)
        kotlin.test.assertEquals("Heltec_v3_room_server", listed.first().env)
        kotlin.test.assertFalse(Releases.isFileCached(listed.first(), dir))
        kotlin.test.assertTrue(Releases.isDfuCached(listed.first(), dir))
        assertContentEquals(dfuBytes, Releases.readCachedFile(listed.first(), dfu = true, cache = dir))

        Releases.removeCachedFirmware(dir, listed.first())
        kotlin.test.assertTrue(Releases.cachedFirmwares(Releases.DEFAULT_REPO, dir).isEmpty())
        kotlin.test.assertFalse(Releases.isDfuCached(listed.first(), dir))
    }
}
