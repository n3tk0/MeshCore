package io.github.n3tk0.loraota.ui

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.foundation.Canvas
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.KeyboardArrowDown
import androidx.compose.material.icons.filled.KeyboardArrowUp
import androidx.compose.material3.Icon
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Card
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.VerticalDivider
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import io.github.n3tk0.loraota.R
import io.github.n3tk0.loraota.ota.LinkFollow
import io.github.n3tk0.loraota.ota.LinkQuality
import io.github.n3tk0.loraota.ota.LinkSample
import io.github.n3tk0.loraota.ota.BarTier
import io.github.n3tk0.loraota.ota.Asymmetry

internal val OUT_COLOR = Color(0xFF2E9E44)
internal val IN_COLOR = Color(0xFF8E5CC9)

/** Red for 1 bar, orange for 2-3, the side's own colour for 4-5; grey outline when nothing is known. */
@Composable
internal fun barColor(bars: Int, own: Color): Color = when (LinkQuality.tier(bars)) {
    BarTier.RED -> Color(0xFFD32F2F)
    BarTier.ORANGE -> Color(0xFFF57C00)
    BarTier.OWN -> own
    BarTier.NONE -> MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.4f)
}

/** Phone-style signal bars: [LinkQuality.bars] of 5 filled, coloured by the count. */
@Composable
internal fun SignalBars(snr: Double?, own: Color, modifier: Modifier = Modifier, barWidth: Dp = 5.dp, unit: Dp = 4.dp) {
    val filled = LinkQuality.bars(snr)
    val on = barColor(filled, own)
    val off = MaterialTheme.colorScheme.outlineVariant
    Row(modifier, horizontalArrangement = Arrangement.spacedBy(2.dp), verticalAlignment = Alignment.Bottom) {
        for (i in 1..LinkQuality.MAX_BARS) {
            Box(
                Modifier.width(barWidth).height(unit * i + unit / 2)
                    .background(if (i <= filled) on else off, RoundedCornerShape(1.dp)),
            )
        }
    }
}

/** What the bars show: the trace, else (direct, nothing traced yet) what the status says of the last hop. */
private fun shownOut(vm: AppViewModel): Double? =
    vm.linkOut ?: if (vm.routeHops == 0) vm.status?.lastSnr?.takeIf { vm.linkTime == 0L } else null

/**
 * The link in the top bar of the tabs other than the overview, next to the repeater's name: "→", the outgoing bars and
 * SNR on the left, the incoming bars, SNR and "←" on the right (with hops: the weakest of the route and its length). The
 * numbers go first when there is little room ([showNumbers] false). A tap measures again; a small spinner shows it.
 */
@Composable
internal fun LinkTitleIndicator(vm: AppViewModel, showNumbers: Boolean, modifier: Modifier = Modifier) {
    val out = shownOut(vm)
    val inn = vm.linkIn
    val measuring = vm.linkState == LinkState.MEASURING
    val hops = vm.routeHops?.takeIf { it > 0 && vm.linkRoute != null }
    val enabled = vm.routeHops != null && !vm.busy && !vm.following && !vm.reconnecting
    val desc = stringResource(
        R.string.link_strip_desc,
        stringResource(R.string.link_bars_desc, LinkQuality.bars(out), LinkQuality.MAX_BARS),
        stringResource(R.string.link_bars_desc, LinkQuality.bars(inn), LinkQuality.MAX_BARS),
    ) + (if (hops != null) ", " + pluralStringResource(R.plurals.link_hops_short, hops, hops) else "") +
        ". " + stringResource(R.string.link_tap_measure)
    Row(
        modifier.clip(RoundedCornerShape(8.dp))
            .clickable(enabled = enabled, role = Role.Button) { vm.measureLinkNow() }
            .semantics(mergeDescendants = true) { contentDescription = desc }
            .padding(horizontal = 4.dp, vertical = 2.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(3.dp),
    ) {
        val numStyle = MaterialTheme.typography.labelSmall
        Text("→", style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.Bold, color = OUT_COLOR)
        SignalBars(out, OUT_COLOR, barWidth = 3.dp, unit = 2.5.dp)
        if (showNumbers && out != null) {
            Text(LinkQuality.shortSnr(out), style = numStyle, maxLines = 1, color = barColor(LinkQuality.bars(out), OUT_COLOR))
        }
        Box(Modifier.width(if (measuring) 14.dp else 4.dp), contentAlignment = Alignment.Center) {
            if (measuring) CircularProgressIndicator(Modifier.size(12.dp), strokeWidth = 1.5.dp)
        }
        if (showNumbers && hops != null) {
            Text(pluralStringResource(R.plurals.link_hops_short, hops, hops), style = numStyle, maxLines = 1,
                color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        SignalBars(inn, IN_COLOR, barWidth = 3.dp, unit = 2.5.dp)
        if (showNumbers && inn != null) {
            Text(LinkQuality.shortSnr(inn), style = numStyle, maxLines = 1, color = barColor(LinkQuality.bars(inn), IN_COLOR))
        }
        Text("←", style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.Bold, color = IN_COLOR)
    }
}

@Composable
private fun LinkSide(label: String, labelColor: Color, snr: Double?, rssi: Int?, note: String?, modifier: Modifier = Modifier) {
    Column(modifier, verticalArrangement = Arrangement.spacedBy(2.dp)) {
        Text(label, style = MaterialTheme.typography.labelMedium, fontWeight = FontWeight.Bold, color = labelColor)
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            SignalBars(snr, labelColor)
            Column {
                Text(
                    if (snr == null) stringResource(R.string.link_unknown) else stringResource(R.string.link_snr, snr),
                    style = MaterialTheme.typography.titleSmall, fontWeight = FontWeight.Bold,
                    color = barColor(LinkQuality.bars(snr), labelColor),
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

/** One link of a traced route: "from → to" with what the far end heard going out and what came back. */
@Composable
private fun HopRow(from: String, to: String, out: Double, inn: Double) {
    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        Text("$from → $to", style = MaterialTheme.typography.labelMedium, maxLines = 1, overflow = TextOverflow.Ellipsis,
            modifier = Modifier.weight(1f))
        Text("→ ${LinkQuality.shortSnr(out)}", style = MaterialTheme.typography.labelMedium, fontWeight = FontWeight.Bold,
            color = barColor(LinkQuality.bars(out), OUT_COLOR))
        Text("← ${LinkQuality.shortSnr(inn)}", style = MaterialTheme.typography.labelMedium, fontWeight = FontWeight.Bold,
            color = barColor(LinkQuality.bars(inn), IN_COLOR))
    }
}

@Composable
private fun asymmetryText(out: Double?, inn: Double?): String? = when (LinkQuality.asymmetry(out, inn)) {
    Asymmetry.NONE -> null
    Asymmetry.PHONE_WEAK -> stringResource(R.string.link_asym_phone)
    Asymmetry.REPEATER_WEAK -> stringResource(R.string.link_asym_repeater)
}

/**
 * The link both ways, on the overview of a repeater: left what the repeater hears of us, right what we hear of it
 * (a trace, see [io.github.n3tk0.loraota.ota.LinkTrace]). With hops the trace goes along the learned route and back: the
 * bars show the weakest link each way and the links are listed; 'Check direct' tries a one-hop trace to the repeater.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun LinkCard(vm: AppViewModel) {
    val hops = vm.routeHops
    val direct = hops == 0
    val s = vm.status
    val routeView = vm.linkRoute
    // the status answers the request we sent: its SNR is the same direction as the trace's first byte
    val out = vm.linkOut ?: if (direct) s?.lastSnr?.takeIf { vm.linkTime == 0L } else s?.lastSnr
    val inn = vm.linkIn
    val noTrace = hops == null || vm.linkState == LinkState.NOT_DIRECT
    val expanded = vm.linkCardExpanded
    val toggleDesc = stringResource(if (expanded) R.string.link_hide_details else R.string.link_show_details)
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(horizontal = 12.dp, vertical = 8.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Row(
                Modifier.fillMaxWidth().clickable(role = Role.Button, onClickLabel = toggleDesc) { vm.linkCardExpanded = !expanded },
                verticalAlignment = Alignment.Top,
            ) {
                val lastHop = if (direct) null else if (routeView != null && vm.linkOut != null) {
                    pluralStringResource(R.plurals.link_weakest, hops!!, hops)
                } else stringResource(R.string.link_last_hop)
                LinkSide(stringResource(R.string.link_out), OUT_COLOR, out, if (routeView == null) s?.lastRssi else null, lastHop, Modifier.weight(1f))
                VerticalDivider(Modifier.height(52.dp).padding(horizontal = 8.dp))
                LinkSide(stringResource(R.string.link_in), IN_COLOR, inn, null, null, Modifier.weight(1f))
                Column(Modifier.padding(start = 4.dp), horizontalAlignment = Alignment.CenterHorizontally) {
                    Box(Modifier.size(16.dp), contentAlignment = Alignment.Center) {
                        if (vm.linkState == LinkState.MEASURING) CircularProgressIndicator(Modifier.size(14.dp), strokeWidth = 1.5.dp)
                    }
                    Icon(
                        if (expanded) Icons.Default.KeyboardArrowUp else Icons.Default.KeyboardArrowDown,
                        contentDescription = toggleDesc,
                        tint = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            }
            AnimatedVisibility(expanded) {
              Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
            if (direct) asymmetryText(vm.linkOut, vm.linkIn)?.let {
                Text(it, style = MaterialTheme.typography.labelSmall, color = Color(0xFFF57C00))
            }
            if (routeView != null) {
                val names = listOf(stringResource(R.string.link_you)) + routeView.names
                Column(Modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(1.dp)) {
                    for ((i, h) in routeView.route.hops.withIndex()) HopRow(names[i], names[i + 1], h.out, h.inn)
                }
            }
            val state = when {
                hops == null -> stringResource(R.string.link_not_direct_unknown)
                vm.linkState == LinkState.NOT_DIRECT -> pluralStringResource(R.plurals.link_not_direct, hops, hops)
                vm.linkState == LinkState.MEASURING -> stringResource(R.string.link_state_measuring)
                vm.linkState == LinkState.NO_REPLY -> stringResource(R.string.link_state_no_reply)
                vm.linkState == LinkState.FAILED -> stringResource(R.string.link_state_failed)
                vm.linkState == LinkState.NO_BUDGET -> stringResource(R.string.link_state_budget)
                vm.linkState == LinkState.OK && vm.linkTime > 0 -> stringResource(
                    R.string.link_state_ok, java.text.SimpleDateFormat("HH:mm:ss", java.util.Locale.ROOT).format(vm.linkTime),
                )
                else -> stringResource(R.string.link_state_none)
            }
            val canAct = !vm.busy && !vm.following && !vm.reconnecting
            val measureButtons: @Composable () -> Unit = {
                TextButton(onClick = { vm.measureLinkNow() }, enabled = canAct) {
                    Text(stringResource(R.string.link_measure))
                }
                TextButton(onClick = { vm.toggleFollow() }, enabled = vm.following || (!vm.busy && !vm.reconnecting)) {
                    Text(if (vm.following) stringResource(R.string.link_follow_stop, vm.followLeft * (LinkFollow.INTERVAL_MS / 1000).toInt())
                        else stringResource(R.string.link_follow))
                }
            }
            if (direct) {
                Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                    Text(state, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant,
                        modifier = Modifier.weight(1f))
                    measureButtons()
                }
            } else {
                Text(state, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                FlowRow(Modifier.fillMaxWidth(), verticalArrangement = Arrangement.Center) {
                    if (!noTrace) measureButtons()
                    TextButton(onClick = { vm.checkDirectNow() }, enabled = canAct) {
                        Text(stringResource(R.string.link_check_direct))
                    }
                }
                val ds = vm.directState
                val line = when (ds) {
                    LinkState.MEASURING -> stringResource(R.string.link_state_measuring)
                    LinkState.OK -> if (hops != null) {
                        pluralStringResource(R.plurals.link_direct_ok, hops, hops)
                    } else stringResource(R.string.link_direct_ok_flood)
                    LinkState.NO_REPLY -> stringResource(R.string.link_direct_none)
                    LinkState.NO_BUDGET -> stringResource(R.string.link_state_budget)
                    LinkState.FAILED -> stringResource(R.string.link_state_failed)
                    else -> null
                }
                if (line != null) {
                    Text(line, style = MaterialTheme.typography.bodySmall, fontWeight = FontWeight.Bold)
                    if (ds == LinkState.OK) {
                        HopRow(stringResource(R.string.link_you), stringResource(R.string.link_repeater), vm.directOut ?: 0.0, vm.directIn ?: 0.0)
                        asymmetryText(vm.directOut, vm.directIn)?.let {
                            Text(it, style = MaterialTheme.typography.labelSmall, color = Color(0xFFF57C00))
                        }
                    }
                }
            }
            if (vm.linkSamples.size >= 2) {
                LinkGraph(vm.linkSamples, OUT_COLOR, IN_COLOR)
            }
              }
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
