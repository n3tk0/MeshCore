package io.github.n3tk0.loraota.github

import org.json.JSONArray
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class AppUpdaterTest {

    @Test
    fun cleansTagAndVersionPrefixes() {
        assertEquals("1.2.3", AppUpdater.cleanVersion("lora-ota-app-v1.2.3"))
        assertEquals("0.2.0", AppUpdater.cleanVersion("lora-ota-app-0.2.0"))
        assertEquals("0.2.0-build.42", AppUpdater.cleanVersion("v0.2.0-build.42"))
        assertEquals("1.0.0", AppUpdater.cleanVersion("1.0.0"))
    }

    @Test
    fun comparesSemanticAndBuildVersions() {
        assertTrue(AppUpdater.isNewer("0.2.0", "0.1.0"))
        assertTrue(AppUpdater.isNewer("1.0.0", "0.9.9"))
        assertTrue(AppUpdater.isNewer("0.2.1", "0.2.0"))
        assertTrue(AppUpdater.isNewer("0.2.0", "0.2.0-build.42"))
        assertTrue(AppUpdater.isNewer("0.2.0-build.10", "0.2.0-build.2"))
        assertTrue(AppUpdater.isNewer("0.2.0-rc.1", "0.2.0-beta.2"))
        assertTrue(AppUpdater.isNewer("0.2.0-rc.2", "0.2.0-alpha.2"))
        assertFalse(AppUpdater.isNewer("0.2.0", "0.2.0"))
        assertFalse(AppUpdater.isNewer("0.1.9", "0.2.0"))
        assertFalse(AppUpdater.isNewer("0.2.0-build.5", "0.2.0"))
    }

    @Test
    fun parsesLatestAppReleaseAndIgnoresFirmwareAndDrafts() {
        val json = JSONArray(
            """
            [
              {
                "tag_name": "boards-20",
                "draft": false,
                "name": "Board builds #20",
                "assets": [
                  {"name": "ota-manifest.json", "browser_download_url": "https://example.com/m.json", "size": 100}
                ]
              },
              {
                "tag_name": "lora-ota-app-v0.3.0",
                "draft": true,
                "name": "Draft v0.3.0",
                "assets": [
                  {"name": "LoraOta-lora-ota-app-v0.3.0.apk", "browser_download_url": "https://example.com/030.apk", "size": 5000000}
                ]
              },
              {
                "tag_name": "lora-ota-app-v0.2.0",
                "draft": false,
                "name": "LoRa OTA app v0.2.0",
                "body": "Settings and theme support",
                "html_url": "https://github.com/n3tk0/MeshCore/releases/tag/lora-ota-app-v0.2.0",
                "published_at": "2026-11-20T12:00:00Z",
                "assets": [
                  {"name": "LoraOta-group-v0.2.0.apk", "browser_download_url": "https://example.com/group.apk", "size": 5000001},
                  {"name": "LoraOta-lora-ota-app-v0.2.0.apk", "browser_download_url": "https://example.com/020.apk", "size": 5242880, "digest": "sha256:ABCDEF0123456789"}
                ]
              },
              {
                "tag_name": "lora-ota-app-v0.1.0",
                "draft": false,
                "name": "LoRa OTA app v0.1.0",
                "assets": [
                  {"name": "LoraOta-lora-ota-app-v0.1.0.apk", "browser_download_url": "https://example.com/010.apk", "size": 4000000}
                ]
              }
            ]
            """.trimIndent()
        )
        val latest = AppUpdater.parseLatest(json)
        assertNotNull(latest)
        assertEquals("lora-ota-app-v0.2.0", latest!!.tag)
        assertEquals("0.2.0", latest.version)
        assertEquals("LoraOta-lora-ota-app-v0.2.0.apk", latest.apkName)
        assertEquals("https://example.com/020.apk", latest.apkUrl)
        assertEquals(5242880L, latest.apkSize)
        assertEquals("abcdef0123456789", latest.apkSha256)
        assertEquals("Settings and theme support", latest.notes)
    }

    @Test
    fun returnsNullWhenNoAppReleaseExists() {
        val json = JSONArray("""[{"tag_name": "boards-17", "draft": false, "assets": []}]""")
        assertNull(AppUpdater.parseLatest(json))
    }

    @Test
    fun findsAppReleaseOnSecondPageWhenPageOneIsFullOfFirmwareReleases() {
        val page1 = JSONArray().apply {
            repeat(50) { i ->
                put(
                    org.json.JSONObject()
                        .put("tag_name", "boards-${100 - i}")
                        .put("draft", false)
                        .put("assets", JSONArray())
                )
            }
        }
        val page2 = JSONArray(
            """
            [
              {
                "tag_name": "lora-ota-app-v0.4.0",
                "draft": false,
                "name": "LoRa OTA app v0.4.0",
                "assets": [
                  {"name": "LoraOta-lora-ota-app-v0.4.0.apk", "browser_download_url": "https://example.com/040.apk", "size": 6000000}
                ]
              }
            ]
            """.trimIndent()
        )
        var fetchedPages = 0
        val found = AppUpdater.findLatestAcrossPages(maxPages = 5) { p ->
            fetchedPages++
            if (p == 1) page1 else page2
        }
        assertEquals(2, fetchedPages)
        assertNotNull(found)
        assertEquals("0.4.0", found!!.version)
    }
}
