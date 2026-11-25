package io.github.n3tk0.loraota.tools

import io.github.n3tk0.loraota.backup.RepeaterBackup
import io.github.n3tk0.loraota.ota.HistorySeries
import java.time.Instant
import java.time.ZoneOffset
import java.time.format.DateTimeFormatter
import java.util.Locale

/** One neighbor returned by a repeater's `neighbors` CLI command (hex8:secs_ago:snr_x4). */
data class NeighborEntry(
    val prefixHex: String,
    val secsAgo: Long,
    val snrDb: Float,
    val resolvedName: String? = null,
)

object NeighborsParser {
    /**
     * Parses `formatNeighborsReply` output from `examples/simple_repeater/MyMesh.cpp`:
     * `-none-`, or newline-separated `<8-hex-pubkey>:<secs_ago>:<snr_x4>`.
     * [contactsByPrefix] maps lowercase hex public keys (full or 8-char prefix) to contact names.
     */
    fun parse(raw: String, contactsByPrefix: Map<String, String> = emptyMap()): List<NeighborEntry> {
        val cleaned = raw.trim().removePrefix("> ").trim()
        if (cleaned.isEmpty() || cleaned.equals("-none-", ignoreCase = true)) return emptyList()
        val out = ArrayList<NeighborEntry>()
        for (line in cleaned.lines()) {
            val trimmed = line.trim()
            if (trimmed.isEmpty() || trimmed.equals("-none-", ignoreCase = true)) continue
            val parts = trimmed.split(':')
            if (parts.size < 3) continue
            val hexId = parts[0].trim().lowercase(Locale.ROOT)
            if (hexId.length < 4 || !hexId.all { it in '0'..'9' || it in 'a'..'f' }) continue
            val secs = parts[1].trim().toLongOrNull() ?: continue
            val snrRaw = parts[2].trim().toIntOrNull() ?: continue
            val name = contactsByPrefix[hexId]
                ?: contactsByPrefix.entries.firstOrNull { (k, _) ->
                    k.lowercase(Locale.ROOT).startsWith(hexId)
                }?.value
            out.add(NeighborEntry(hexId, secs.coerceAtLeast(0L), snrRaw / 4f, name))
        }
        return out.sortedBy { it.secsAgo }
    }
}

object CliHelper {
    private val DANGEROUS_EXACT = setOf("reboot", "poweroff", "shutdown", "clkreboot", "erase", "clear stats", "ota rollback")
    private val NO_REPLY_EXACT = setOf("reboot", "poweroff", "shutdown", "clkreboot")
    /** Firmware (`CommonCLI` / `MyMesh`) strips an optional `xx|` tag prefix before dispatching commands. */
    private val TAG_PREFIX = Regex("""^[A-Za-z0-9]{2}\|""")

    private fun normalizeCommand(command: String): String {
        val trimmed = command.trim()
        return TAG_PREFIX.replaceFirst(trimmed, "").trimStart().lowercase(Locale.ROOT)
    }

    val REPEATER_QUICK_COMMANDS = listOf(
        "ver",
        "selftest",
        "neighbors",
        "clock",
        "clock sync",
        "power",
        "i2c",
        "battsave",
        "ota status",
        "advert",
        "reboot",
    )

    val COMPANION_QUICK_COMMANDS = listOf(
        "ver",
        "board",
        "get name",
        "get tz.offset",
        "get wifi.status",
        "get wifi.ip",
        "reboot",
    )

    private val REPEATER_BASE_COMMANDS = listOf(
        "ver", "board", "selftest", "neighbors", "clock", "clock sync",
        "power", "i2c", "battsave", "battery capacity", "ota status",
        "advert", "advert.zerohop", "sensor list", "region", "clear stats",
        "get ", "set ", "reboot", "poweroff",
    )

    private val COMPANION_BASE_COMMANDS = listOf(
        "ver", "board", "get name", "set name ", "get tz.offset", "set tz.offset ",
        "get wifi.status", "get wifi.ip", "get wifi.ssid", "get wifi.enabled",
        "set wifi.enabled ", "set wifi.ssid ", "tempradio off", "reboot",
    )

    /** True when the command restarts, shuts down, erases, rolls back OTA, resets stats, or changes passwords/keys. */
    fun isDangerous(command: String): Boolean {
        val c = normalizeCommand(command)
        if (c.isEmpty()) return false
        if (c in DANGEROUS_EXACT) return true
        if (c.startsWith("password ") || c == "password") return true
        if (c.startsWith("set guest.password ") || c.startsWith("set wifi.clear")) return true
        if (c.startsWith("set prv.key ")) return true
        // radio changes can cut the repeater off the mesh; `tempradio off` only restores the saved ones
        if (c.startsWith("set radio ") || c.startsWith("set freq ") || c.startsWith("set tx ")) return true
        if (c == "tempradio" || (c.startsWith("tempradio ") && c != "tempradio off")) return true
        if (c == "start ota") return true
        return false
    }

    /** The new password of a `password <new>` command (tag prefix removed), or null for any other command. */
    fun newPasswordOf(command: String): String? {
        val trimmed = TAG_PREFIX.replaceFirst(command.trim(), "").trimStart()
        if (!trimmed.lowercase(Locale.ROOT).startsWith("password ")) return null
        return trimmed.substring("password ".length).trim().ifEmpty { null }
    }

    /** True when the firmware executes the command immediately without returning a CLI reply. */
    fun expectsNoReply(command: String): Boolean {
        val c = normalizeCommand(command)
        return c in NO_REPLY_EXACT
    }

    /** Returns up to [limit] command suggestions matching [input]. */
    fun suggestions(input: String, isCompanion: Boolean, limit: Int = 6): List<String> {
        val trimmed = input.trimStart()
        if (trimmed.isEmpty()) return emptyList()
        val lower = trimmed.lowercase(Locale.ROOT)
        if (!isCompanion) {
            if (lower.startsWith("get ")) {
                val prefix = lower.removePrefix("get ")
                return (RepeaterBackup.KEYS + RepeaterBackup.OTA)
                    .filter { it.startsWith(prefix) && "get $it" != lower }
                    .take(limit)
                    .map { "get $it" }
            }
            if (lower.startsWith("set ")) {
                val rest = lower.removePrefix("set ")
                if (' ' !in rest) {
                    return (RepeaterBackup.KEYS + RepeaterBackup.OTA)
                        .filter { it.startsWith(rest) }
                        .take(limit)
                        .map { "set $it " }
                }
                return emptyList()
            }
        }
        val base = if (isCompanion) COMPANION_BASE_COMMANDS else REPEATER_BASE_COMMANDS
        return base.filter { it.startsWith(lower) && it.trim() != lower.trim() }.take(limit)
    }

    /** True when the command embeds a secret value (so it should not be saved to history or snippets). */
    fun isSecretWriteCommand(command: String): Boolean {
        val c = normalizeCommand(command)
        return c.startsWith("password ") ||
            c.startsWith("set guest.password ") ||
            c.startsWith("set wifi.pwd ") ||
            c.startsWith("set prv.key ") ||
            c.startsWith("set bridge.secret ") ||
            c.startsWith("set pin ") ||
            SECRET_SET.containsMatchIn(c)
    }

    /** Any `set <key> <value>` whose key names a password, secret or private key (e.g. mqtt.password). */
    private val SECRET_SET = Regex("^set [a-z0-9._-]*(password|passwd|pwd|secret|prv|private)[a-z0-9._-]* ")

    /** True when the command's reply contains a secret value that must be redacted in exported transcripts. */
    fun isSecretReadCommand(command: String): Boolean {
        val c = normalizeCommand(command)
        return c.startsWith("get guest.password") ||
            c.startsWith("get wifi.pwd") ||
            c.startsWith("get prv.key") ||
            c.startsWith("get bridge.secret")
    }

    private val REDACT_RULES = listOf(
        Regex("""(?i)(password[ \t]+now:[ \t]*)[^\r\n]+""") to "$1[REDACTED]",
        Regex("""(?i)(pin[ \t]+is[ \t]+now[ \t]+)[^\r\n]+""") to "$1[REDACTED]",
        Regex("""(?i)(\bpassword[ \t]+)[^\r\n]+""") to "$1[REDACTED]",
        Regex("""(?i)(\bguest\.password[ \t]+)[^\r\n]+""") to "$1[REDACTED]",
        Regex("""(?i)(\bwifi\.pwd[ \t]+)[^\r\n]+""") to "$1[REDACTED]",
        Regex("""(?i)(\bprv\.key[ \t]+)[^\r\n]+""") to "$1[REDACTED]",
        Regex("""(?i)(\bbridge\.secret[ \t]+)[^\r\n]+""") to "$1[REDACTED]",
        Regex("""(?i)(\bset[ \t]+pin[ \t]+)[^\r\n]+""") to "$1[REDACTED]",
        Regex("""(?i)(Authorization:[ \t]*Bearer[ \t]+)\S+""") to "$1[REDACTED]",
        Regex("""(?i)(Bearer[ \t]+)[A-Za-z0-9_\-\.]{8,}""") to "$1[REDACTED]",
        Regex("""\bgh[pousr]_[A-Za-z0-9_]{10,}\b""") to "[REDACTED_TOKEN]",
        Regex("""\bgithub_pat_[A-Za-z0-9_]{10,}\b""") to "[REDACTED_TOKEN]",
    )

    /** Strips admin/guest/Wi-Fi/bridge passwords, private keys, BLE PINs, and GitHub tokens from logs or diagnostics. */
    fun redactSecrets(text: String): String {
        var out = text
        for ((regex, replacement) in REDACT_RULES) {
            out = regex.replace(out, replacement)
        }
        return out
    }

    /**
     * Redacts both inline secrets and reply lines immediately following secret-reading commands
     * (`get guest.password`, `get wifi.pwd`, `get prv.key`, `get bridge.secret`).
     * Each entry is `(isCommand, text)`.
     */
    fun redactCliEntries(entries: List<Pair<Boolean, String>>): List<String> {
        val out = ArrayList<String>(entries.size)
        var hideNextReply = false
        for ((isCmd, text) in entries) {
            if (isCmd) {
                hideNextReply = isSecretReadCommand(text)
                out.add(redactSecrets(text))
            } else if (hideNextReply) {
                hideNextReply = false
                out.add("[REDACTED]")
            } else {
                out.add(redactSecrets(text))
            }
        }
        return out
    }
}

object TelemetryCsv {
    private val ISO_FMT = DateTimeFormatter.ISO_INSTANT

    /** Exports one telemetry [HistorySeries] as RFC-4180 CSV with UTC ISO-8601 timestamps. */
    fun export(deviceName: String, fieldName: String, unit: String, series: HistorySeries): String {
        val sb = StringBuilder()
        sb.append("# device=").append(sanitizeComment(deviceName))
            .append(", metric=").append(sanitizeComment(fieldName))
            .append(", ring=").append(if (series.ring == HistorySeries.RING_DAY) "24h" else "7d")
            .append('\n')
        sb.append("index,timestamp_utc,value,unit\n")
        for ((i, raw) in series.values.withIndex()) {
            if (raw == null) continue
            val ts = series.timeOf(i)?.let { ISO_FMT.format(Instant.ofEpochSecond(it).atOffset(ZoneOffset.UTC)) } ?: ""
            val value = formatDecimal(series.field, raw)
            sb.append(i).append(',')
                .append(csvCell(ts)).append(',')
                .append(value).append(',')
                .append(csvCell(unit)).append('\n')
        }
        return sb.toString()
    }

    private fun formatDecimal(field: Int, raw: Int): String {
        val d = HistorySeries.display(field, raw)
        return when (field) {
            HistorySeries.F_BATT_MV -> String.format(Locale.ROOT, "%.3f", d)
            HistorySeries.F_TEMP_DC, HistorySeries.F_PRESSURE -> String.format(Locale.ROOT, "%.1f", d)
            else -> d.toInt().toString()
        }
    }

    private fun sanitizeComment(s: String) = s.replace('\n', ' ').replace('\r', ' ').trim()

    private fun csvCell(s: String): String =
        if (s.any { it == ',' || it == '"' || it == '\n' || it == '\r' }) "\"${s.replace("\"", "\"\"")}\"" else s
}

object DiagnosticReport {
    fun build(
        appVersion: String,
        versionCode: Int,
        isGroupApk: Boolean,
        androidVersion: String,
        deviceModel: String,
        companionName: String,
        companionModel: String,
        companionVersion: String,
        companionOta: Boolean,
        bleLastError: String,
        repeaterName: String?,
        repeaterIdent: String?,
        logs: List<String>,
        cliHistory: List<String>,
    ): String {
        val sb = StringBuilder()
        sb.append("=== MeshCore Service Diagnostic Report ===\n")
        sb.append("App: v").append(appVersion).append(" (").append(versionCode).append(")")
            .append(if (isGroupApk) " [Group]\n" else " [Public]\n")
        sb.append("Device: ").append(deviceModel).append(" (Android ").append(androidVersion).append(")\n")
        if (companionName.isNotEmpty() || companionModel.isNotEmpty()) {
            sb.append("Companion: ").append(companionName.ifEmpty { "-" })
                .append(" | ").append(companionModel.ifEmpty { "-" })
                .append(" ").append(companionVersion.ifEmpty { "" })
                .append(" | customOta=").append(companionOta).append('\n')
        }
        if (bleLastError.isNotEmpty()) {
            sb.append("Last BLE error: ").append(bleLastError).append('\n')
        }
        if (!repeaterName.isNullOrEmpty()) {
            sb.append("Repeater: ").append(repeaterName).append('\n')
        }
        if (!repeaterIdent.isNullOrEmpty()) {
            sb.append("Repeater firmware: ").append(repeaterIdent).append('\n')
        }
        if (logs.isNotEmpty()) {
            sb.append("\n--- Operation Log (last ").append(minOf(logs.size, 80)).append(") ---\n")
            for (line in logs.takeLast(80)) sb.append(line).append('\n')
        }
        if (cliHistory.isNotEmpty()) {
            // Redact across the entire history BEFORE taking last 50 so a secret-read command at index N-51
            // still redacts its reply at index N-50.
            var hideNextReply = false
            val redactedHistory = ArrayList<String>(cliHistory.size)
            for (line in cliHistory) {
                val cmdBody = line.removePrefix("[COMMAND] ").takeIf { line.startsWith("[COMMAND] ") }
                when {
                    cmdBody != null -> {
                        hideNextReply = CliHelper.isSecretReadCommand(cmdBody)
                        redactedHistory.add(line)
                    }
                    hideNextReply && line.startsWith("[REPLY]") -> {
                        hideNextReply = false
                        redactedHistory.add("[REPLY] [REDACTED]")
                    }
                    else -> {
                        hideNextReply = false
                        redactedHistory.add(line)
                    }
                }
            }
            val tail = redactedHistory.takeLast(50)
            sb.append("\n--- CLI Transcript (last ").append(tail.size).append(") ---\n")
            for (line in tail) sb.append(line).append('\n')
        }
        return CliHelper.redactSecrets(sb.toString())
    }
}
