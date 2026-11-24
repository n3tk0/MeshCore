package io.github.n3tk0.loraota.tools

import io.github.n3tk0.loraota.backup.Backup
import io.github.n3tk0.loraota.backup.DeviceKind
import io.github.n3tk0.loraota.ota.HealthCheck
import io.github.n3tk0.loraota.ota.HealthSeverity
import io.github.n3tk0.loraota.ota.HealthStore
import io.github.n3tk0.loraota.ota.SelfTest
import io.github.n3tk0.loraota.ui.BatteryChem

/** How a repeater did at its last check (the same three colors as the Health Sweep). */
enum class KnownHealth { OK, WARN, DANGER, UNKNOWN }

/** One line of the start screen's "My repeaters": everything known about it without a connection. */
data class KnownRepeaterRow(
    val key: String,
    val name: String,
    val pinned: Boolean,
    val batteryV: Double?,
    /** Only when the battery chemistry of this repeater is known: the percent from its volts. */
    val batteryPercent: Int?,
    val health: KnownHealth,
    /** The firmware of its latest backup that says one; "" when unknown. */
    val firmware: String,
    /** When it was last checked (a self test, else a backup); null: never. */
    val lastCheckedMs: Long?,
    val hasBackup: Boolean,
    val hasPassword: Boolean,
)

object KnownRepeaters {
    /** The battery percent for [volts] at rest with the chemistry's own empty and full voltage. */
    fun percent(volts: Double, chem: BatteryChem): Int =
        (((volts - chem.emptyV) / (chem.fullV - chem.emptyV)).coerceIn(0.0, 1.0) * 100.0).let { Math.round(it).toInt() }

    fun healthOf(line: String): KnownHealth {
        val parsed = SelfTest.parse(line) ?: return KnownHealth.UNKNOWN
        val problems = HealthCheck.problems(HealthCheck.warnings(parsed, null))
        return when {
            problems.any { it.severity == HealthSeverity.DANGER } -> KnownHealth.DANGER
            problems.any { it.severity == HealthSeverity.WARN } || !parsed.ok -> KnownHealth.WARN
            else -> KnownHealth.OK
        }
    }

    /**
     * Repeaters known from what is kept on the phone: self tests, repeater backups, pins, kept passwords
     * and battery choices. A name alone (the companion's contacts) is not enough: it may be anything.
     * Pinned first, then the most recently checked, then by name.
     */
    fun build(
        health: Map<String, List<HealthStore.Record>>,
        backups: List<Backup>,
        pinned: Collection<String>,
        names: Map<String, String>,
        chems: Map<String, BatteryChem>,
        passwordKeys: Collection<String>,
    ): List<KnownRepeaterRow> {
        val repBackups = backups.filter { it.kind == DeviceKind.REPEATER && it.deviceKey.isNotEmpty() }
            .groupBy { it.deviceKey }
        // only self tests count as proof of a repeater (the companion keeps other health lines)
        val tested = health.filter { (_, l) -> l.lastOrNull()?.let { SelfTest.parse(it.line) != null } == true }
        val keys = LinkedHashSet<String>().apply {
            addAll(tested.keys); addAll(repBackups.keys); addAll(pinned); addAll(passwordKeys); addAll(chems.keys)
        }.filter { it.isNotEmpty() }
        val pinnedSet = pinned.toSet()
        val pwSet = passwordKeys.toSet()
        val rows = keys.map { key ->
            val latest = tested[key]?.lastOrNull()
            val parsed = latest?.let { SelfTest.parse(it.line) }
            val mine = repBackups[key].orEmpty().sortedByDescending { it.time }
            val volts = parsed?.batteryMv?.let { it / 1000.0 }
            val chem = chems[key]
            val checked = listOfNotNull(latest?.time, mine.firstOrNull()?.time).maxOrNull()
            KnownRepeaterRow(
                key = key,
                name = names[key]?.takeIf { it.isNotBlank() } ?: mine.firstOrNull { it.deviceName.isNotBlank() }?.deviceName
                    ?: key.take(8),
                pinned = key in pinnedSet,
                batteryV = volts,
                batteryPercent = if (volts != null && chem != null) percent(volts, chem) else null,
                health = latest?.let { healthOf(it.line) } ?: KnownHealth.UNKNOWN,
                firmware = mine.firstOrNull { it.firmware.isNotBlank() }?.firmware.orEmpty(),
                lastCheckedMs = checked,
                hasBackup = mine.isNotEmpty(),
                hasPassword = key in pwSet,
            )
        }
        return rows.sortedWith(
            compareByDescending<KnownRepeaterRow> { it.pinned }
                .thenByDescending { it.lastCheckedMs ?: Long.MIN_VALUE }
                .thenBy { it.name.lowercase() },
        )
    }
}
