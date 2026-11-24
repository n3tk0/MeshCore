package io.github.n3tk0.loraota.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import io.github.n3tk0.loraota.R
import io.github.n3tk0.loraota.backup.Backup
import io.github.n3tk0.loraota.backup.BackupCompare
import java.text.DateFormat
import java.util.Date

private const val MAX_NAMES = 12

/** Two backups side by side (the older one first): settings, channels and contacts, secrets redacted. */
@Composable
fun BackupCompareDialog(a: Backup, b: Backup, onClose: () -> Unit) {
    Dialog(onDismissRequest = onClose, properties = DialogProperties(usePlatformDefaultWidth = false)) {
        BackupCompareContent(a, b, onClose)
    }
}

/** The compare page itself (the dialog's content; also rendered on its own for the docs screenshots). */
@Composable
fun BackupCompareContent(a: Backup, b: Backup, onClose: () -> Unit) {
    val (old, new) = remember(a, b) { BackupCompare.inTimeOrder(a, b) }
    val cmp = remember(old, new) { BackupCompare.compare(old, new) }
    // the app's language (it can differ from the phone's), not the JVM default
    val locale = androidx.compose.ui.platform.LocalConfiguration.current.locales[0]
    val fmt = remember(locale) { DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT, locale) }
    fun label(x: Backup) = "${x.deviceName.ifEmpty { x.deviceKey.take(8) }} · ${fmt.format(Date(x.time))}" +
        (if (x.firmware.isNotEmpty()) " · ${x.firmware.replace("|", " · ")}" else "")

    run {
        Surface(Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
            Column(Modifier.statusBarsPadding().padding(horizontal = 16.dp)) {
                Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                    Text(stringResource(R.string.compare_title), style = MaterialTheme.typography.titleLarge, modifier = Modifier.weight(1f))
                    TextButton(onClick = onClose) { Text(stringResource(R.string.build_close)) }
                }
                Column(Modifier.verticalScroll(rememberScrollState()).padding(bottom = 24.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                    Text(stringResource(R.string.compare_old, label(old)), style = MaterialTheme.typography.bodySmall)
                    Text(stringResource(R.string.compare_new, label(new)), style = MaterialTheme.typography.bodySmall)
                    if (old.deviceKey != new.deviceKey) {
                        Text(stringResource(R.string.compare_other_device), style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.error)
                    }
                    if (cmp.isEmpty) {
                        Text(stringResource(R.string.compare_none), modifier = Modifier.padding(top = 8.dp))
                    }

                    if (cmp.settingsAdded.isNotEmpty() || cmp.settingsRemoved.isNotEmpty() || cmp.settingsChanged.isNotEmpty()) {
                        SectionTitle(stringResource(R.string.compare_settings))
                        for (c in cmp.settingsChanged) Line(c.label, "${c.old}  →  ${c.new}", c.secret, '~')
                        for (e in cmp.settingsAdded) Line(e.label, e.value, e.secret, '+')
                        for (e in cmp.settingsRemoved) Line(e.label, e.value, e.secret, '−')
                    }
                    if (cmp.channelsAdded.isNotEmpty() || cmp.channelsRemoved.isNotEmpty() || cmp.channelsChanged.isNotEmpty()) {
                        SectionTitle(stringResource(R.string.compare_channels))
                        for (c in cmp.channelsChanged) {
                            val text = if (c.old == c.new) c.old else "${c.old}  →  ${c.new}"
                            Line(c.label, text, c.secret, '~', note = if (c.secret) stringResource(R.string.compare_secret_changed) else null)
                        }
                        for (e in cmp.channelsAdded) Line("#${e.id}", e.label, false, '+')
                        for (e in cmp.channelsRemoved) Line("#${e.id}", e.label, false, '−')
                    }
                    if (cmp.contactsOld != cmp.contactsNew || cmp.contactsAdded.isNotEmpty() || cmp.contactsRemoved.isNotEmpty()) {
                        SectionTitle(stringResource(R.string.compare_contacts))
                        val d = cmp.contactsDelta
                        Text(
                            stringResource(R.string.compare_contacts_count, cmp.contactsOld, cmp.contactsNew, if (d > 0) "+$d" else "$d"),
                            style = MaterialTheme.typography.bodyMedium,
                        )
                        Names('+', cmp.contactsAdded)
                        Names('−', cmp.contactsRemoved)
                    }
                }
            }
        }
    }
}

@Composable
private fun SectionTitle(text: String) {
    Text(text, style = MaterialTheme.typography.titleSmall, color = MaterialTheme.colorScheme.primary, modifier = Modifier.padding(top = 10.dp))
    HorizontalDivider()
}

@Composable
private fun Line(key: String, value: String, secret: Boolean, mark: Char, note: String? = null) {
    Column(Modifier.fillMaxWidth()) {
        Text("$mark $key", style = MaterialTheme.typography.labelMedium, fontFamily = FontFamily.Monospace,
            color = when (mark) {
                '+' -> MaterialTheme.colorScheme.primary
                '−' -> MaterialTheme.colorScheme.error
                else -> MaterialTheme.colorScheme.onSurface
            })
        Text(value, style = MaterialTheme.typography.bodySmall, fontFamily = FontFamily.Monospace)
        val n = note ?: if (secret) stringResource(R.string.compare_secret_changed) else null
        if (n != null && mark == '~') {
            Text(n, style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
}

@Composable
private fun Names(mark: Char, names: List<String>) {
    if (names.isEmpty()) return
    val shown = names.take(MAX_NAMES).joinToString(", ") + if (names.size > MAX_NAMES) " … +${names.size - MAX_NAMES}" else ""
    Text("$mark $shown", style = MaterialTheme.typography.bodySmall,
        color = if (mark == '+') MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.error)
}
