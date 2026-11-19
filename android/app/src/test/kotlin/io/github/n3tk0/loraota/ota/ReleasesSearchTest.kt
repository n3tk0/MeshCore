package io.github.n3tk0.loraota.ota

import org.json.JSONArray
import org.json.JSONObject
import java.nio.file.Files
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import kotlin.test.assertNull

class ReleasesSearchTest {
    private fun fw(sha: String, published: String = "", env: String = "Xiao_nrf52_repeater") =
        FirmwareFile("r-$sha", env, "v1", "$env.bin", 10, sha, "http://x/$sha", published = published)

    private fun page(vararg shas: String, releases: Int = 50) = Releases.Page(releases, shas.map { fw(it) })

    @Test
    fun foundOnALaterPageStopsTheSearch() {
        val asked = ArrayList<Int>()
        val f = Releases.searchPages("aa") { n ->
            asked.add(n)
            if (n == 4) page("aa") else page("n$n")
        }
        assertEquals("aa", f?.sha256)
        assertEquals(listOf(2, 3, 4), asked)   // page 1 is the list's own
    }

    @Test
    fun emptyPageEndsTheSearch() {
        val asked = ArrayList<Int>()
        assertNull(Releases.searchPages("aa") { n -> asked.add(n); if (n == 3) Releases.Page(0, emptyList()) else page("n$n") })
        assertEquals(listOf(2, 3), asked)
    }

    @Test
    fun shortPageIsTheLastOne() {
        val asked = ArrayList<Int>()
        assertNull(Releases.searchPages("aa") { n -> asked.add(n); page("n$n", releases = 7) })
        assertEquals(listOf(2), asked)
    }

    @Test
    fun givesUpAfterTenPages() {
        val asked = ArrayList<Int>()
        assertNull(Releases.searchPages("aa") { n -> asked.add(n); page("n$n") })
        assertEquals((2..Releases.MAX_PAGES).toList(), asked)
        assertEquals(10, asked.last())
    }

    @Test
    fun emptyShaNeverMatches() {
        assertNull(Releases.searchPages("") { n -> if (n == 2) Releases.Page(50, listOf(fw(""))) else Releases.Page(0, emptyList()) })
    }

    @Test
    fun findOlderUsesTheKeptFindWithNoNetwork() {
        val dir = Files.createTempDirectory("ota").toFile()
        val older = dir.resolve("releases-older-n3tk0_MeshCore.json")
        older.writeText(JSONArray().put(JSONObject().put("release", "boards-1").put("env", "E").put("version", "v1")
            .put("file", "E.bin").put("size", 10).put("sha256", "bb").put("url", "http://127.0.0.1:1/x")).toString())
        assertEquals("boards-1", Releases.findOlder("n3tk0/MeshCore", "bb", dir)?.release)
        assertEquals(1, Releases.olderCached("n3tk0/MeshCore", dir).size)
    }

    @Test
    fun releasePageKeepsThePublishDate() {
        val rel = JSONObject().put("tag_name", "boards-5").put("published_at", "2026-11-01T10:00:00Z").put("assets", JSONArray()
            .put(JSONObject().put("name", "ota-manifest.json").put("browser_download_url", "http://m"))
            .put(JSONObject().put("name", "E.bin").put("browser_download_url", "http://e")))
        val manifest = """{"files":[{"env":"E","version":"v1","file":"E.bin","size":3,"sha256":"AB"}]}"""
        val out = Releases.parseReleases("n3tk0/MeshCore", JSONArray().put(rel)) { manifest.toByteArray() }
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
