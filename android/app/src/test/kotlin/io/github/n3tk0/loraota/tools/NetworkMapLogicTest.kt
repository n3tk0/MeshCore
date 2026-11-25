package io.github.n3tk0.loraota.tools

import io.github.n3tk0.loraota.backup.Backup
import io.github.n3tk0.loraota.backup.BackupItem
import io.github.n3tk0.loraota.backup.DeviceKind
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

class NetworkMapLogicTest {
    private fun rep(key: String, name: String, lat: String, lon: String, time: Long = 1) = Backup(
        DeviceKind.REPEATER, key, name, time, false, "v1",
        listOf(
            BackupItem(BackupItem.GROUP_SETTING, "lat", "lat", lat),
            BackupItem(BackupItem.GROUP_SETTING, "lon", "lon", lon),
        ),
    )

    private val keyA = "aaaaaaaa11112222"
    private val keyB = "bbbbbbbb11112222"
    private val keyC = "cccccccc11112222"

    @Test
    fun snrClasses() {
        assertEquals(SnrClass.GOOD, NetworkMapLogic.snrClass(8f, 5f))
        assertEquals(SnrClass.FAIR, NetworkMapLogic.snrClass(8f, 4.9f))
        assertEquals(SnrClass.FAIR, NetworkMapLogic.snrClass(0f, null))
        assertEquals(SnrClass.BAD, NetworkMapLogic.snrClass(9f, -0.1f))
        assertEquals(SnrClass.UNKNOWN, NetworkMapLogic.snrClass(null, null))
    }

    @Test
    fun skipsZeroCoordsAndBuildsLinks() {
        val backups = listOf(
            rep(keyA, "Alpha", "42.70", "23.30"),
            rep(keyB, "Beta", "42.80", "23.40"),
            rep(keyC, "Gamma", "0", "0"),
        )
        val observed = mapOf(
            keyA to listOf(NeighborEntry("bbbbbbbb", 10, 9f), NeighborEntry("cccccccc", 10, 3f)),
            keyB to listOf(NeighborEntry("aaaaaaaa", 10, 1f)),
        )
        val names = mapOf(keyA to "Alpha", keyB to "Beta", keyC to "Gamma")
        val d = NetworkMapLogic.build(backups, observed, names, mapOf(keyA to "85%"))
        assertEquals(setOf("Alpha", "Beta"), d.nodes.map { it.name }.toSet())
        assertEquals("85%", d.nodes.first { it.name == "Alpha" }.snippet)
        assertEquals(1, d.links.size)
        val l = d.links[0]
        assertEquals(SnrClass.FAIR, l.snrClass)
        assertTrue(l.asymmetric)
        assertNotNull(l.distanceKm)
        assertNotNull(l.bearingDeg)
        assertNotNull(d.bounds)
    }

    @Test
    fun noNodesNoBounds() {
        val d = NetworkMapLogic.build(emptyList(), emptyMap(), emptyMap())
        assertNull(d.bounds)
        assertTrue(d.links.isEmpty())
    }

    @Test
    fun tileCounts() {
        val b = MapBounds(north = 43.0, south = 42.0, east = 24.0, west = 23.0)
        assertEquals(1L, TileMath.tilesAtZoom(MapBounds(0.0, 0.0, 0.0, 0.0), 0))
        val z10 = TileMath.tilesAtZoom(b, 10)
        assertTrue(z10 in 20..60, "z10=$z10")
        assertEquals(TileMath.tilesAtZoom(b, 10) + TileMath.tilesAtZoom(b, 11), TileMath.tileCount(b, 10, 11))
        assertTrue(TileMath.allowed(2500))
        assertTrue(!TileMath.allowed(2501))
        assertTrue(!TileMath.allowed(0))
    }
}
