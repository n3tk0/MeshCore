package io.github.n3tk0.loraota.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.VerticalDivider
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import io.github.n3tk0.loraota.R
import io.github.n3tk0.loraota.ota.LinkFollow
import io.github.n3tk0.loraota.ota.LinkQuality
import io.github.n3tk0.loraota.ota.LinkSample
import io.github.n3tk0.loraota.tools.SnrClass

/** Green, amber, red as on the network map (see NetworkMapScreen); grey when the SNR is not known. */
@Composable
private fun levelColor(c: SnrClass): Color = when (c) {
    SnrClass.GOOD -> Color(0xFF2E9E44)
    SnrClass.FAIR -> Color(0xFFF2A100)
    SnrClass.BAD -> Color(0xFFD32F2F)
    SnrClass.UNKNOWN -> MaterialTheme.colorScheme.outline
}

/** Phone-style signal bars: [LinkQuality.bars] of 4 filled in the colour of the level. */
@Composable
internal fun SignalBars(snr: Double?, modifier: Modifier = Modifier) {
    val filled = LinkQuality.bars(snr)
    val on = levelColor(LinkQuality.level(snr))
    val off = MaterialTheme.colorScheme.outlineVariant
    Row(modifier, horizontalArrangement = Arrangement.spacedBy(2.dp), verticalAlignment = Alignment.Bottom) {
        for (i in 1..LinkQuality.MAX_BARS) {
            Box(
                Modifier.width(5.dp).height((6 + i * 4).dp)
                    .background(if (i <= filled) on else off, RoundedCornerShape(1.dp)),
            )
        }
    }
}

@Composable
private fun LinkSide(label: String, labelColor: Color, snr: Double?, rssi: Int?, note: String?, modifier: Modifier = Modifier) {
    Column(modifier, verticalArrangement = Arrangement.spacedBy(2.dp)) {
        Text(label, style = MaterialTheme.typography.labelMedium, fontWeight = FontWeight.Bold, color = labelColor)
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            SignalBars(snr)
            Column {
                Text(
                    if (snr == null) stringResource(R.string.link_unknown) else stringResource(R.string.link_snr, snr),
                    style = MaterialTheme.typography.titleSmall, fontWeight = FontWeight.Bold,
                    color = levelColor(LinkQuality.level(snr)),
                )
                if (rssi != null) {
                    Text(stringResource(R.string.link_rssi, rssi), style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
                if (note != null) {
                    Text(note, style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
            }
        }
    }
}

/**
 * The link both ways, above every tab of a repeater: left what the repeater hears of us, right what we hear of it
 * (a trace, see [io.github.n3tk0.loraota.ota.LinkTrace]). With hops on the route there is no trace, only what the
 * status says of the last hop.
 */
@Composable
internal fun LinkCard(vm: AppViewModel) {
    val hops = vm.routeHops
    val direct = hops == 0
    val s = vm.status
    val outColor = MaterialTheme.colorScheme.primary
    val inColor = Color(0xFF8E5CC9)   // apart from the outgoing colour and from the green / amber / red of the levels
    // the status answers the request we sent: its SNR is the same direction as the trace's first byte
    val out = if (direct) vm.linkOut ?: s?.lastSnr else s?.lastSnr
    val inn = if (direct) vm.linkIn else null
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(horizontal = 12.dp, vertical = 8.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.Top) {
                LinkSide(
                    stringResource(R.string.link_out), outColor, out, s?.lastRssi,
                    if (direct) null else stringResource(R.string.link_last_hop), Modifier.weight(1f),
                )
                VerticalDivider(Modifier.height(52.dp).padding(horizontal = 8.dp))
                LinkSide(stringResource(R.string.link_in), inColor, inn, null, null, Modifier.weight(1f))
            }
            val state = when {
                !direct && hops == null -> stringResource(R.string.link_not_direct_unknown)
                !direct -> pluralStringResource(R.plurals.link_not_direct, hops!!, hops)
                vm.linkState == LinkState.MEASURING -> stringResource(R.string.link_state_measuring)
                vm.linkState == LinkState.NO_REPLY -> stringResource(R.string.link_state_no_reply)
                vm.linkState == LinkState.FAILED -> stringResource(R.string.link_state_failed)
                vm.linkState == LinkState.NO_BUDGET -> stringResource(R.string.link_state_budget)
                vm.linkState == LinkState.OK && vm.linkTime > 0 -> stringResource(
                    R.string.link_state_ok, java.text.SimpleDateFormat("HH:mm:ss", java.util.Locale.ROOT).format(vm.linkTime),
                )
                else -> stringResource(R.string.link_state_none)
            }
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Text(state, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.weight(1f))
                if (direct) {
                    TextButton(onClick = { vm.measureLinkNow() }, enabled = !vm.busy && !vm.following && !vm.reconnecting) {
                        Text(stringResource(R.string.link_measure))
                    }
                    TextButton(onClick = { vm.toggleFollow() }, enabled = vm.following || (!vm.busy && !vm.reconnecting)) {
                        Text(if (vm.following) stringResource(R.string.link_follow_stop, vm.followLeft * (LinkFollow.INTERVAL_MS / 1000).toInt())
                            else stringResource(R.string.link_follow))
                    }
                }
            }
            if (direct && vm.linkSamples.size >= 2) {
                LinkGraph(vm.linkSamples, outColor, inColor)
            }
        }
    }
}

/** Both directions over the last traces, with min / avg / max under it. */
@Composable
private fun LinkGraph(samples: List<LinkSample>, outColor: Color, inColor: Color) {
    val guide = MaterialTheme.colorScheme.outlineVariant
    Canvas(Modifier.fillMaxWidth().height(48.dp)) {
        val lo = -10.0
        val hi = 12.0
        fun y(v: Double) = (size.height * (1 - (v.coerceIn(lo, hi) - lo) / (hi - lo))).toFloat()
        val dash = PathEffect.dashPathEffect(floatArrayOf(6f, 6f))
        for (g in listOf(0.0, 5.0)) {
            drawLine(guide, Offset(0f, y(g)), Offset(size.width, y(g)), strokeWidth = 1f, pathEffect = dash)
        }
        val step = if (samples.size > 1) size.width / (samples.size - 1) else 0f
        fun line(color: Color, pick: (LinkSample) -> Double?) {
            val p = Path()
            var open = false
            for ((i, smp) in samples.withIndex()) {
                val v = pick(smp)
                if (v == null) { open = false; continue }
                if (open) p.lineTo(i * step, y(v)) else p.moveTo(i * step, y(v))
                open = true
                drawCircle(color, 2.5f, Offset(i * step, y(v)))
            }
            drawPath(p, color, style = Stroke(width = 3f))
        }
        line(outColor) { it.out }
        line(inColor) { it.inn }
    }
    val o = LinkFollow.stat(samples.mapNotNull { it.out })
    val n = LinkFollow.stat(samples.mapNotNull { it.inn })
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        for ((st, arrow, color) in listOf(Triple(o, "→", outColor), Triple(n, "←", inColor))) {
            Text(
                if (st == null) "$arrow ${stringResource(R.string.link_unknown)}"
                else stringResource(R.string.link_stat_triple, arrow, st.min, st.avg, st.max),
                style = MaterialTheme.typography.labelSmall, color = color, modifier = Modifier.weight(1f),
            )
        }
    }
}
