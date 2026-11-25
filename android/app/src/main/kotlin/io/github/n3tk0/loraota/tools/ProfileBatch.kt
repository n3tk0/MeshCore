package io.github.n3tk0.loraota.tools

import io.github.n3tk0.loraota.backup.RepeaterBackup

/** What applying one profile to one repeater would do: the keys to write (already normalized) and those left out. */
class ProfilePlan(
    /** key to the value to send with `set <key> <value>`, in the profile's order. */
    val changes: Map<String, String>,
    /** keys the repeater does not have (older firmware): skipped, not an error. */
    val unsupported: List<String>,
    /** keys whose value does not pass the settings editor's validation: not written. */
    val invalid: List<Pair<String, SettingValidationError>>,
) {
    val unchanged get() = changes.isEmpty() && invalid.isEmpty()
}

object ProfilePlanner {
    /**
     * The commands to bring [current] (what the repeater has now, by key) to [profile]: only keys that
     * differ, never an identity or secret key (name, GPS, passwords), and only through the same
     * normalizing and validation as the settings editor.
     */
    fun plan(profile: Map<String, String>, current: Map<String, String>): ProfilePlan {
        val changes = LinkedHashMap<String, String>()
        val unsupported = ArrayList<String>()
        val invalid = ArrayList<Pair<String, SettingValidationError>>()
        for ((k, raw) in ConfigProfileLogic.extractTemplateSettings(profile)) {
            if (k !in current) {
                unsupported.add(k)
                continue
            }
            val want = RepeaterSettingsValidator.normalizeForSave(k, raw)
            val have = RepeaterSettingsValidator.normalizeForSave(k, current.getValue(k))
            if (want == have) continue
            val err = RepeaterSettingsValidator.validate(k, raw)
            if (err != null) invalid.add(k to err) else changes[k] = want
        }
        return ProfilePlan(changes, unsupported, invalid)
    }

    fun needsRestart(plan: ProfilePlan) = plan.changes.keys.any { it in RepeaterBackup.NEEDS_RESTART }

    /** Rough time per repeater: login, backup, read, a few writes; each over LoRa. */
    const val SECONDS_PER_REPEATER = 120

    fun estimateMinutes(count: Int): Int = if (count <= 0) 0 else (count * SECONDS_PER_REPEATER + 59) / 60
}

/** How one repeater of a batch went. */
enum class BatchResult { PENDING, APPLIED, UNCHANGED, SKIPPED, FAILED }

class BatchEntry(
    val key: String,
    val name: String,
    var result: BatchResult = BatchResult.PENDING,
    var note: String = "",
    var keys: Int = 0,
)

/** Applying one profile to several repeaters, one after another; [retryFailed] puts the ones that did not go back in line. */
class ProfileBatchState(val profileName: String, val entries: List<BatchEntry>) {
    val next: BatchEntry? get() = entries.firstOrNull { it.result == BatchResult.PENDING }
    val finished get() = next == null
    val done get() = entries.count { it.result != BatchResult.PENDING }
    fun count(r: BatchResult) = entries.count { it.result == r }
    /** Failed, skipped, or not reached (after a Stop): what "retry" would run. */
    val retryable get() = count(BatchResult.FAILED) + count(BatchResult.SKIPPED) + count(BatchResult.PENDING)
    val appliedKeys get() = entries.filter { it.result == BatchResult.APPLIED }.sumOf { it.keys }

    fun retryFailed() {
        for (e in entries) if (e.result == BatchResult.FAILED || e.result == BatchResult.SKIPPED) {
            e.result = BatchResult.PENDING
            e.note = ""
            e.keys = 0
        }
    }

    /** A copy with the same entries, so that a screen holding it as state redraws. */
    fun refreshed() = ProfileBatchState(profileName, entries)
}
