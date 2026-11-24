package io.github.n3tk0.loraota.tools

import io.github.n3tk0.loraota.ota.HealthCheck
import io.github.n3tk0.loraota.ota.HealthSeverity
import io.github.n3tk0.loraota.ota.SelfTest
import java.util.Locale

/** Client-side validation errors matching `src/helpers/CommonCLI.cpp` firmware rules. */
enum class SettingValidationError {
    NAME_EMPTY,
    NAME_TOO_LONG,
    NAME_BAD_CHARS,
    TX_RANGE,
    LAT_RANGE,
    LON_RANGE,
    ADVERT_RANGE,
    FLOOD_ADVERT_RANGE,
    AF_RANGE,
}

object RepeaterSettingsValidator {
    private val FORBIDDEN_NAME_CHARS = setOf('[', ']', '\\', ':', ',', '?', '*')

    private val DECIMAL_KEYS = setOf("lat", "lon", "af")
    private val TRIM_KEYS = setOf("name", "tx", "lat", "lon", "advert.interval", "flood.advert.interval", "af")

    /** Normalizes decimal commas and outer whitespace before sending `set <key> <value>` to C `atof`/`atoi`. */
    fun normalizeForSave(key: String, rawValue: String): String = when (key) {
        in DECIMAL_KEYS -> rawValue.trim().replace(',', '.')
        in TRIM_KEYS -> rawValue.trim()
        else -> rawValue
    }

    /** Validates a single repeater setting before sending `set <key> <value>` over LoRa. */
    fun validate(key: String, rawValue: String): SettingValidationError? {
        val v = normalizeForSave(key, rawValue)
        return when (key) {
            "name" -> when {
                v.isEmpty() -> SettingValidationError.NAME_EMPTY
                v.toByteArray(Charsets.UTF_8).size > 31 -> SettingValidationError.NAME_TOO_LONG
                v.any { it in FORBIDDEN_NAME_CHARS } -> SettingValidationError.NAME_BAD_CHARS
                else -> null
            }
            "tx" -> {
                val n = v.toIntOrNull()
                if (n == null || n !in -9..30) SettingValidationError.TX_RANGE else null
            }
            "lat" -> {
                val d = v.toDoubleOrNull()
                if (d == null || d.isNaN() || d < -90.0 || d > 90.0) SettingValidationError.LAT_RANGE else null
            }
            "lon" -> {
                val d = v.toDoubleOrNull()
                if (d == null || d.isNaN() || d < -180.0 || d > 180.0) SettingValidationError.LON_RANGE else null
            }
            "advert.interval" -> {
                val n = v.toIntOrNull()
                if (n == null || (n != 0 && n !in 60..240)) SettingValidationError.ADVERT_RANGE else null
            }
            "flood.advert.interval" -> {
                val n = v.toIntOrNull()
                if (n == null || (n != 0 && n !in 3..168)) SettingValidationError.FLOOD_ADVERT_RANGE else null
            }
            "af" -> {
                val f = v.toFloatOrNull()
                if (f == null || f.isNaN() || f < 0f || f > 9f) SettingValidationError.AF_RANGE else null
            }
            else -> null
        }
    }

    /**
     * Validates `tempradio <freq>,<bw>,<sf>,<cr>,<mins>` parameters matching `CommonCLI.cpp:271-285`
     * (which parses via `Utils::parseTextParts` with default `,` separator).
     * Returns the ready CLI command string, or `null` if any parameter is out of bounds.
     */
    fun buildTempRadioCommand(
        freqStr: String,
        bwStr: String,
        sfStr: String,
        crStr: String,
        minsStr: String,
    ): String? {
        val freq = freqStr.trim().replace(',', '.').toFloatOrNull() ?: return null
        val bw = bwStr.trim().replace(',', '.').toFloatOrNull() ?: return null
        val sf = sfStr.trim().toIntOrNull() ?: return null
        val cr = crStr.trim().toIntOrNull() ?: return null
        val mins = minsStr.trim().toIntOrNull() ?: return null
        if (freq !in 150.0f..2500.0f) return null
        if (bw !in 7.0f..500.0f) return null
        if (sf !in 5..12) return null
        if (cr !in 5..8) return null
        if (mins !in 1..60) return null
        val bwFmt = String.format(Locale.ROOT, "%.3f", bw).trimEnd('0').let {
            if (it.endsWith('.')) "${it}0" else it
        }
        return String.format(Locale.ROOT, "tempradio %.3f,%s,%d,%d,%d", freq, bwFmt, sf, cr, mins)
    }
}

/** Fleet filter options shown in the repeaters list screen. */
enum class FleetFilter { ALL, DIRECT, ISSUES, PINNED }

object FleetFilterLogic {
    /** True when the repeater's latest self-test line has any WARN/DANGER severity or non-ok status. */
    fun hasHealthIssue(latestHealthLine: String?): Boolean {
        if (latestHealthLine.isNullOrBlank()) return false
        val parsed = SelfTest.parse(latestHealthLine) ?: return false
        if (!parsed.ok) return true
        val probs = HealthCheck.problems(HealthCheck.warnings(parsed, null))
        return probs.any { it.severity == HealthSeverity.DANGER || it.severity == HealthSeverity.WARN }
    }

    fun matches(
        filter: FleetFilter,
        hops: Int?,
        isPinned: Boolean,
        latestHealthLine: String?,
    ): Boolean = when (filter) {
        FleetFilter.ALL -> true
        FleetFilter.DIRECT -> hops == 0
        FleetFilter.ISSUES -> hasHealthIssue(latestHealthLine)
        FleetFilter.PINNED -> isPinned
    }
}

/** Lightweight row model for formatting a shareable fleet Health Sweep report. */
data class SweepReportRow(
    val name: String,
    val statusLabel: String,
    val batteryText: String,
    val detail: String,
)

object SweepReportFormatter {
    /** Formats a concise plain-text report suitable for sharing in Telegram/Signal/email. */
    fun format(rows: List<SweepReportRow>, generatedAt: String): String {
        val sb = StringBuilder()
        sb.append("MeshCore Fleet Health Report (").append(generatedAt).append(")\n")
        sb.append("Total checked: ").append(rows.size).append("\n\n")
        for (r in rows) {
            sb.append("• ").append(r.name).append(" [").append(r.statusLabel).append("]")
            if (r.batteryText.isNotEmpty()) sb.append(" · ").append(r.batteryText)
            if (r.detail.isNotEmpty()) sb.append(" — ").append(r.detail)
            sb.append('\n')
        }
        return sb.toString().trimEnd()
    }
}
