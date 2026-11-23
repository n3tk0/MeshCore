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
    private val DANGEROUS_EXACT = setOf("reboot", "poweroff", "shutdown", "clkreboot", "erase", "clear stats")
    private val NO_REPLY_EXACT = setOf("reboot", "poweroff", "shutdown", "clkreboot")

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

    /** True when the command restarts, shuts down, erases, resets stats, or changes passwords. */
    fun isDangerous(command: String): Boolean {
        val c = command.trim().lowercase(Locale.ROOT)
        if (c.isEmpty()) return false
        if (c in DANGEROUS_EXACT) return true
        if (c.startsWith("password ") || c == "password") return true
        if (c.startsWith("set guest.password ") || c.startsWith("set wifi.clear")) return true
        return false
    }

    /** True when the firmware executes the command immediately without returning a CLI reply. */
    fun expectsNoReply(command: String): Boolean {
        val c = command.trim().lowercase(Locale.ROOT)
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

    private val REDACT_RULES = listOf(
        Regex("""(?i)(password\s+now:\s*)\S+""") to "$1[REDACTED]",
        Regex("""(?i)(\bpassword\s+)\S+""") to "$1[REDACTED]",
        Regex("""(?i)(\bguest\.password\s+)\S+""") to "$1[REDACTED]",
        Regex("""(?i)(\bwifi\.pwd\s+)\S+""") to "$1[REDACTED]",
        Regex("""(?i)(Authorization:\s*Bearer\s+)\S+""") to "$1[REDACTED]",
        Regex("""(?i)(Bearer\s+)[A-Za-z0-9_\-\.]{8,}""") to "$1[REDACTED]",
        Regex("""\bgh[pousr]_[A-Za-z0-9_]{10,}\b""") to "[REDACTED_TOKEN]",
        Regex("""\bgithub_pat_[A-Za-z0-9_]{10,}\b""") to "[REDACTED_TOKEN]",
    )

    /** Strips admin/guest/Wi-Fi passwords and GitHub tokens from logs or shared diagnostics. */
    fun redactSecrets(text: String): String {
        var out = text
        for ((regex, replacement) in REDACT_RULES) {
            out = regex.replace(out, replacement)
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
        sb.append("=== LoRa OTA Diagnostic Report ===\n")
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
            sb.append("\n--- CLI Transcript (last ").append(minOf(cliHistory.size, 50)).append(") ---\n")
            for (line in cliHistory.takeLast(50)) sb.append(line).append('\n')
        }
        return CliHelper.redactSecrets(sb.toString())
    }
}
