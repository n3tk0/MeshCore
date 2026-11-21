package io.github.n3tk0.loraota.ota

import org.json.JSONArray
import org.json.JSONObject
import java.nio.file.Files
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertNotNull
import kotlin.test.assertNull

class ReleasesSearchTest {
    private fun fw(sha: String, published: String = "", env: String = "Xiao_nrf52_repeater") =
        FirmwareFile("r-$sha", env, "v1", "$env.bin", 10, sha, "http://x/$sha", published = published)

    private fun page(vararg shas: String, releases: Int = 50) = Releases.Page(releases, shas.map { fw(it) })

    private fun found(r: Releases.SearchResult) = (r as? Releases.SearchResult.Found)?.file

    @Test
    fun foundOnALaterPageStopsTheSearch() {
        val asked = ArrayList<Int>()
        val r = Releases.searchPages("aa") { n ->
            asked.add(n)
            if (n == 4) page("aa") else page("n$n")
        }
        assertEquals("aa", found(r)?.sha256)
        assertEquals(listOf(2, 3, 4), asked)   // page 1 is the list's own
    }

    @Test
    fun emptyPageIsNotFound() {
        val asked = ArrayList<Int>()
        assertEquals(Releases.SearchResult.NotFound,
            Releases.searchPages("aa") { n -> asked.add(n); if (n == 3) Releases.Page(0, emptyList()) else page("n$n") })
        assertEquals(listOf(2, 3), asked)
    }

    @Test
    fun shortPageIsTheLastOne() {
        val asked = ArrayList<Int>()
        assertEquals(Releases.SearchResult.NotFound, Releases.searchPages("aa") { n -> asked.add(n); page("n$n", releases = 7) })
        assertEquals(listOf(2), asked)
    }

    @Test
    fun theCapIsIncomplete() {
        val asked = ArrayList<Int>()
        assertEquals(Releases.SearchResult.Incomplete, Releases.searchPages("aa") { n -> asked.add(n); page("n$n") })
        assertEquals((2..Releases.MAX_PAGES).toList(), asked)
        asked.clear()
        assertEquals(Releases.SearchResult.Incomplete, Releases.searchPages("aa", Releases.MAX_PAGES_RETRY) { n -> asked.add(n); page("n$n") })
        assertEquals(30, asked.last())
    }

    @Test
    fun aFailedManifestMakesTheEndIncomplete() {
        val r = Releases.searchPages("aa") { n -> if (n == 3) Releases.Page(7, emptyList(), incomplete = true) else page("n$n") }
        assertEquals(Releases.SearchResult.Incomplete, r)
    }

    @Test
    fun aFailedLaterPageIsIncompleteButTheFirstOneThrows() {
        assertEquals(Releases.SearchResult.Incomplete,
            Releases.searchPages("aa") { n -> if (n == 3) throw java.io.IOException("x") else page("n$n") })
        assertFailsWith<java.io.IOException> { Releases.searchPages("aa") { throw java.io.IOException("offline") } }
    }

    @Test
    fun emptyShaNeverMatches() {
        assertEquals(Releases.SearchResult.NotFound,
            Releases.searchPages("") { n -> if (n == 2) Releases.Page(7, listOf(fw(""))) else Releases.Page(0, emptyList()) })
    }

    private fun release(tag: String, vararg assets: JSONObject) = JSONObject().put("tag_name", tag)
        .put("assets", JSONArray().apply { assets.forEach { put(it) } })

    private fun asset(name: String, digest: String? = null) = JSONObject().put("name", name)
        .put("browser_download_url", "http://x/$name").apply { if (digest != null) put("digest", "sha256:$digest") }

    private fun manifest(sha: String) = """{"files":[{"env":"E","version":"v1","file":"E.bin","size":3,"sha256":"$sha"}]}"""

    @Test
    fun digestsSkipTheManifestsOfOtherReleases() {
        val fetched = ArrayList<String>()
        val releases = JSONArray()
            .put(release("r-other", asset("ota-manifest.json", "00"), asset("E.bin", "bb")))
            .put(release("r-match", asset("ota-manifest.json", "00"), asset("E.bin", "AA")))
        val out = Releases.parseReleases("n3tk0/MeshCore", releases, { fetched.add(it); manifest("aa").toByteArray() },
            { Releases.mayHave(it, "aa") })
        assertEquals(listOf("http://x/ota-manifest.json"), fetched)
        assertEquals(listOf("r-match"), out.map { it.release })
    }

    @Test
    fun aBinWithNoDigestIsChecked() {
        val fetched = ArrayList<String>()
        val releases = JSONArray().put(release("r-old", asset("ota-manifest.json"), asset("E.bin")))
        val out = Releases.parseReleases("n3tk0/MeshCore", releases, { fetched.add(it); manifest("aa").toByteArray() },
            { Releases.mayHave(it, "aa") })
        assertEquals(1, fetched.size)
        assertEquals(1, out.size)
    }

    @Test
    fun aFailedManifestOfACheckedReleaseIsReported() {
        var failed = 0
        val releases = JSONArray()
            .put(release("r1", asset("ota-manifest.json"), asset("E_repeater.bin", "aa")))
            .put(release("r2", asset("ota-manifest.json"), asset("E_repeater.bin", "cc")))   // skipped: not reported
        val out = Releases.parseReleases("n3tk0/MeshCore", releases, { throw java.io.IOException("x") },
            { Releases.mayHave(it, "aa") }, { _, _ -> failed++ })
        assertEquals(1, out.size)   // from the asset list, by its digest
        assertEquals(1, failed)
    }

    @Test
    fun findOlderUsesTheKeptFindWithNoNetwork() {
        val dir = Files.createTempDirectory("ota").toFile()
        val older = dir.resolve("releases-older-n3tk0_MeshCore.json")
        older.writeText(JSONArray().put(JSONObject().put("release", "boards-1").put("env", "E").put("version", "v1")
            .put("file", "E.bin").put("size", 10).put("sha256", "bb").put("url", "http://127.0.0.1:1/x")).toString())
        assertEquals("boards-1", found(Releases.findOlder("n3tk0/MeshCore", "bb", dir))?.release)
        assertEquals(1, Releases.olderCached("n3tk0/MeshCore", dir).size)
    }

    @Test
    fun releasePageKeepsThePublishDate() {
        val rel = JSONObject().put("tag_name", "boards-5").put("published_at", "2026-11-01T10:00:00Z").put("assets", JSONArray()
            .put(JSONObject().put("name", "ota-manifest.json").put("browser_download_url", "http://m"))
            .put(JSONObject().put("name", "E.bin").put("browser_download_url", "http://e")))
        val manifest = """{"files":[{"env":"E","version":"v1","file":"E.bin","size":3,"sha256":"AB"}]}"""
        val out = Releases.parseReleases("n3tk0/MeshCore", JSONArray().put(rel), { manifest.toByteArray() })
        assertEquals(1, out.size)
        assertEquals("ab", out[0].sha256)
        assertEquals("2026-11-01T10:00:00Z", out[0].published)
        assertNotNull(out[0].publishedMillis.takeIf { it > 0 })
    }

    @Test
    fun recommendedIsTheNewestByDate() {
        val list = listOf(fw("a", "2026-01-01T00:00:00Z"), fw("b", "2026-03-01T00:00:00Z"), fw("c", "2026-02-01T00:00:00Z"))
        assertEquals("b", Releases.recommended(list)?.sha256)
    }

    @Test
    fun recommendedWithNoDatesIsTheFirst() {
        assertEquals("a", Releases.recommended(listOf(fw("a"), fw("b")))?.sha256)
        assertNull(Releases.recommended(emptyList()))
    }

    @Test
    fun anAppBuildIsDatedAndRecommendedOverAnOlderRelease() {
        val dir = Files.createTempDirectory("ota").toFile()
        val data = ByteArray(10) { it.toByte() }
        val sha = hex(io.github.n3tk0.loraota.delta.DetoolsPatch.sha256(data))
        Releases.addBuild(dir, listOf(FirmwareFile("build-3", "E", "v1", "E.bin", 10, sha, "")), mapOf("E.bin" to data))
        val b = Releases.builds(dir)
        assertEquals(1, b.size)
        assert(b[0].publishedMillis > 0)
        val release = fw("zz", "2020-01-01T00:00:00Z", "E")
        assertEquals("build-3", Releases.recommended(listOf(release) + b)?.release)
    }
}
