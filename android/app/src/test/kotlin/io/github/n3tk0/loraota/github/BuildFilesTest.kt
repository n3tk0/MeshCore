package io.github.n3tk0.loraota.github

import io.github.n3tk0.loraota.delta.DetoolsPatch
import io.github.n3tk0.loraota.ota.Releases
import io.github.n3tk0.loraota.ota.hex
import org.json.JSONArray
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.nio.file.Files
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream
import kotlin.random.Random
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertTrue

class BuildFilesTest {
    private val bin = Random(2).nextBytes(3000)
    private val zip = Random(3).nextBytes(2000)
    private fun sha(b: ByteArray) = hex(DetoolsPatch.sha256(b))

    private fun artifact(otaJson: String): ByteArray {
        val out = ByteArrayOutputStream()
        ZipOutputStream(out).use { z ->
            for ((name, data) in listOf("Xiao_nrf52_repeater-lite-v1.18.0-abc.bin" to bin,
                    "Xiao_nrf52_repeater-lite-v1.18.0-abc.zip" to zip,
                    "Xiao_nrf52_repeater-lite-v1.18.0-abc.ota.json" to otaJson.toByteArray())) {
                z.putNextEntry(ZipEntry(name))
                z.write(data)
                z.closeEntry()
            }
        }
        return out.toByteArray()
    }

    private val otaJson = JSONObject().put("env", "Xiao_nrf52_repeater").put("version", "v1.18.0-abc")
        .put("file", "Xiao_nrf52_repeater-lite-v1.18.0-abc.bin").put("size", bin.size).put("sha256", sha(bin))
        .put("dfu", "Xiao_nrf52_repeater-lite-v1.18.0-abc.zip").put("dfu_size", zip.size).put("dfu_sha256", sha(zip)).toString()

    /** An artifact's files become firmware on the phone: listed, and downloaded with no internet. */
    @Test
    fun artifactToBuild() {
        val files = BuildWatcher.unzip(artifact(otaJson))
        val entries = BuildWatcher.entries(files, "build-7")
        assertEquals(1, entries.size)
        val f = entries[0]
        assertTrue(f.isBuild && f.isLite && f.hasLoraOta && f.dfuIsZip)
        assertEquals("v1.18.0-abc · Build #7 · lite", f.label)
        val dir = Files.createTempDirectory("build").toFile()
        Releases.addBuild(dir, entries, files)
        assertEquals(listOf(f), Releases.builds(dir))
        assertContentEquals(bin, Releases.download(f, dir))
        assertContentEquals(zip, Releases.downloadDfu(f, dir))
        // a later list of releases keeps the build's files
        Releases.cleanUp(dir)
        assertContentEquals(bin, Releases.download(f, dir))
    }

    @Test
    fun damagedArtifactIsRefused() {
        val files = BuildWatcher.unzip(artifact(otaJson)).toMutableMap()
        files["Xiao_nrf52_repeater-lite-v1.18.0-abc.bin"] = bin.copyOf().also { it[0] = (it[0] + 1).toByte() }
        val dir = Files.createTempDirectory("build").toFile()
        assertFailsWith<java.io.IOException> { Releases.addBuild(dir, BuildWatcher.entries(files, "build-1"), files) }
    }

    /** Stock releases have no manifest: the flashable files by name, with GitHub's SHA-256. */
    @Test
    fun upstreamAssetNames() {
        fun asset(name: String, digest: String? = "sha256:" + "ab".repeat(32)) = JSONObject().put("name", name).put("size", 100)
            .put("browser_download_url", "https://example.invalid/$name").apply { if (digest != null) put("digest", digest) }
        val assets = JSONArray(listOf(
            asset("RAK_4631_repeater-v1.18.0-1234567.zip"),
            asset("RAK_4631_repeater-v1.18.0-1234567.uf2"),
            asset("Ebyte_EoRa-S3_Repeater-v1.18.0-1234567.bin"),
            asset("Ebyte_EoRa-S3_Repeater-v1.18.0-1234567-merged.bin"),
            asset("RAK_11310_repeater-v1.18.0-1234567.bin"),    // RP2040: no -merged.bin, the app cannot write it
            asset("Heltec_t114_repeater-v1.18.0-1234567.zip", digest = null),
        ))
        val files = Releases.fromAssetNames("repeater-v1.18.0", assets)
        assertEquals(listOf("Ebyte_EoRa-S3_Repeater", "RAK_4631_repeater"), files.map { it.env })
        val esp = files[0]
        assertEquals("Ebyte_EoRa-S3_Repeater-v1.18.0-1234567.bin", esp.dfu)
        assertEquals("v1.18.0-1234567", esp.version)
        assertTrue(!esp.hasLoraOta && esp.hasDfu && !esp.dfuIsZip)
        assertTrue(files[1].dfuIsZip)
    }
}
