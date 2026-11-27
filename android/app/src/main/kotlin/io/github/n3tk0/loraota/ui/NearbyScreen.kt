package io.github.n3tk0.loraota.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Lock
import androidx.compose.material.icons.filled.Search
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import io.github.n3tk0.loraota.R
import io.github.n3tk0.loraota.ota.LinkQuality
import io.github.n3tk0.loraota.ota.NodeDiscover
import io.github.n3tk0.loraota.ota.hex

/**
 * Zero-hop nearby repeater discovery screen (`CMD_SEND_CONTROL_DATA` / `DISCOVER_REQ`).
 * Shows responding repeaters in direct radio range with both-direction SNR and RSSI,
 * lets the user log in to known ones or add unknown ones directly to the companion's contacts.
 */
@OptIn(ExperimentalMaterial3Api::class, ExperimentalLayoutApi::class)
@Composable
fun NearbyScreen(vm: AppViewModel) {
    Hint(stringResource(R.string.nearby_hint_short), stringResource(R.string.nearby_hint))

    val totalSec = (NodeDiscover.LISTEN_MS / 1000L).toFloat().coerceAtLeast(1f)
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        if (vm.nearbyListening) {
            OutlinedButton(onClick = { vm.stop() }) {
                Text(stringResource(R.string.nearby_stop, vm.nearbySecondsLeft))
            }
        } else if (vm.nearbyNaming) {
            OutlinedButton(onClick = { vm.stop() }) {
                Text(stringResource(R.string.nearby_stop_naming))
            }
        } else {
            Button(onClick = { vm.searchNearby() }, enabled = !vm.busy) {
                Icon(Icons.Default.Search, null, modifier = Modifier.size(18.dp))
                Spacer(Modifier.width(6.dp))
                Text(stringResource(R.string.nearby_search))
            }
        }
    }

    if (vm.nearbyListening) {
        val elapsed = (totalSec - vm.nearbySecondsLeft).coerceIn(0f, totalSec)
        LinearProgressIndicator(
            progress = { elapsed / totalSec },
            modifier = Modifier.fillMaxWidth(),
        )
    }

    if (vm.nearbyError.isNotEmpty() && !vm.nearbyListening) {
        InfoCard(vm.nearbyError)
    }

    if (vm.nearbyRepeaters.isNotEmpty()) {
        Text(
            pluralStringResource(R.plurals.nearby_found, vm.nearbyRepeaters.size, vm.nearbyRepeaters.size),
            style = MaterialTheme.typography.titleSmall,
            color = MaterialTheme.colorScheme.primary,
        )
        for (item in vm.nearbyRepeaters) {
            NearbyRepeaterCard(vm = vm, item = item)
        }
    } else if (vm.nearbySearched && !vm.nearbyListening && vm.nearbyError.isEmpty()) {
        InfoCard(stringResource(R.string.nearby_empty))
    }

    vm.loginSheet?.let { c ->
        val sheet = rememberModalBottomSheetState(skipPartiallyExpanded = true)
        ModalBottomSheet(onDismissRequest = { vm.loginSheet = null }, sheetState = sheet) {
            LoginSheet(vm, c)
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun NearbyRepeaterCard(vm: AppViewModel, item: NodeDiscover.DiscoveredRepeater) {
    val matchingContact = item.contactKeyHex?.let { key ->
        vm.repeaters.firstOrNull { hex(it.publicKey) == key }
    }
    val hasSavedPw = matchingContact != null && vm.hasSavedPassword(matchingContact)
    val canOpen = item.inContacts && !vm.busy && !vm.nearbyListening

    Card(
        modifier = Modifier
            .fillMaxWidth()
            .then(if (canOpen) Modifier.clickable { vm.openNearbyRepeater(item) } else Modifier),
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 12.dp, vertical = 10.dp),
            verticalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                    Text(
                        item.displayName,
                        style = MaterialTheme.typography.titleMedium,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                    item.owner?.let { owner ->
                        Text(
                            owner,
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                        )
                    }
                    if (item.displayName != item.shortId) {
                        Text(
                            item.shortId,
                            style = MaterialTheme.typography.labelSmall,
                            fontFamily = FontFamily.Monospace,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                }
                if (item.inContacts && hasSavedPw) {
                    IconButton(
                        onClick = { vm.editNearbyRepeaterPassword(item) },
                        enabled = !vm.busy && !vm.nearbyListening,
                    ) {
                        Icon(
                            Icons.Default.Lock,
                            stringResource(R.string.change_password),
                            modifier = Modifier.size(18.dp),
                            tint = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                }
                if (!item.inContacts && item.hasFullKey) {
                    OutlinedButton(
                        onClick = { vm.addNearbyRepeater(item) },
                        enabled = !vm.busy && !vm.nearbyListening,
                    ) {
                        Icon(Icons.Default.Add, null, modifier = Modifier.size(16.dp))
                        Spacer(Modifier.width(4.dp))
                        Text(stringResource(R.string.nearby_add))
                    }
                }
            }

            // Signal quality: outgoing (repeater heard us) and incoming (we heard repeater) + RSSI
            FlowRow(
                horizontalArrangement = Arrangement.spacedBy(12.dp),
                verticalArrangement = Arrangement.spacedBy(4.dp),
            ) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(4.dp),
                ) {
                    Text("→", style = MaterialTheme.typography.labelMedium, fontWeight = FontWeight.Bold, color = OUT_COLOR)
                    SignalBars(item.outSnr, OUT_COLOR, barWidth = 3.dp, unit = 2.5.dp)
                    Text(
                        "${LinkQuality.shortSnr(item.outSnr)} dB",
                        style = MaterialTheme.typography.labelSmall,
                        color = barColor(LinkQuality.bars(item.outSnr), OUT_COLOR),
                    )
                }
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(4.dp),
                ) {
                    Text("←", style = MaterialTheme.typography.labelMedium, fontWeight = FontWeight.Bold, color = IN_COLOR)
                    SignalBars(item.inSnr, IN_COLOR, barWidth = 3.dp, unit = 2.5.dp)
                    Text(
                        "${LinkQuality.shortSnr(item.inSnr)} dB",
                        style = MaterialTheme.typography.labelSmall,
                        color = barColor(LinkQuality.bars(item.inSnr), IN_COLOR),
                    )
                }
                Text(
                    stringResource(R.string.link_rssi, item.rssi),
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }

            FlowRow(
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalArrangement = Arrangement.spacedBy(4.dp),
            ) {
                if (item.inContacts) {
                    Surface(
                        color = MaterialTheme.colorScheme.primaryContainer,
                        shape = MaterialTheme.shapes.small,
                    ) {
                        Text(
                            if (item.contactHops != null && item.contactHops > 0) {
                                pluralStringResource(R.plurals.nearby_in_contacts_hops, item.contactHops, item.contactHops)
                            } else {
                                stringResource(R.string.nearby_in_contacts)
                            },
                            modifier = Modifier.padding(horizontal = 8.dp, vertical = 2.dp),
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onPrimaryContainer,
                        )
                    }
                    Text(
                        stringResource(R.string.tap_to_login),
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        modifier = Modifier.align(Alignment.CenterVertically),
                    )
                } else {
                    Surface(
                        color = MaterialTheme.colorScheme.tertiaryContainer,
                        shape = MaterialTheme.shapes.small,
                    ) {
                        Text(
                            stringResource(R.string.nearby_not_in_contacts),
                            modifier = Modifier.padding(horizontal = 8.dp, vertical = 2.dp),
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onTertiaryContainer,
                        )
                    }
                }
            }
        }
    }
}
