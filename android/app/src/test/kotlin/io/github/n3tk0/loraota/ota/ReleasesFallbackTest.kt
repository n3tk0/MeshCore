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
        val (files, _) = Releases.fromBinAssets("t", JSONArray(listOf(asset("E-1.bin", "aa".repeat(32)), asset("E-1-merged.bin", "bb".repeat(32)))))
        assertEquals(listOf("E-1.bin"), files.map { it.file })
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
