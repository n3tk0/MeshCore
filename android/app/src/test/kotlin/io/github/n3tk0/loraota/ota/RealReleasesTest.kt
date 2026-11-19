package io.github.n3tk0.loraota.ota

import io.github.n3tk0.loraota.flash.BoardCheck
import io.github.n3tk0.loraota.flash.Role
import org.json.JSONArray
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull
import kotlin.test.assertTrue

/** The real n3tk0/MeshCore release list and manifests (boards-10..17), 2026-11-19. */
class RealReleasesTest {
    private fun res(n: String) = javaClass.getResourceAsStream("/releases/$n")!!.readBytes()

    private fun parse(): List<FirmwareFile> {
        val rels = JSONArray(String(res("releases.json")))
        return Releases.parseReleases(Releases.DEFAULT_REPO, rels, { url ->
            res("manifest-" + url.substringAfter("/download/").substringBefore("/") + ".json")
        })
    }

    @Test
    fun everyReleaseIsListed() {
        val all = parse()
        all.forEach { println("${it.release} ${it.env} lora=${it.hasLoraOta} dfu=${it.hasDfu} ${it.published}") }
        for (n in 10..17) assertTrue(all.any { it.release == "boards-$n" }, "boards-$n missing")
    }

    @Test
    fun loraOtaCandidatesOfTheRunningBoard() {
        val all = parse()
        val env = "Xiao_nrf52_solar_lite_repeater"
        val c = all.filter { it.hasLoraOta && it.env == env }
        println(c.map { it.release })
        assertTrue(c.map { it.release }.containsAll(listOf("boards-14", "boards-15", "boards-17")))
        val flash = all.filter { it.hasDfu && it.dfuIsZip && BoardCheck.role(it.env) == Role.REPEATER }
        assertTrue(flash.map { it.release }.containsAll((10..17).map { "boards-$it" }))
    }

    @Test
    fun rowsOfOneBranchDiffer() {
        val zone = java.time.ZoneId.of("UTC")
        val rows = parse().filter { it.hasLoraOta && it.env == "Xiao_nrf52_solar_lite_repeater" }.map { it.rowLabel(zone) }
        println(rows)
        assertEquals(3, rows.toSet().size)
        assertEquals("boards-17 \u00b7 934d2da \u00b7 19.11 20:32 \u00b7 lite", rows.first())
    }

    @Test
    fun tokenOnlyForTheApi() {
        assertEquals("Bearer t", Releases.authFor("https://api.github.com/repos/n3tk0/MeshCore/releases?page=1", "t"))
        assertNull(Releases.authFor("https://github.com/n3tk0/MeshCore/releases/download/boards-17/ota-manifest.json", "t"))
        assertNull(Releases.authFor("https://release-assets.githubusercontent.com/x", "t"))
        assertNull(Releases.authFor("https://api.github.com.evil.example/x", "t"))
        assertNull(Releases.authFor("http://api.github.com/x", "t"))
        assertNull(Releases.authFor("https://api.github.com/x", null))
    }

    @Test
    fun rateLimitIsRecognised() {
        assertTrue(HttpError(403, "u").rateLimited)
        assertTrue(HttpError(429, "u").rateLimited)
        assertTrue(!HttpError(500, "u").rateLimited)
    }
}
