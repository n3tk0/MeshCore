package io.github.n3tk0.loraota.ota

import org.json.JSONArray
import org.json.JSONObject
import java.nio.file.Files
import kotlin.test.Test
import kotlin.test.assertEquals

/** The board's title and description (English / Bulgarian) of ota-manifest.json (tools/board-info.sh). */
class ReleasesBoardInfoTest {
    private fun release(manifestFiles: String): Pair<JSONArray, (String) -> ByteArray> {
        val rel = JSONObject().put("tag_name", "repeaters-v1.18.0-n1").put("published_at", "2026-11-19T10:00:00Z")
            .put("assets", JSONArray()
                .put(JSONObject().put("name", "ota-manifest.json").put("browser_download_url", "http://m"))
                .put(JSONObject().put("name", "E-v1-abc1234.bin").put("browser_download_url", "http://e")))
        return JSONArray().put(rel) to { _: String -> """{"files":[$manifestFiles]}""".toByteArray() }
    }

    @Test
    fun titleAndDescriptionsAreParsed() {
        val (rels, fetch) = release("""{"env":"E","version":"v1","file":"E-v1-abc1234.bin","size":3,"sha256":"ab",
            "title":"Board E","description":"nRF52840 · SX1262","description_bg":"nRF52840 · SX1262 (bg)"}""")
        val f = Releases.parseReleases("o/r", rels, fetch).single()
        assertEquals("Board E", f.title)
        assertEquals("nRF52840 · SX1262", f.description)
        assertEquals("nRF52840 · SX1262 (bg)", f.descriptionBg)
        assertEquals("nRF52840 · SX1262 (bg)", f.descriptionFor("bg"))
        assertEquals("nRF52840 · SX1262", f.descriptionFor("en"))
    }

    @Test
    fun oldManifestWithoutThemHasEmptyFields() {
        val (rels, fetch) = release("""{"env":"E","version":"v1","file":"E-v1-abc1234.bin","size":3,"sha256":"ab"}""")
        val f = Releases.parseReleases("o/r", rels, fetch).single()
        assertEquals("", f.title)
        assertEquals("", f.description)
        assertEquals("", f.descriptionFor("bg"))
    }

    @Test
    fun nullValuesAreEmptyAndBulgarianFallsBackToEnglish() {
        val (rels, fetch) = release("""{"env":"E","file":"E-v1-abc1234.bin","size":3,"sha256":"ab",
            "title":null,"description":"only English","description_bg":null}""")
        val f = Releases.parseReleases("o/r", rels, fetch).single()
        assertEquals("", f.title)
        assertEquals("only English", f.descriptionFor("bg"))
    }

    @Test
    fun fieldsSurviveTheCache() {
        val dir = Files.createTempDirectory("ota-info").toFile()
        val f = FirmwareFile("r1", "E", "v1", "E.bin", 3, "ab", "http://e", title = "Board E",
            description = "en text", descriptionBg = "bg text")
        Releases.addBuild(dir, listOf(f.copy(file = "", size = 0, sha256 = "", url = "", dfu = "", dfuSize = 0)), emptyMap())
        val back = Releases.builds(dir).single()
        assertEquals("Board E", back.title)
        assertEquals("en text", back.description)
        assertEquals("bg text", back.descriptionBg)
    }
}
