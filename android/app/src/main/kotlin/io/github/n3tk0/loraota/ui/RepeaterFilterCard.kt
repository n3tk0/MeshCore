package io.github.n3tk0.loraota.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.KeyboardArrowDown
import androidx.compose.material.icons.filled.KeyboardArrowUp
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
import io.github.n3tk0.loraota.tools.FilterConfig
import io.github.n3tk0.loraota.tools.FilterIngress
import io.github.n3tk0.loraota.tools.FilterList
import io.github.n3tk0.loraota.tools.FilterNewKey
import io.github.n3tk0.loraota.tools.RepeaterFilter
import io.github.n3tk0.loraota.tools.RepeaterFilter.Err

@Composable
private fun filterErrText(e: Err): String = stringResource(
    when (e) {
        Err.EMPTY, Err.RANGE -> R.string.filter_err_range
        Err.HEX -> R.string.filter_err_hex
        Err.BLOCK_LEN -> R.string.filter_err_block_len
        Err.PATH_LEN -> R.string.filter_err_path_len
        Err.ODD -> R.string.filter_err_odd
        Err.NAME_HASH -> R.string.filter_err_hash
        Err.NAME_SPACE -> R.string.filter_err_space
        Err.NAME_LONG -> R.string.filter_err_name_long
        Err.SENDER_LONG -> R.string.filter_err_sender
        Err.TEXT_LONG -> R.string.filter_err_text
        Err.SECS -> R.string.filter_err_secs
        Err.PROB -> R.string.filter_err_prob
    },
)

/**
 * The repeater's packet filter, over its `filter ...` console commands. Nothing is read until "Load" is pressed
 * (each read is a LoRa message); each change is one command and a read back of that setting.
 */
@Composable
fun RepeaterFilterCard(vm: AppViewModel) {
    val cfg = vm.filterConfig
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth()) {
                Column(Modifier.weight(1f)) {
                    Text(stringResource(R.string.filter_title), style = MaterialTheme.typography.titleMedium)
                    Text(
                        stringResource(R.string.filter_subtitle),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
                OutlinedButton(onClick = { vm.loadFilter() }, enabled = !vm.busy) {
                    Text(stringResource(if (vm.filterLoaded) R.string.refresh else R.string.rset_load))
                }
            }
            when {
                !vm.filterLoaded -> Text(
                    stringResource(R.string.filter_hint_load),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                vm.filterUnsupported -> Text(
                    stringResource(R.string.filter_unsupported),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.error,
                )
                else -> FilterBody(vm, cfg)
            }
        }
    }
}

@Composable
private fun FilterBody(vm: AppViewModel, cfg: FilterConfig) {
    val busy = vm.busy
    SwitchRow(stringResource(R.string.filter_enabled), null, cfg.enabled, !busy) {
        vm.filterCommand(RepeaterFilter.cmdEnable(it), listOf(RepeaterFilter.STATUS, RepeaterFilter.DRYRUN))
    }
    SwitchRow(stringResource(R.string.filter_dryrun), stringResource(R.string.filter_dryrun_hint), cfg.dryRun, !busy) {
        vm.filterCommand(RepeaterFilter.cmdOnOff(RepeaterFilter.DRYRUN, it), listOf(RepeaterFilter.DRYRUN))
    }
    SwitchRow(stringResource(R.string.filter_malformed), null, cfg.malformed, !busy) {
        vm.filterCommand(RepeaterFilter.cmdOnOff(RepeaterFilter.MALFORMED, it), listOf(RepeaterFilter.MALFORMED))
    }
    HorizontalDivider()

    // hash
    var hash by remember(cfg.hash) { mutableStateOf(cfg.hash.toString()) }
    SettingBlock(stringResource(R.string.filter_hash), stringResource(R.string.filter_now, cfg.hash.toString())) {
        if ((hash.trim().toIntOrNull() ?: 0) >= 2 || cfg.hash >= 2) WarnText(stringResource(R.string.filter_hash_warn))
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.Top) {
            NumField(hash, { hash = it }, stringResource(R.string.filter_hash), RepeaterFilter.validateHash(hash), Modifier.weight(1f))
            SetButton(!busy && RepeaterFilter.validateHash(hash) == null) {
                vm.filterCommand(RepeaterFilter.cmdHash(hash.trim().toInt()), listOf(RepeaterFilter.HASH))
            }
        }
    }

    // advert window
    var advert by remember(cfg.advertHours) { mutableStateOf(cfg.advertHours.toString()) }
    SettingBlock(
        stringResource(R.string.filter_advert),
        stringResource(R.string.filter_now, if (cfg.advertHours == 0) stringResource(R.string.filter_value_off) else stringResource(R.string.filter_value_hours, cfg.advertHours)),
    ) {
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.Top) {
            NumField(advert, { advert = it }, stringResource(R.string.filter_advert), RepeaterFilter.validateAdvert(advert), Modifier.weight(1f))
            SetButton(!busy && RepeaterFilter.validateAdvert(advert) == null) {
                vm.filterCommand(RepeaterFilter.cmdAdvert(advert.trim().toInt()), listOf(RepeaterFilter.ADVERT))
            }
        }
    }

    // age
    var age by remember(cfg.ageMin) { mutableStateOf(if (cfg.ageMin > 0) cfg.ageMin.toString() else "") }
    SettingBlock(
        stringResource(R.string.filter_age),
        stringResource(R.string.filter_now, if (cfg.ageMin <= 0) stringResource(R.string.filter_value_off) else stringResource(R.string.filter_value_mins, cfg.ageMin)),
    ) {
        WarnText(stringResource(R.string.filter_age_warn))
        if (cfg.ageMin > 0 && cfg.ageInactive) WarnText(stringResource(R.string.filter_age_inactive))
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.Top) {
            NumField(age, { age = it }, stringResource(R.string.filter_age), if (age.isBlank()) null else RepeaterFilter.validateAge(age), Modifier.weight(1f))
            SetButton(!busy && RepeaterFilter.validateAge(age) == null) {
                vm.filterCommand(RepeaterFilter.cmdAge(age.trim().toInt()), listOf(RepeaterFilter.AGE))
            }
            TextButton(onClick = { vm.filterCommand(RepeaterFilter.cmdAge(null), listOf(RepeaterFilter.AGE)) }, enabled = !busy && cfg.ageMin > 0) {
                Text(stringResource(R.string.filter_turn_off))
            }
        }
    }

    // ingress (null: the firmware does not have it)
    cfg.ingress?.let { ing ->
        var n by remember(ing) { mutableStateOf(if (ing.off) "" else ing.n.toString()) }
        var secs by remember(ing) { mutableStateOf(if (ing.off || ing.secs <= 0) "60" else ing.secs.toString()) }
        var soft by remember(ing) { mutableStateOf(if (ing.soft > 0) ing.soft.toString() else "") }
        val err = if (n.isBlank()) null else RepeaterFilter.validateIngress(n, secs, soft)
        val now = if (ing.off) stringResource(R.string.filter_value_off)
        else stringResource(R.string.filter_value_ingress, ing.n, ing.secs) + if (ing.soft > 0) stringResource(R.string.filter_value_soft, ing.soft) else ""
        SettingBlock(stringResource(R.string.filter_ingress), stringResource(R.string.filter_now, now)) {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                NumField(n, { n = it }, stringResource(R.string.filter_ingress_n), err, Modifier.weight(1f))
                NumField(secs, { secs = it }, stringResource(R.string.filter_ingress_secs), err, Modifier.weight(1f))
                NumField(soft, { soft = it }, stringResource(R.string.filter_ingress_soft), err, Modifier.weight(1f))
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
                SetButton(!busy && RepeaterFilter.validateIngress(n, secs, soft) == null) {
                    val cmd = RepeaterFilter.cmdIngress(FilterIngress(n.trim().toInt(), secs.trim().toInt(), soft.trim().toIntOrNull() ?: 0))
                    vm.filterCommand(cmd, listOf(RepeaterFilter.INGRESS))
                }
                TextButton(onClick = { vm.filterCommand(RepeaterFilter.cmdIngress(null), listOf(RepeaterFilter.INGRESS)) }, enabled = !busy && !ing.off) {
                    Text(stringResource(R.string.filter_turn_off))
                }
                if (err != null) Text(filterErrText(err), color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodySmall)
            }
        }
    }

    // newkey
    cfg.newKey?.let { nk ->
        var hours by remember(nk) { mutableStateOf(if (nk.off) "" else nk.hours.toString()) }
        var mins by remember(nk) { mutableStateOf(if (nk.off || nk.mins <= 0) "60" else nk.mins.toString()) }
        val err = if (hours.isBlank()) null else RepeaterFilter.validateNewKey(hours, mins)
        val now = if (nk.off) stringResource(R.string.filter_value_off) else stringResource(R.string.filter_value_newkey, nk.hours, nk.mins)
        SettingBlock(stringResource(R.string.filter_newkey), stringResource(R.string.filter_now, now)) {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                NumField(hours, { hours = it }, stringResource(R.string.filter_newkey_hours), err, Modifier.weight(1f))
                NumField(mins, { mins = it }, stringResource(R.string.filter_newkey_mins), err, Modifier.weight(1f))
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
                SetButton(!busy && RepeaterFilter.validateNewKey(hours, mins) == null) {
                    val cmd = RepeaterFilter.cmdNewKey(FilterNewKey(hours.trim().toInt(), mins.trim().toIntOrNull() ?: 60))
                    vm.filterCommand(cmd, listOf(RepeaterFilter.NEWKEY))
                }
                TextButton(onClick = { vm.filterCommand(RepeaterFilter.cmdNewKey(null), listOf(RepeaterFilter.NEWKEY)) }, enabled = !busy && !nk.off) {
                    Text(stringResource(R.string.filter_turn_off))
                }
                if (err != null) Text(filterErrText(err), color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodySmall)
            }
        }
    }
    HorizontalDivider()

    for (l in FilterList.entries) FilterListSection(vm, cfg, l)
    HorizontalDivider()
    FilterStats(vm)
}

@Composable
private fun SwitchRow(title: String, sub: String?, checked: Boolean, enabled: Boolean, onChange: (Boolean) -> Unit) {
    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f)) {
            Text(title, style = MaterialTheme.typography.bodyMedium)
            if (sub != null) Text(sub, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        Switch(checked = checked, onCheckedChange = onChange, enabled = enabled)
    }
}

@Composable
private fun SettingBlock(title: String, now: String, content: @Composable ColumnScope.() -> Unit) {
    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Text(title, style = MaterialTheme.typography.bodyMedium, modifier = Modifier.weight(1f))
        }
        Text(now, style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        content()
    }
}

@Composable
private fun WarnText(text: String) {
    Text(text, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.error)
}

@Composable
private fun NumField(value: String, onChange: (String) -> Unit, label: String, err: Err?, modifier: Modifier) {
    OutlinedTextField(
        value = value,
        onValueChange = { onChange(it.filter(Char::isDigit).take(6)) },
        label = { Text(label, maxLines = 1) },
        isError = err != null,
        supportingText = err?.let { e -> { Text(filterErrText(e)) } },
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
        singleLine = true,
        modifier = modifier,
    )
}

@Composable
private fun SetButton(enabled: Boolean, onClick: () -> Unit) {
    Button(onClick = onClick, enabled = enabled, modifier = Modifier.padding(top = 6.dp)) { Text(stringResource(R.string.filter_set)) }
}

private fun listTitle(l: FilterList) = when (l) {
    FilterList.BLOCK -> R.string.filter_list_block
    FilterList.PATH -> R.string.filter_list_path
    FilterList.CHANNEL -> R.string.filter_list_channel
    FilterList.WATCH -> R.string.filter_list_watch
    FilterList.SENDER -> R.string.filter_list_sender
    FilterList.TEXT -> R.string.filter_list_text
}

private fun listHint(l: FilterList) = when (l) {
    FilterList.BLOCK -> R.string.filter_hint_block
    FilterList.PATH -> R.string.filter_hint_path
    FilterList.CHANNEL -> R.string.filter_hint_channel
    FilterList.WATCH -> R.string.filter_hint_watch
    FilterList.SENDER -> R.string.filter_hint_sender
    FilterList.TEXT -> R.string.filter_hint_text
}

private fun validateEntry(l: FilterList, v: String): Err? = when (l) {
    FilterList.BLOCK -> RepeaterFilter.validateBlock(v)
    FilterList.PATH -> RepeaterFilter.validatePath(v)
    FilterList.CHANNEL -> RepeaterFilter.validateChannel(v, allowPublic = true)
    FilterList.WATCH -> RepeaterFilter.validateChannel(v, allowPublic = false)
    FilterList.SENDER -> RepeaterFilter.validateSender(v)
    FilterList.TEXT -> RepeaterFilter.validateText(v)
}

/** One list (block, path, channel, watch, sender, text): expandable, entries with remove, and an add row. */
@Composable
private fun FilterListSection(vm: AppViewModel, cfg: FilterConfig, l: FilterList) {
    var open by rememberSaveable(l.name) { mutableStateOf(false) }
    var input by rememberSaveable(l.name + "_in") { mutableStateOf("") }
    var secs by rememberSaveable(l.name + "_secs") { mutableStateOf("") }
    var prob by rememberSaveable(l.name + "_prob") { mutableStateOf("") }
    val entries = cfg.lists[l].orEmpty()
    val hasRule = l == FilterList.SENDER || l == FilterList.TEXT
    val value = input.trim()
    val entryErr = if (value.isEmpty()) null else validateEntry(l, value)
    val numErr = if (hasRule) RepeaterFilter.validateRuleNumbers(secs, prob) else null
    val canAdd = !vm.busy && value.isNotEmpty() && entryErr == null && numErr == null

    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Row(
            Modifier.fillMaxWidth().clickable { open = !open },
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(
                stringResource(listTitle(l)) + " (${entries.size})",
                style = MaterialTheme.typography.titleSmall,
                modifier = Modifier.weight(1f).padding(vertical = 6.dp),
            )
            Icon(
                if (open) Icons.Default.KeyboardArrowUp else Icons.Default.KeyboardArrowDown,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        if (open) {
            if (entries.isEmpty()) {
                Text(stringResource(R.string.filter_list_empty), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
            for (e in entries) {
                Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                    Text(e.display, fontFamily = FontFamily.Monospace, style = MaterialTheme.typography.bodySmall, modifier = Modifier.weight(1f))
                    if (e.removable) {
                        IconButton(
                            onClick = { vm.filterCommand(RepeaterFilter.cmdRemove(l, e.key), listOf(l.listCommand)) },
                            enabled = !vm.busy,
                            modifier = Modifier.size(36.dp),
                        ) {
                            Icon(Icons.Default.Delete, contentDescription = stringResource(R.string.filter_list_remove), modifier = Modifier.size(18.dp))
                        }
                    }
                }
            }
            if (l in cfg.truncated) WarnText(stringResource(R.string.filter_list_truncated))
            OutlinedTextField(
                value = input,
                onValueChange = { input = it.replace('\n', ' ').take(64) },
                label = { Text(stringResource(listHint(l)), maxLines = 1) },
                isError = entryErr != null,
                supportingText = entryErr?.let { e -> { Text(filterErrText(e)) } },
                singleLine = true,
                enabled = !vm.busy,
                modifier = Modifier.fillMaxWidth(),
            )
            if (hasRule) {
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    OutlinedTextField(
                        value = secs, onValueChange = { secs = it.filter(Char::isDigit).take(5) },
                        label = { Text(stringResource(R.string.filter_hint_secs), maxLines = 1) },
                        isError = numErr == Err.SECS,
                        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                        singleLine = true, enabled = !vm.busy, modifier = Modifier.weight(1f),
                    )
                    OutlinedTextField(
                        value = prob, onValueChange = { prob = it.filter(Char::isDigit).take(3) },
                        label = { Text(stringResource(R.string.filter_hint_prob), maxLines = 1) },
                        isError = numErr == Err.PROB,
                        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                        singleLine = true, enabled = !vm.busy, modifier = Modifier.weight(1f),
                    )
                }
                if (numErr != null) Text(filterErrText(numErr), color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodySmall)
            }
            Button(
                onClick = {
                    val cmd = RepeaterFilter.cmdAdd(l, value, secs.trim().toIntOrNull(), prob.trim().toIntOrNull())
                    vm.filterCommand(cmd, listOf(l.listCommand))
                    input = ""; secs = ""; prob = ""
                },
                enabled = canAdd,
            ) { Text(stringResource(R.string.filter_list_add)) }
        }
    }
}

/** The totals of the status line and the airtime the drops saved: both read on demand. */
@Composable
private fun FilterStats(vm: AppViewModel) {
    var open by rememberSaveable("filter_stats") { mutableStateOf(false) }
    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Row(Modifier.fillMaxWidth().clickable { open = !open }, verticalAlignment = Alignment.CenterVertically) {
            Text(stringResource(R.string.filter_stats), style = MaterialTheme.typography.titleSmall, modifier = Modifier.weight(1f).padding(vertical = 6.dp))
            Icon(
                if (open) Icons.Default.KeyboardArrowUp else Icons.Default.KeyboardArrowDown,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        if (open) {
            OutlinedButton(onClick = { vm.loadFilterStats() }, enabled = !vm.busy) {
                Text(stringResource(if (vm.filterStatsStatus != null) R.string.refresh else R.string.filter_stats_load))
            }
            vm.filterStatsStatus?.let {
                Text(it, fontFamily = FontFamily.Monospace, style = MaterialTheme.typography.bodySmall)
            }
            vm.filterStatsAir?.let {
                Text(stringResource(R.string.filter_stats_air, it), fontFamily = FontFamily.Monospace, style = MaterialTheme.typography.bodySmall)
            }
        }
    }
}
