package io.github.n3tk0.loraota.tools

import io.github.n3tk0.loraota.backup.Backup
import io.github.n3tk0.loraota.backup.DeviceKind
import java.util.Locale
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.floor
import kotlin.math.ln
import kotlin.math.max
import kotlin.math.min
import kotlin.math.tan

/** Link quality class by the worse of the two SNR directions. */
enum class SnrClass { GOOD, FAIR, BAD, UNKNOWN }

data class MapNode(
    val key: String,
    val name: String,
    val lat: Double,
    val lon: Double,
    /** Short health text (battery) or empty. */
    val snippet: String = "",
)

data class MapLink(
    val a: MapNode,
    val b: MapNode,
    val snrAtoBDb: Float?,
    val snrBtoADb: Float?,
    val distanceKm: Double?,
    val bearingDeg: Int?,
    val cardinal: String?,
    val asymmetric: Boolean,
    val snrClass: SnrClass,
)

data class MapBounds(val north: Double, val south: Double, val east: Double, val west: Double)

data class NetworkMapData(val nodes: List<MapNode>, val links: List<MapLink>) {
    val bounds: MapBounds?
        get() = if (nodes.isEmpty()) null else MapBounds(
            north = nodes.maxOf { it.lat }, south = nodes.minOf { it.lat },
            east = nodes.maxOf { it.lon }, west = nodes.minOf { it.lon },
        )
}

object NetworkMapLogic {
    /** Green >= 5 dB, amber 0..5, red < 0, by the worse known direction. */
    fun snrClass(a: Float?, b: Float?): SnrClass {
        val worst = listOfNotNull(a, b).minOrNull() ?: return SnrClass.UNKNOWN
        return when {
            worst >= 5f -> SnrClass.GOOD
            worst >= 0f -> SnrClass.FAIR
            else -> SnrClass.BAD
        }
    }

    /**
     * Repeaters with known GPS (lat=lon=0 skipped) from the newest backups, plus links from the hearing graph
     * where both ends have a position on the map.
     */
    fun build(
        backups: List<Backup>,
        observed: Map<String, List<NeighborEntry>>,
        knownNames: Map<String, String>,
        snippetByKeyOrName: Map<String, String> = emptyMap(),
    ): NetworkMapData {
        val nodes = ArrayList<MapNode>()
        val seen = HashSet<String>()
        for (b in backups.filter { it.kind == DeviceKind.REPEATER }.sortedByDescending { it.time }) {
            val key = b.deviceKey.lowercase(Locale.ROOT)
            val nameLow = b.deviceName.lowercase(Locale.ROOT)
            val idKey = key.ifEmpty { nameLow }
            if (idKey.isEmpty() || !seen.add(idKey)) continue
            val coordMap = MeshGeoTopology.extractCoordsFromBackups(listOf(b))
            val c = coordMap[idKey] ?: coordMap[nameLow] ?: continue
            if (c.first !in -90.0..90.0 || c.second !in -180.0..180.0) continue
            val name = b.deviceName.ifBlank { knownNames[b.deviceKey] ?: b.deviceKey.take(8).uppercase(Locale.ROOT) }
            val snippet = snippetByKeyOrName[key] ?: snippetByKeyOrName[name.lowercase(Locale.ROOT)] ?: ""
            nodes.add(MapNode(idKey, name, c.first, c.second, snippet))
        }
        fun find(edgeKey: String, edgeName: String): MapNode? =
            nodes.firstOrNull { it.key == edgeKey }
                // a short neighbor prefix (1-byte hash) may fit several repeaters: only a single match counts
                ?: nodes.filter { edgeKey.length >= 2 && (it.key.startsWith(edgeKey) || edgeKey.startsWith(it.key)) }
                    .singleOrNull()
                ?: nodes.firstOrNull { it.name.equals(edgeName, ignoreCase = true) }

        val coords = nodes.associate { it.key to (it.lat to it.lon) }
        val links = ArrayList<MapLink>()
        val used = HashSet<Pair<String, String>>()
        for (e in MeshGeoTopology.buildTopologyEdges(observed, knownNames, coords)) {
            val na = find(e.nodeAKey, e.nodeAName) ?: continue
            val nb = find(e.nodeBKey, e.nodeBName) ?: continue
            if (na.key == nb.key) continue
            val pair = if (na.key < nb.key) na.key to nb.key else nb.key to na.key
            if (!used.add(pair)) continue
            val geo = MeshGeoTopology.distanceAndBearing(na.lat, na.lon, nb.lat, nb.lon)
            links.add(
                MapLink(
                    na, nb, e.snrAtoBDb, e.snrBtoADb, geo?.distanceKm, geo?.bearingDeg, geo?.cardinal,
                    e.isAsymmetric, snrClass(e.snrAtoBDb, e.snrBtoADb),
                ),
            )
        }
        return NetworkMapData(nodes, links)
    }
}

/** Slippy-map tile counting for the offline-area download guard. */
object TileMath {
    const val MAX_TILES = 2500

    private fun x(lon: Double, z: Int): Int =
        floor((lon + 180.0) / 360.0 * (1 shl z)).toInt().coerceIn(0, (1 shl z) - 1)

    private fun y(lat: Double, z: Int): Int {
        val r = Math.toRadians(lat.coerceIn(-85.0511, 85.0511))
        val v = (1.0 - ln(tan(r) + 1.0 / cos(r)) / PI) / 2.0 * (1 shl z)
        return floor(v).toInt().coerceIn(0, (1 shl z) - 1)
    }

    fun tilesAtZoom(b: MapBounds, zoom: Int): Long {
        val w = abs(x(b.east, zoom) - x(b.west, zoom)) + 1L
        val h = abs(y(b.south, zoom) - y(b.north, zoom)) + 1L
        return w * h
    }

    fun tileCount(b: MapBounds, minZoom: Int, maxZoom: Int): Long =
        (min(minZoom, maxZoom)..max(minZoom, maxZoom)).sumOf { tilesAtZoom(b, it) }

    fun allowed(count: Long) = count in 1..MAX_TILES.toLong()
}
