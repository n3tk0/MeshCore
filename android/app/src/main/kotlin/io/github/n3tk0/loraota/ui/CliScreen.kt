package io.github.n3tk0.loraota.ui

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.Send
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.KeyboardArrowDown
import androidx.compose.material.icons.filled.KeyboardArrowUp
import androidx.compose.material.icons.filled.Share
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.AssistChip
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.InputChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.SuggestionChip
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import io.github.n3tk0.loraota.R
import io.github.n3tk0.loraota.tools.CliCommandInfo
import io.github.n3tk0.loraota.tools.CliCommands
import io.github.n3tk0.loraota.tools.CliHelper
import io.github.n3tk0.loraota.tools.CliHistory
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Interactive CLI console for both the logged-in repeater (remote tagged CLI) and the connected companion (local CLI).
 * Features RTT badges, tap-to-copy/refill transcript lines, custom pinned snippets, history, autocomplete,
 * dangerous-command confirmation, and redacted copy/share.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun CliConsole(vm: AppViewModel, isCompanion: Boolean) {
    val ctx = LocalContext.current
    var input by (if (isCompanion) vm.toolsState::companionCliInput else vm.toolsState::repeaterCliInput)
    var historyIdx by remember { mutableIntStateOf(-1) }
    var pendingConfirm by remember { mutableStateOf<String?>(null) }
    var copiedNotice by remember { mutableStateOf(false) }
    var copiedLineNotice by remember { mutableStateOf(false) }
    var showAddSnippet by remember { mutableStateOf(false) }
    var newSnippetText by remember { mutableStateOf("") }

    val lines = if (isCompanion) vm.companionCliLog else vm.repeaterCliLog
    val quickCmds = if (isCompanion) CliHelper.COMPANION_QUICK_COMMANDS else CliHelper.REPEATER_QUICK_COMMANDS
    val history = vm.cliHistory(isCompanion)
    val suggestions = remember(input, isCompanion) {
        val known = CliCommands.match(input, isCompanion)
        // the setting names the firmware accepts (`get <key>` / `set <key>`) that the catalog does not list
        val extra = CliHelper.suggestions(input, isCompanion, 5)
            .filter { s -> known.none { it.fillText == s || it.template == s } }
            .map { CliCommandInfo(it, "") }
        (known + extra).take(5)
    }
    val timeFmt = remember { SimpleDateFormat("HH:mm:ss", Locale.ROOT) }
    val transcriptScroll = rememberScrollState()

    LaunchedEffect(lines.size) {
        copiedNotice = false
        copiedLineNotice = false
        if (lines.isNotEmpty()) transcriptScroll.animateScrollTo(transcriptScroll.maxValue)
    }

    fun submit(raw: String) {
        val cmd = raw.trim()
        if (cmd.isEmpty() || vm.busy) return
        if (CliHelper.isDangerous(cmd)) {
            pendingConfirm = cmd
        } else {
            vm.sendCliCommand(cmd, isCompanion)
            input = ""
            historyIdx = -1
        }
    }

    fun formatRtt(ms: Long?): String = when {
        ms == null -> ""
        ms >= 1000L -> String.format(Locale.ROOT, " (%.1fs)", ms / 1000.0)
        else -> " (${ms}ms)"
    }

    fun transcriptText(): String {
        val redacted = CliHelper.redactCliEntries(lines.map { (it.kind == CliLineKind.COMMAND) to it.text })
        return lines.zip(redacted).joinToString("\n") { (line, safeText) ->
            val prefix = when (line.kind) {
                CliLineKind.COMMAND -> "> "
                CliLineKind.REPLY -> "  "
                CliLineKind.INFO -> "# "
                CliLineKind.ERROR -> "! "
            }
            "[${timeFmt.format(Date(line.timeMs))}] $prefix$safeText${formatRtt(line.durationMs)}"
        }
    }

    // Stock companion warning for remote repeater CLI (fire-and-forget still allowed)
    if (!isCompanion && vm.canReadRepeater == false) {
        Card(
            Modifier.fillMaxWidth(),
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.secondaryContainer),
        ) {
            Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Icon(Icons.Default.Warning, null, modifier = Modifier.size(18.dp),
                        tint = MaterialTheme.colorScheme.onSecondaryContainer)
                    Spacer(Modifier.width(8.dp))
                    Text(
                        stringResource(R.string.cli_stock_warn_title),
                        style = MaterialTheme.typography.titleSmall,
                        color = MaterialTheme.colorScheme.onSecondaryContainer,
                    )
                }
                Text(
                    stringResource(R.string.cli_stock_warn_body),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSecondaryContainer,
                )
                OutlinedButton(
                    onClick = { vm.openFlash(FlashTarget.COMPANION) },
                    enabled = !vm.busy,
                ) {
                    Text(stringResource(R.string.flash_open))
                }
            }
        }
    }

    // Quick command chips + custom pinned snippets
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            stringResource(R.string.cli_quick_commands),
            style = MaterialTheme.typography.labelMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.weight(1f),
        )
        TextButton(onClick = { newSnippetText = input.trim(); showAddSnippet = true }) {
            Icon(Icons.Default.Add, null, modifier = Modifier.size(16.dp))
            Spacer(Modifier.width(2.dp))
            Text(stringResource(R.string.cli_add_snippet), style = MaterialTheme.typography.labelSmall)
        }
    }
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .horizontalScroll(rememberScrollState()),
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        for (snip in vm.customCliSnippets) {
            InputChip(
                selected = true,
                onClick = { submit(snip) },
                enabled = !vm.busy,
                label = { Text(snip, fontFamily = FontFamily.Monospace, style = MaterialTheme.typography.labelMedium) },
                trailingIcon = {
                    Icon(
                        Icons.Default.Close,
                        contentDescription = stringResource(R.string.cli_remove_snippet),
                        modifier = Modifier
                            .size(15.dp)
                            .clickable(enabled = !vm.busy) { vm.removeCliSnippet(snip) },
                    )
                },
            )
        }
        for (cmd in quickCmds) {
            AssistChip(
                onClick = { submit(cmd) },
                enabled = !vm.busy,
                label = { Text(cmd, fontFamily = FontFamily.Monospace, style = MaterialTheme.typography.labelMedium) },
            )
        }
    }

    // Terminal output box (tap command -> refill input; tap reply -> copy single redacted line)
    Surface(
        modifier = Modifier.fillMaxWidth(),
        color = Color(0xFF12161C),
        shape = MaterialTheme.shapes.medium,
        tonalElevation = 1.dp,
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .heightIn(min = 160.dp, max = 300.dp)
                .verticalScroll(transcriptScroll)
                .padding(10.dp),
            verticalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            if (lines.isEmpty()) {
                Text(
                    stringResource(if (isCompanion) R.string.cli_empty_companion else R.string.cli_empty_repeater),
                    color = Color(0xFF8C97A8),
                    fontFamily = FontFamily.Monospace,
                    style = MaterialTheme.typography.bodySmall,
                )
            } else {
                val redactedLines = remember(lines.size, lines.lastOrNull()) {
                    CliHelper.redactCliEntries(lines.map { (it.kind == CliLineKind.COMMAND) to it.text })
                }
                for ((idx, line) in lines.withIndex()) {
                    val (prefix, color) = when (line.kind) {
                        CliLineKind.COMMAND -> "> " to Color(0xFF7CC4FA)
                        CliLineKind.REPLY -> "" to Color(0xFFE6EDF3)
                        CliLineKind.INFO -> "# " to Color(0xFFFFC168)
                        CliLineKind.ERROR -> "! " to Color(0xFFFF7B72)
                    }
                    val isSecretCmd = line.kind == CliLineKind.COMMAND && CliHelper.isSecretWriteCommand(line.text)
                    val shownText = if (isSecretCmd) {
                        redactedLines.getOrElse(idx) { CliHelper.redactSecrets(line.text) }
                    } else {
                        line.text
                    }
                    val rttText = formatRtt(line.durationMs)
                    Text(
                        "${timeFmt.format(Date(line.timeMs))} $prefix$shownText$rttText",
                        color = color,
                        fontFamily = FontFamily.Monospace,
                        style = MaterialTheme.typography.bodySmall,
                        modifier = Modifier
                            .fillMaxWidth()
                            .clickable {
                                if (line.kind == CliLineKind.COMMAND) {
                                    if (!isSecretCmd) {
                                        input = line.text
                                        historyIdx = -1
                                    }
                                } else {
                                    val cm = ctx.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
                                    val safe = redactedLines.getOrElse(idx) { CliHelper.redactSecrets(line.text) }
                                    cm.setPrimaryClip(ClipData.newPlainText("MeshCore CLI line", safe))
                                    copiedLineNotice = true
                                }
                            },
                    )
                }
            }
        }
    }
    if (lines.isNotEmpty()) {
        Text(
            stringResource(if (copiedLineNotice) R.string.cli_line_copied else R.string.cli_tap_line_hint),
            style = MaterialTheme.typography.labelSmall,
            color = if (copiedLineNotice) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }

    // Live command suggestions when typing: tap fills the input (never sends)
    if (suggestions.isNotEmpty()) {
        Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
            for (s in suggestions) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clickable { input = s.fillText; historyIdx = -1 }
                        .padding(horizontal = 8.dp, vertical = 6.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    Text(s.template, fontFamily = FontFamily.Monospace, style = MaterialTheme.typography.labelMedium)
                    if (s.hint.isNotEmpty()) {
                        Text(
                            s.hint,
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                        )
                    }
                }
            }
        }
    }

    // Command input bar with history arrows and send button
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(4.dp),
    ) {
        OutlinedTextField(
            value = input,
            onValueChange = { input = it; historyIdx = -1 },
            modifier = Modifier.weight(1f),
            singleLine = true,
            enabled = !vm.busy,
            placeholder = { Text(stringResource(R.string.cli_input_hint)) },
            keyboardOptions = KeyboardOptions(imeAction = ImeAction.Send),
            keyboardActions = KeyboardActions(onSend = { submit(input) }),
        )
        IconButton(
            onClick = {
                CliHistory.older(history.size, historyIdx)?.let { historyIdx = it; input = history[it] }
            },
            enabled = !vm.busy && history.isNotEmpty(),
        ) {
            Icon(Icons.Default.KeyboardArrowUp, stringResource(R.string.cli_hist_prev))
        }
        IconButton(
            onClick = {
                val next = CliHistory.newer(historyIdx)
                historyIdx = next
                input = if (next >= 0) history[next] else ""
            },
            enabled = !vm.busy && historyIdx >= 0,
        ) {
            Icon(Icons.Default.KeyboardArrowDown, stringResource(R.string.cli_hist_next))
        }
        IconButton(onClick = { submit(input) }, enabled = !vm.busy && input.isNotBlank()) {
            Icon(Icons.AutoMirrored.Filled.Send, stringResource(R.string.cli_send),
                tint = if (!vm.busy && input.isNotBlank()) MaterialTheme.colorScheme.primary
                else MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }

    // Transcript utility actions (Copy / Share / Clear)
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        TextButton(
            onClick = {
                val cm = ctx.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
                cm.setPrimaryClip(ClipData.newPlainText("MeshCore CLI", transcriptText()))
                copiedNotice = true
            },
            enabled = lines.isNotEmpty(),
        ) {
            Text(stringResource(if (copiedNotice) R.string.cli_copied else R.string.cli_copy))
        }
        TextButton(
            onClick = {
                val intent = Intent(Intent.ACTION_SEND).apply {
                    type = "text/plain"
                    putExtra(Intent.EXTRA_TEXT, transcriptText())
                }
                runCatching { ctx.startActivity(Intent.createChooser(intent, ctx.getString(R.string.cli_share))) }
            },
            enabled = lines.isNotEmpty(),
        ) {
            Icon(Icons.Default.Share, null, modifier = Modifier.size(16.dp))
            Spacer(Modifier.width(4.dp))
            Text(stringResource(R.string.cli_share))
        }
        Spacer(Modifier.weight(1f))
        TextButton(
            onClick = { vm.clearCliLog(isCompanion); copiedNotice = false; copiedLineNotice = false },
            enabled = lines.isNotEmpty(),
        ) {
            Icon(Icons.Default.Delete, null, modifier = Modifier.size(16.dp))
            Spacer(Modifier.width(4.dp))
            Text(stringResource(R.string.cli_clear))
        }
    }

    // Add custom pinned command dialog
    if (showAddSnippet) {
        val trimmedSnippet = newSnippetText.trim()
        val isSecretSnippet = trimmedSnippet.isNotEmpty() && CliHelper.isSecretWriteCommand(trimmedSnippet)
        AlertDialog(
            onDismissRequest = { showAddSnippet = false },
            title = { Text(stringResource(R.string.cli_add_snippet_title)) },
            text = {
                OutlinedTextField(
                    value = newSnippetText,
                    onValueChange = { newSnippetText = it },
                    singleLine = true,
                    isError = isSecretSnippet,
                    supportingText = if (isSecretSnippet) {
                        { Text(stringResource(R.string.cli_snippet_secret_blocked)) }
                    } else null,
                    label = { Text(stringResource(R.string.cli_add_snippet_hint)) },
                    modifier = Modifier.fillMaxWidth(),
                )
            },
            confirmButton = {
                TextButton(
                    onClick = {
                        vm.addCliSnippet(newSnippetText)
                        showAddSnippet = false
                    },
                    enabled = trimmedSnippet.isNotEmpty() && !isSecretSnippet,
                ) { Text(stringResource(R.string.cli_add_snippet)) }
            },
            dismissButton = {
                TextButton(onClick = { showAddSnippet = false }) { Text(stringResource(R.string.cancel)) }
            },
        )
    }

    // Confirmation dialog for dangerous commands
    pendingConfirm?.let { cmd ->
        AlertDialog(
            onDismissRequest = { pendingConfirm = null },
            title = { Text(stringResource(R.string.cli_confirm_title)) },
            text = { Text(stringResource(R.string.cli_confirm_body, CliHelper.redactSecrets(cmd))) },
            confirmButton = {
                TextButton(onClick = {
                    pendingConfirm = null
                    vm.sendCliCommand(cmd, isCompanion)
                    input = ""
                    historyIdx = -1
                }) {
                    Text(stringResource(R.string.cli_confirm_run), color = MaterialTheme.colorScheme.error)
                }
            },
            dismissButton = {
                TextButton(onClick = { pendingConfirm = null }) { Text(stringResource(R.string.cancel)) }
            },
        )
    }
}
