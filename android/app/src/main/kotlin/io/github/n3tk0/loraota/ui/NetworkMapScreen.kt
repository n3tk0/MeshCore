package io.github.n3tk0.loraota.ui

import android.content.Context
import android.graphics.Color as AColor
import android.graphics.DashPathEffect
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Card
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import io.github.n3tk0.loraota.BuildConfig
import io.github.n3tk0.loraota.R
import io.github.n3tk0.loraota.tools.MapBounds
import io.github.n3tk0.loraota.tools.MapLink
import io.github.n3tk0.loraota.tools.NetworkMapData
import io.github.n3tk0.loraota.tools.SnrClass
import io.github.n3tk0.loraota.tools.TileMath
import org.osmdroid.config.Configuration
import org.osmdroid.tileprovider.cachemanager.CacheManager
import org.osmdroid.tileprovider.tilesource.TileSourceFactory
import org.osmdroid.util.BoundingBox
import org.osmdroid.util.GeoPoint
import org.osmdroid.views.MapView
import org.osmdroid.views.overlay.Marker
import org.osmdroid.views.overlay.Polyline
import java.io.File
import java.util.Locale

/** osmdroid keeps its tiles inside the app's own storage (no storage permission); the user agent carries no personal data. */
internal fun initOsmdroid(ctx: Context) {
    val cfg = Configuration.getInstance()
    cfg.userAgentValue = "MeshCoreService/${BuildConfig.VERSION_NAME}"
    val base = File(ctx.filesDir, "osmdroid").apply { mkdirs() }
    cfg.osmdroidBasePath = base
    cfg.osmdroidTileCache = File(base, "tiles").apply { mkdirs() }
}

private fun linkColor(c: SnrClass): Int = when (c) {
    SnrClass.GOOD -> AColor.rgb(0x2E, 0x9E, 0x44)
    SnrClass.FAIR -> AColor.rgb(0xF2, 0xA1, 0x00)
    SnrClass.BAD -> AColor.rgb(0xD3, 0x2F, 0x2F)
    SnrClass.UNKNOWN -> AColor.GRAY
}

private fun fmtSnr(v: Float?) = v?.let { String.format(Locale.ROOT, "%+.1f dB", it) } ?: "—"

private sealed interface DlState {
    data object Idle : DlState
    data class Confirm(val tiles: Long, val minZ: Int, val maxZ: Int, val bb: BoundingBox) : DlState
    data class TooMany(val tiles: Long) : DlState
    data class Running(val progress: Float) : DlState
    data class Done(val ok: Boolean) : DlState
}

@Composable
fun NetworkMapDialog(data: NetworkMapData, onDismiss: () -> Unit) {
    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(usePlatformDefaultWidth = false),
    ) {
        Surface(Modifier.fillMaxSize()) {
            NetworkMapContent(data, onDismiss)
        }
    }
}

@Composable
private fun NetworkMapContent(data: NetworkMapData, onBack: () -> Unit) {
    val ctx = LocalContext.current
    val lifecycleOwner = LocalLifecycleOwner.current
    var selected by remember { mutableStateOf<MapLink?>(null) }
    var dl by remember { mutableStateOf<DlState>(DlState.Idle) }
    var task by remember { mutableStateOf<CacheManager.CacheManagerTask?>(null) }

    val mapView = remember {
        initOsmdroid(ctx)
        MapView(ctx).apply {
            setTileSource(TileSourceFactory.MAPNIK)
            setMultiTouchControls(true)
            minZoomLevel = 3.0
            maxZoomLevel = 19.0
            zoomController.setVisibility(org.osmdroid.views.CustomZoomButtonsController.Visibility.SHOW_AND_FADEOUT)
        }
    }

    // markers and links follow the data: the Offline hub may still be loading when the map opens
    var fitted by remember { mutableStateOf(false) }
    LaunchedEffect(data) {
        mapView.overlays.clear()
        selected = null
        for (l in data.links) {
            val line = Polyline(mapView).apply {
                setPoints(listOf(GeoPoint(l.a.lat, l.a.lon), GeoPoint(l.b.lat, l.b.lon)))
                outlinePaint.color = linkColor(l.snrClass)
                outlinePaint.strokeWidth = if (l.asymmetric) 11f else 8f
                if (l.asymmetric) outlinePaint.pathEffect = DashPathEffect(floatArrayOf(28f, 16f), 0f)
                setOnClickListener { _, _, _ -> selected = l; true }
            }
            mapView.overlays.add(line)
        }
        for (n in data.nodes) {
            mapView.overlays.add(
                Marker(mapView).apply {
                    position = GeoPoint(n.lat, n.lon)
                    title = n.name
                    if (n.snippet.isNotEmpty()) snippet = n.snippet
                    setAnchor(Marker.ANCHOR_CENTER, Marker.ANCHOR_BOTTOM)
                },
            )
        }
        mapView.invalidate()
        if (!fitted && data.bounds != null) {
            fitted = true
            fit(mapView, data)
        }
    }

    fun fit() = fit(mapView, data)

    DisposableEffect(lifecycleOwner) {
        val obs = LifecycleEventObserver { _, e ->
            when (e) {
                Lifecycle.Event.ON_RESUME -> mapView.onResume()
                Lifecycle.Event.ON_PAUSE -> mapView.onPause()
                else -> {}
            }
        }
        lifecycleOwner.lifecycle.addObserver(obs)
        mapView.onResume()
        onDispose {
            lifecycleOwner.lifecycle.removeObserver(obs)
            task?.cancel(true)
            mapView.onPause()
            mapView.onDetach()
        }
    }

    Column(Modifier.fillMaxSize().statusBarsPadding()) {
        Row(
            Modifier.fillMaxWidth().padding(horizontal = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            TextButton(onClick = onBack) { Text(stringResource(R.string.map_back)) }
            Text(
                stringResource(R.string.map_title),
                style = MaterialTheme.typography.titleMedium,
                modifier = Modifier.weight(1f),
            )
            TextButton(onClick = { fit() }) { Text(stringResource(R.string.map_fit)) }
            TextButton(
                enabled = dl !is DlState.Running,
                onClick = {
                    val bb = mapView.boundingBox
                    val minZ = mapView.zoomLevelDouble.toInt().coerceIn(3, 19)
                    val maxZ = (minZ + 3).coerceAtMost(19)
                    val n = TileMath.tileCount(MapBounds(bb.latNorth, bb.latSouth, bb.lonEast, bb.lonWest), minZ, maxZ)
                    dl = if (TileMath.allowed(n)) DlState.Confirm(n, minZ, maxZ, bb) else DlState.TooMany(n)
                },
            ) { Text(stringResource(R.string.map_save_offline)) }
        }
        if (data.nodes.isEmpty()) {
            Text(
                stringResource(R.string.map_no_nodes),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
            )
        }
        (dl as? DlState.Running)?.let {
            LinearProgressIndicator(progress = { it.progress }, modifier = Modifier.fillMaxWidth())
        }
        Box(Modifier.weight(1f).fillMaxWidth()) {
            AndroidView(factory = { mapView }, modifier = Modifier.fillMaxSize())
            Text(
                "© OpenStreetMap contributors",
                style = MaterialTheme.typography.labelSmall,
                color = Color.Black,
                modifier = Modifier
                    .align(Alignment.BottomStart)
                    .background(Color.White.copy(alpha = 0.75f))
                    .padding(horizontal = 4.dp, vertical = 1.dp),
            )
            selected?.let { l ->
                Card(Modifier.align(Alignment.BottomCenter).padding(8.dp).padding(bottom = 18.dp).fillMaxWidth()) {
                    Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Text("${l.a.name} ↔ ${l.b.name}", style = MaterialTheme.typography.titleSmall, modifier = Modifier.weight(1f))
                            TextButton(onClick = { selected = null }) { Text(stringResource(R.string.map_close)) }
                        }
                        Text("${l.a.name}→${l.b.name}: ${fmtSnr(l.snrAtoBDb)}", style = MaterialTheme.typography.bodySmall)
                        Text("${l.b.name}→${l.a.name}: ${fmtSnr(l.snrBtoADb)}", style = MaterialTheme.typography.bodySmall)
                        l.distanceKm?.let { km ->
                            Text(
                                String.format(Locale.ROOT, "%.2f km, %d° %s", km, l.bearingDeg ?: 0, l.cardinal ?: ""),
                                style = MaterialTheme.typography.bodySmall,
                            )
                        }
                        if (l.asymmetric) {
                            Text(
                                stringResource(R.string.audit_topology_asym),
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.error,
                            )
                        }
                    }
                }
            }
        }
    }

    when (val s = dl) {
        is DlState.Confirm -> AlertDialog(
            onDismissRequest = { dl = DlState.Idle },
            title = { Text(stringResource(R.string.map_save_offline)) },
            text = { Text(stringResource(R.string.map_save_confirm, s.tiles, s.minZ, s.maxZ)) },
            confirmButton = {
                TextButton(onClick = {
                    dl = DlState.Running(0f)
                    task = CacheManager(mapView).downloadAreaAsync(
                        ctx, s.bb, s.minZ, s.maxZ,
                        object : CacheManager.CacheManagerCallback {
                            override fun onTaskComplete() { dl = DlState.Done(true) }
                            override fun onTaskFailed(errors: Int) { dl = DlState.Done(false) }
                            override fun updateProgress(progress: Int, currentZoomLevel: Int, zoomMin: Int, zoomMax: Int) {
                                dl = DlState.Running((progress / 100f).coerceIn(0f, 1f))
                            }
                            override fun downloadStarted() {}
                            override fun setPossibleTilesInArea(total: Int) {}
                        },
                    )
                }) { Text(stringResource(R.string.map_download)) }
            },
            dismissButton = { TextButton(onClick = { dl = DlState.Idle }) { Text(stringResource(R.string.map_cancel)) } },
        )
        is DlState.TooMany -> AlertDialog(
            onDismissRequest = { dl = DlState.Idle },
            title = { Text(stringResource(R.string.map_save_offline)) },
            text = { Text(stringResource(R.string.map_save_too_many, s.tiles, TileMath.MAX_TILES)) },
            confirmButton = { TextButton(onClick = { dl = DlState.Idle }) { Text(stringResource(R.string.map_close)) } },
        )
        is DlState.Running -> AlertDialog(
            onDismissRequest = {},
            title = { Text(stringResource(R.string.map_saving)) },
            text = { LinearProgressIndicator(progress = { s.progress }, modifier = Modifier.fillMaxWidth()) },
            confirmButton = {
                TextButton(onClick = { task?.cancel(true); task = null; dl = DlState.Idle }) {
                    Text(stringResource(R.string.map_cancel))
                }
            },
        )
        is DlState.Done -> AlertDialog(
            onDismissRequest = { dl = DlState.Idle },
            text = { Text(stringResource(if (s.ok) R.string.map_save_done else R.string.map_save_failed)) },
            confirmButton = { TextButton(onClick = { dl = DlState.Idle }) { Text(stringResource(R.string.map_close)) } },
        )
        DlState.Idle -> {}
    }
}

/** Zooms [mapView] to all repeaters of [data] (a single one: zoom 14 around it). */
private fun fit(mapView: MapView, data: NetworkMapData) {
    val b = data.bounds ?: return
    mapView.post {
        if (b.north - b.south < 0.002 && b.east - b.west < 0.002) {
            mapView.controller.setZoom(14.0)
            mapView.controller.setCenter(GeoPoint((b.north + b.south) / 2, (b.east + b.west) / 2))
        } else {
            mapView.zoomToBoundingBox(BoundingBox(b.north, b.east, b.south, b.west), false, 96)
        }
    }
}
