package io.github.n3tk0.loraota.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import io.github.n3tk0.loraota.R
import io.github.n3tk0.loraota.tools.MailboxRecipient
import io.github.n3tk0.loraota.tools.MailboxStatus
import io.github.n3tk0.loraota.tools.RepeaterMailbox

/**
 * The repeater's mailbox over its `mailbox ...` console commands: who gets mail (named from the companion's contacts),
 * "Add me" with the connected companion's own key, a recipient from the contacts or a pasted key, and the settings.
 * Nothing is read until "Load" is pressed; each change is one command and a read back.
 */
@Composable
fun RepeaterMailboxCard(vm: AppViewModel) {
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth()) {
                Column(Modifier.weight(1f)) {
                    Text(stringResource(R.string.mailbox_title), style = MaterialTheme.typography.titleMedium)
                    Text(
                        stringResource(R.string.mailbox_subtitle),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
                OutlinedButton(onClick = { vm.loadMailbox() }, enabled = !vm.busy) {
                    Text(stringResource(if (vm.mailboxLoaded) R.string.refresh else R.string.rset_load))
                }
            }
            val status = vm.mailboxStatus
            when {
                !vm.mailboxLoaded -> Text(
                    stringResource(R.string.mailbox_hint_load),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                vm.mailboxUnsupported || status == null -> Text(
                    stringResource(R.string.mailbox_unsupported),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.error,
                )
                else -> MailboxBody(vm, status)
            }
        }
    }
}

@Composable
private fun MailboxBody(vm: AppViewModel, status: MailboxStatus) {
    val busy = vm.busy
    val recipients = vm.mailboxRecipients
    val selfKey = vm.selfKeyHex
    val selfIn = recipients.any { it.isSelf }
    val full = recipients.size >= RepeaterMailbox.MAX_OWNERS
    var confirmOnFor by remember { mutableStateOf<String?>(null) }   // a key to add while the mailbox is off
    var removeOf by remember { mutableStateOf<MailboxRecipient?>(null) }
    var picking by remember { mutableStateOf(false) }
    var confirmClear by remember { mutableStateOf(false) }

    fun add(key: String) {
        if (!status.enabled) confirmOnFor = key else vm.mailboxCommand(RepeaterMailbox.cmdAdd(key))
    }

    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f)) {
            Text(stringResource(R.string.mailbox_enabled), style = MaterialTheme.typography.bodyMedium)
            Text(
                stringResource(R.string.mailbox_kept_total, status.kept, status.max),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        Switch(checked = status.enabled, onCheckedChange = { vm.mailboxCommand(RepeaterMailbox.cmdEnable(it)) }, enabled = !busy)
    }
    if (!vm.mailboxDirect) {
        Text(stringResource(R.string.mailbox_hop_warn), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.error)
    }
    HorizontalDivider()

    Text(
        stringResource(R.string.mailbox_recipients, recipients.size, RepeaterMailbox.MAX_OWNERS),
        style = MaterialTheme.typography.titleSmall,
    )
    if (recipients.isEmpty()) {
        Text(stringResource(R.string.mailbox_none), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
    for (r in recipients) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text(recipientName(r), style = MaterialTheme.typography.bodyMedium)
                Text(
                    listOfNotNull(
                        r.prefix,
                        stringResource(R.string.mailbox_kept, r.kept),
                        if (r.here) stringResource(R.string.mailbox_here) else null,
                    ).joinToString(" · "),
                    style = MaterialTheme.typography.bodySmall,
                    fontFamily = FontFamily.Monospace,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            IconButton(onClick = { removeOf = r }, enabled = !busy, modifier = Modifier.size(40.dp)) {
                Icon(Icons.Default.Delete, contentDescription = stringResource(R.string.mailbox_remove), modifier = Modifier.size(20.dp))
            }
        }
    }

    when {
        selfIn -> Text(stringResource(R.string.mailbox_me_already), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.primary)
        selfKey.length != RepeaterMailbox.KEY_HEX -> Text(stringResource(R.string.mailbox_no_self_key), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.error)
    }
    if (full) {
        Text(stringResource(R.string.mailbox_full, RepeaterMailbox.MAX_OWNERS), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        Button(onClick = { add(selfKey) }, enabled = !busy && !full && !selfIn && selfKey.length == RepeaterMailbox.KEY_HEX) {
            Text(stringResource(R.string.mailbox_add_me))
        }
        OutlinedButton(onClick = { picking = true }, enabled = !busy && !full) {
            Text(stringResource(R.string.mailbox_add_contact))
        }
    }
    var keyInput by rememberSaveable { mutableStateOf("") }
    val key = RepeaterMailbox.normalizeKey(keyInput)
    val keyErr = keyInput.isNotBlank() && key == null
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.Top) {
        OutlinedTextField(
            value = keyInput,
            onValueChange = { keyInput = it.replace('\n', ' ').take(80) },
            label = { Text(stringResource(R.string.mailbox_paste_key), maxLines = 1) },
            isError = keyErr,
            supportingText = if (keyErr) ({ Text(stringResource(R.string.mailbox_key_err)) }) else null,
            singleLine = true,
            enabled = !busy && !full,
            textStyle = MaterialTheme.typography.bodySmall.copy(fontFamily = FontFamily.Monospace),
            modifier = Modifier.weight(1f),
        )
        Button(
            onClick = { key?.let { add(it); keyInput = "" } },
            enabled = !busy && !full && key != null && !vm.isMailboxOwner(key),
            modifier = Modifier.padding(top = 6.dp),
        ) { Text(stringResource(R.string.mailbox_add)) }
    }
    HorizontalDivider()

    NumSetting(
        label = stringResource(R.string.mailbox_ttl),
        now = stringResource(R.string.mailbox_ttl_now, status.ttlHours),
        initial = status.ttlHours,
        valid = RepeaterMailbox::validTtl,
        busy = busy,
    ) { vm.mailboxCommand(RepeaterMailbox.cmdTtl(it)) }
    NumSetting(
        label = stringResource(R.string.mailbox_away),
        now = stringResource(R.string.mailbox_away_now, status.awayMins),
        initial = status.awayMins,
        valid = RepeaterMailbox::validAway,
        busy = busy,
    ) { vm.mailboxCommand(RepeaterMailbox.cmdAway(it)) }
    TextButton(onClick = { confirmClear = true }, enabled = !busy && status.kept > 0) {
        Text(stringResource(R.string.mailbox_clear), color = MaterialTheme.colorScheme.error)
    }

    confirmOnFor?.let { k ->
        AlertDialog(
            onDismissRequest = { confirmOnFor = null },
            title = { Text(stringResource(R.string.mailbox_turn_on_title)) },
            text = { Text(stringResource(R.string.mailbox_turn_on_text)) },
            confirmButton = {
                TextButton(onClick = {
                    confirmOnFor = null
                    vm.mailboxCommand(RepeaterMailbox.cmdAdd(k), RepeaterMailbox.cmdEnable(true))
                }) { Text(stringResource(R.string.mailbox_add_and_on)) }
            },
            dismissButton = {
                TextButton(onClick = {
                    confirmOnFor = null
                    vm.mailboxCommand(RepeaterMailbox.cmdAdd(k))
                }) { Text(stringResource(R.string.mailbox_add_only)) }
            },
        )
    }
    removeOf?.let { r ->
        AlertDialog(
            onDismissRequest = { removeOf = null },
            title = { Text(stringResource(R.string.mailbox_remove_title, recipientName(r))) },
            text = { Text(stringResource(R.string.mailbox_remove_text)) },
            confirmButton = {
                TextButton(onClick = {
                    removeOf = null
                    vm.mailboxCommand(RepeaterMailbox.cmdDel(r.fullKey ?: r.prefix))
                }) { Text(stringResource(R.string.mailbox_remove), color = MaterialTheme.colorScheme.error) }
            },
            dismissButton = { TextButton(onClick = { removeOf = null }) { Text(stringResource(R.string.cancel)) } },
        )
    }
    if (confirmClear) {
        AlertDialog(
            onDismissRequest = { confirmClear = false },
            title = { Text(stringResource(R.string.mailbox_clear)) },
            text = { Text(stringResource(R.string.mailbox_clear_text)) },
            confirmButton = {
                TextButton(onClick = { confirmClear = false; vm.mailboxCommand(RepeaterMailbox.CLEAR) }) {
                    Text(stringResource(R.string.mailbox_clear), color = MaterialTheme.colorScheme.error)
                }
            },
            dismissButton = { TextButton(onClick = { confirmClear = false }) { Text(stringResource(R.string.cancel)) } },
        )
    }
    if (picking) {
        AlertDialog(
            onDismissRequest = { picking = false },
            title = { Text(stringResource(R.string.mailbox_pick_title)) },
            text = {
                Column(Modifier.heightIn(max = 420.dp).verticalScroll(rememberScrollState())) {
                    if (vm.peopleContacts.isEmpty()) {
                        Text(stringResource(R.string.mailbox_no_people), style = MaterialTheme.typography.bodyMedium)
                    }
                    for ((k, name) in vm.peopleContacts) {
                        val owner = vm.isMailboxOwner(k)
                        Row(
                            Modifier.fillMaxWidth().clickable(enabled = !owner) { picking = false; add(k) }.padding(vertical = 10.dp),
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            Column(Modifier.weight(1f)) {
                                Text(name, style = MaterialTheme.typography.bodyLarge)
                                Text(k.take(RepeaterMailbox.PREFIX_HEX), style = MaterialTheme.typography.bodySmall,
                                    fontFamily = FontFamily.Monospace, color = MaterialTheme.colorScheme.onSurfaceVariant)
                            }
                            if (owner) Text(stringResource(R.string.mailbox_is_recipient), style = MaterialTheme.typography.labelMedium,
                                color = MaterialTheme.colorScheme.primary)
                        }
                    }
                }
            },
            confirmButton = { TextButton(onClick = { picking = false }) { Text(stringResource(R.string.cancel)) } },
        )
    }
}

@Composable
private fun recipientName(r: MailboxRecipient): String = when {
    r.isSelf -> stringResource(R.string.mailbox_you)
    r.name != null -> r.name
    else -> stringResource(R.string.mailbox_unknown_contact)
}

@Composable
private fun NumSetting(label: String, now: String, initial: Int, valid: (String) -> Int?, busy: Boolean, onSet: (Int) -> Unit) {
    var text by remember(initial) { mutableStateOf(initial.toString()) }
    val v = valid(text)
    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
        Text(now, style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.Top) {
            OutlinedTextField(
                value = text,
                onValueChange = { text = it.filter(Char::isDigit).take(4) },
                label = { Text(label, maxLines = 1) },
                isError = v == null,
                supportingText = if (v == null) ({ Text(stringResource(R.string.mailbox_range_err)) }) else null,
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                singleLine = true,
                modifier = Modifier.weight(1f),
            )
            Button(onClick = { v?.let(onSet) }, enabled = !busy && v != null && v != initial, modifier = Modifier.padding(top = 6.dp)) {
                Text(stringResource(R.string.filter_set))
            }
        }
    }
}

/** "✉ 2 · you too" on a repeater row: its recipients at the last read (kept on the phone). */
@Composable
fun MailboxBadge(s: io.github.n3tk0.loraota.tools.MailboxSummary) {
    val text = when {
        !s.enabled -> stringResource(R.string.mailbox_badge_off)
        s.selfIncluded -> stringResource(R.string.mailbox_badge_self, s.recipients.size)
        else -> stringResource(R.string.mailbox_badge, s.recipients.size)
    }
    Text(
        text,
        style = MaterialTheme.typography.labelMedium,
        color = if (s.enabled && s.selfIncluded) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant,
    )
}
