package io.github.n3tk0.loraota.ota

import org.json.JSONArray
import org.json.JSONObject
import java.io.IOException
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class ReleasesFallbackTest {
    private val sha = "3e6246fd" + "0".repeat(56)
    private val zipSha = "ab".repeat(32)
    private val bin = "Xiao_nrf52_repeater-lite-claude-x-4fb105b.bin"
    private val zip = "Xiao_nrf52_repeater-lite-claude-x-4fb105b.zip"

    private fun asset(name: String, digest: String? = null, size: Int = 100) = JSONObject().put("name", name)
        .put("browser_download_url", "https://github.com/o/r/releases/download/boards-15/$name").put("size", size)
        .apply { if (digest != null) put("digest", "sha256:$digest") }

    private fun release(vararg a: JSONObject) = JSONArray().put(JSONObject().put("tag_name", "boards-15")
        .put("published_at", "2026-11-19T10:00:00Z").put("assets", JSONArray(a.toList())))

    private val assets = arrayOf(asset("ota-manifest.json"), asset("SHA256SUMS"), asset(bin, sha, 394852), asset(zip, zipSha, 5000))

    @Test
    fun manifestFailureFallsBackToDigests() {
        var err: Throwable? = null
        var covered = false
        val files = Releases.parseReleases("o/r", release(*assets), { throw IOException("tls") }, onFetchFailed = { e, c -> err = e; covered = c })
        assertTrue(err is IOException)
        assertTrue(covered)
        val f = files.single()
        assertEquals("Xiao_nrf52_repeater", f.env)
        assertEquals("", f.version)
        assertEquals(sha, f.sha256)
        assertEquals(394852, f.size)
        assertTrue(f.hasLoraOta)
        assertEquals(zip, f.dfu)
        assertEquals(zipSha, f.dfuSha256)
        assertEquals("boards-15", f.release)
        assertEquals("4fb105b", f.commit)
    }

    @Test
    fun binWithoutDigestIsNotCovered() {
        var covered = true
        val files = Releases.parseReleases("o/r", release(asset("ota-manifest.json"), asset(bin)), { throw IOException("x") },
            onFetchFailed = { _, c -> covered = c })
        assertTrue(files.isEmpty())
        assertFalse(covered)
    }

    @Test
    fun mergedBinIsNotAnOtaImage() {
        val (files, _) = Releases.fromBinAssets("t", JSONArray(listOf(asset("E_repeater-1.bin", "aa".repeat(32)), asset("E_repeater-1-merged.bin", "bb".repeat(32)))))
        assertEquals(listOf("E_repeater-1.bin"), files.map { it.file })
        // ESP32: the app .bin is also the Wi-Fi OTA image, as the manifest would list it
        val f = files.single()
        assertEquals("E_repeater-1.bin", f.dfu)
        assertEquals("aa".repeat(32), f.dfuSha256)
        assertTrue(f.hasDfu)
    }

    @Test
    fun envsWithDashesAreKept() {
        for (env in listOf("LilyGo_T-Echo_repeater", "LilyGo_T-Echo-Lite_repeater", "Ebyte_EoRa-S3_Repeater", "WioTrackerL1-1W_repeater",
            "wio-e5_repeater", "RAK_4631_companion_radio_ble", "Heltec_v3_room_server")) {
            assertEquals(env, Releases.envOf("$env-v1.18.0-abc1234"))
            assertEquals(env, Releases.envOf("$env-lite-v1.18.0-abc1234"))
            assertEquals(env, Releases.envOf("$env-lite-branch-abc1234"))
        }
        assertEquals("Foo", Releases.envOf("Foo-lite-v1-abc1234"))
        assertEquals("Foo", Releases.envOf("Foo-v1.2-abc1234"))
        val (files, _) = Releases.fromBinAssets("t", JSONArray(listOf(asset("LilyGo_T-Echo_repeater-v1-abc1234.bin", "aa".repeat(32)))))
        assertEquals("LilyGo_T-Echo_repeater", files.single().env)
    }

    @Test
    fun nonRepeatersAreDfuOnly() {
        val (files, covered) = Releases.fromBinAssets("t", JSONArray(listOf(
            asset("RAK_4631_companion_radio_ble-v1-abc1234.zip", "cc".repeat(32), 700),
            asset("RAK_4631_companion_radio_ble-v1-abc1234.uf2", "dd".repeat(32)),
            asset("Heltec_v3_companion_radio_ble-v1-abc1234.bin", "ee".repeat(32), 900),
            asset("Heltec_v3_companion_radio_ble-v1-abc1234-merged.bin", "ff".repeat(32)))))
        assertTrue(covered)
        assertEquals(2, files.size)
        val nrf = files.first { it.env == "RAK_4631_companion_radio_ble" }
        assertFalse(nrf.hasLoraOta)
        assertEquals("", nrf.file)
        assertEquals("RAK_4631_companion_radio_ble-v1-abc1234.zip", nrf.dfu)
        assertEquals("cc".repeat(32), nrf.dfuSha256)
        val esp = files.first { it.env == "Heltec_v3_companion_radio_ble" }
        assertFalse(esp.hasLoraOta)
        assertEquals("ee".repeat(32), esp.dfuSha256)
    }

    @Test
    fun failedManifestKeepsTheCachedEntries() {
        val cache = java.nio.file.Files.createTempDirectory("rel").toFile()
        val full = FirmwareFile("boards-15", "X_repeater", "v1", "X_repeater.bin", 5, "aa", "u", "X_repeater.zip", 6, "bb", "z")
        val fb = FirmwareFile("boards-15", "X_repeater", "", "X_repeater.bin", 5, "aa", "u")
        val other = FirmwareFile("boards-14", "Y_repeater", "v0", "Y.bin", 5, "cc", "u")
        val prev = listOf(full, other)
        val l = Releases.Listing(listOf(fb, other), 1, "x", setOf("boards-15"))
        val m = Releases.withCachedFallbacks(l, prev)
        assertEquals(listOf(full, other), m.files)
        // not in the cache: the fallback stays
        assertEquals(listOf(fb, other), Releases.withCachedFallbacks(l, listOf(other)).files)
        cache.deleteRecursively()
    }

    @Test
    fun searchFindsByDigestWithoutManifest() {
        val rel = release(*assets)
        val a = rel.getJSONObject(0).getJSONArray("assets")
        assertTrue(Releases.mayHave(a, sha))
        assertFalse(Releases.mayHave(a, "00".repeat(32)))
        val files = Releases.parseReleases("o/r", rel, { throw IOException("tls") }, { Releases.mayHave(it, sha) })
        assertEquals(sha, files.single().sha256)
        // another sha: skipped without a fetch
        var fetched = false
        Releases.parseReleases("o/r", rel, { fetched = true; ByteArray(0) }, { Releases.mayHave(it, "00".repeat(32)) })
        assertFalse(fetched)
    }

    @Test
    fun describesErrors() {
        assertEquals("HTTP 403", Releases.describe(HttpError(403, "u")))
        assertEquals("IOException: tls", Releases.describe(IOException("tls")))
    }
}
