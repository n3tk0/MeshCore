package io.github.n3tk0.loraota.ui

import android.content.Context
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import io.github.n3tk0.loraota.backup.Backup
import io.github.n3tk0.loraota.ota.FirmwareFile
import io.github.n3tk0.loraota.ota.HealthStore
import io.github.n3tk0.loraota.tools.CliHelper
import io.github.n3tk0.loraota.tools.CliHistory
import io.github.n3tk0.loraota.tools.ConfigProfile
import io.github.n3tk0.loraota.tools.ConfigProfileLogic
import io.github.n3tk0.loraota.tools.NeighborEntry
import io.github.n3tk0.loraota.tools.PingSample
import org.json.JSONArray
import org.json.JSONObject

/**
 * Persists user-pinned custom CLI snippets (quick commands) in SharedPreferences.
 */
class CliSnippetStore(ctx: Context) {
    private val prefs = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    fun load(): List<String> {
        val raw = prefs.getString(KEY, null) ?: return emptyList()
        return runCatching {
            val arr = JSONArray(raw)
            (0 until arr.length())
                .mapNotNull { arr.optString(it).takeIf { s -> s.isNotBlank() && !CliHelper.isSecretWriteCommand(s) } }
        }.getOrDefault(emptyList())
    }

    fun save(list: List<String>) {
        val arr = JSONArray()
        for (s in list) {
            if (!CliHelper.isSecretWriteCommand(s)) arr.put(s)
        }
        prefs.edit().putString(KEY, arr.toString()).apply()
    }

    private companion object {
        const val PREFS = "cli_snippets"
        const val KEY = "items"
    }
}

/**
 * Persists the last sent CLI commands per console in SharedPreferences (secrets are never stored).
 */
class CliHistoryStore(ctx: Context) {
    private val prefs = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    fun load(isCompanion: Boolean): List<String> {
        val raw = prefs.getString(key(isCompanion), null) ?: return emptyList()
        return runCatching {
            val arr = JSONArray(raw)
            (0 until arr.length())
                .mapNotNull { arr.optString(it).takeIf { s -> s.isNotBlank() && !CliHelper.isSecretWriteCommand(s) } }
                .take(CliHistory.MAX)
        }.getOrDefault(emptyList())
    }

    fun save(isCompanion: Boolean, list: List<String>) {
        val arr = JSONArray()
        for (s in list) if (!CliHelper.isSecretWriteCommand(s)) arr.put(s)
        prefs.edit().putString(key(isCompanion), arr.toString()).apply()
    }

    private fun key(isCompanion: Boolean) = if (isCompanion) "companion" else "repeater"

    private companion object {
        const val PREFS = "cli_history"
    }
}

/**
 * Persists named repeater configuration profiles (radio/tx/advert/loop templates without identity/passwords).
 */
class ConfigProfileStore(ctx: Context) {
    private val prefs = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    fun load(): List<ConfigProfile> {
        val raw = prefs.getString(KEY, null) ?: return emptyList()
        return runCatching {
            val arr = JSONArray(raw)
            val out = ArrayList<ConfigProfile>(arr.length())
            for (i in 0 until arr.length()) {
                val o = arr.getJSONObject(i)
                val name = o.optString("name").trim()
                val sObj = o.optJSONObject("settings")
                if (name.isNotEmpty() && sObj != null) {
                    val map = LinkedHashMap<String, String>()
                    for (k in sObj.keys()) map[k] = sObj.optString(k)
                    val clean = ConfigProfileLogic.extractTemplateSettings(map)
                    if (clean.isNotEmpty()) out.add(ConfigProfile(name, clean))
                }
            }
            out
        }.getOrDefault(emptyList())
    }

    fun save(list: List<ConfigProfile>) {
        val arr = JSONArray()
        for (p in list) {
            val sObj = JSONObject()
            for ((k, v) in ConfigProfileLogic.extractTemplateSettings(p.settings)) sObj.put(k, v)
            arr.put(JSONObject().put("name", p.name).put("settings", sObj))
        }
        prefs.edit().putString(KEY, arr.toString()).apply()
    }

    private companion object {
        const val PREFS = "config_profiles"
        const val KEY = "items"
    }
}

/**
 * Persists last-known neighbor lists per repeater (`observerKeyHex -> List<NeighborEntry>`)
 * so the Offline Mesh Topology / SNR asymmetry matrix works even when disconnected.
 */
class ObservedNeighborsStore(ctx: Context) {
    private val prefs = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    fun load(): Map<String, List<NeighborEntry>> {
        val raw = prefs.getString(KEY, null) ?: return emptyMap()
        return runCatching {
            val root = JSONObject(raw)
            val out = LinkedHashMap<String, List<NeighborEntry>>()
            for (k in root.keys()) {
                val arr = root.optJSONArray(k) ?: continue
                val list = ArrayList<NeighborEntry>(arr.length())
                for (i in 0 until arr.length()) {
                    val o = arr.getJSONObject(i)
                    list.add(
                        NeighborEntry(
                            prefixHex = o.optString("hex"),
                            secsAgo = o.optLong("secs", 0L),
                            snrDb = o.optDouble("snr", 0.0).toFloat(),
                            resolvedName = o.optString("name").takeIf { it.isNotBlank() },
                        )
                    )
                }
                if (list.isNotEmpty()) out[k] = list
            }
            out
        }.getOrDefault(emptyMap())
    }

    fun save(map: Map<String, List<NeighborEntry>>) {
        val root = JSONObject()
        for ((k, list) in map) {
            val arr = JSONArray()
            for (n in list) {
                arr.put(
                    JSONObject()
                        .put("hex", n.prefixHex)
                        .put("secs", n.secsAgo)
                        .put("snr", n.snrDb.toDouble())
                        .put("name", n.resolvedName ?: "")
                )
            }
            root.put(k, arr)
        }
        prefs.edit().putString(KEY, root.toString()).apply()
    }

    private companion object {
        const val PREFS = "observed_neighbors"
        const val KEY = "by_node"
    }
}

/**
 * Encapsulates state for interactive CLI console, quick actions, neighbors, live link ping test,
 * config profiles, and visual repeater settings, keeping AppViewModel modular and easier to maintain.
 */
class RepeaterToolsState(ctx: Context) {
    private val snippetStore = CliSnippetStore(ctx)
    private val profileStore = ConfigProfileStore(ctx)
    private val historyStore = CliHistoryStore(ctx)

    val repeaterCliLog = mutableStateListOf<CliLine>()
    val companionCliLog = mutableStateListOf<CliLine>()
    val repeaterCliHistory = mutableStateListOf<String>().apply { addAll(historyStore.load(false)) }
    val companionCliHistory = mutableStateListOf<String>().apply { addAll(historyStore.load(true)) }

    fun cliHistory(isCompanion: Boolean) = if (isCompanion) companionCliHistory else repeaterCliHistory

    /** Remembers a sent command (newest first, 50 per console; passwords and keys are skipped). */
    fun addCliHistory(isCompanion: Boolean, cmd: String) {
        val list = cliHistory(isCompanion)
        if (CliHistory.add(list, cmd)) historyStore.save(isCompanion, list)
    }
    val customCliSnippets = mutableStateListOf<String>().apply { addAll(snippetStore.load()) }
    val configProfiles = mutableStateListOf<ConfigProfile>().apply { addAll(profileStore.load()) }

    /** The Tools tab's section (0 CLI, 1 settings, 2 backup) and the Overview's neighbors card: kept here, not in a composable. */
    var toolSection by mutableIntStateOf(0)
    var neighborsExpanded by mutableStateOf(false)

    /** What is typed in each CLI console (kept when the tab is left). */
    var repeaterCliInput by mutableStateOf("")
    var companionCliInput by mutableStateOf("")

    val neighbors = mutableStateListOf<NeighborEntry>()
    var neighborsLoaded by mutableStateOf(false)
    var neighborsError by mutableStateOf<String?>(null)
    var lastClockReply by mutableStateOf<String?>(null)
    /** Set after a remote reboot command so Overview/Tools offers 1-tap re-login with the saved password. */
    var needsRelogin by mutableStateOf(false)

    // Live link ping / walk-test state
    val pingSamples = mutableStateListOf<PingSample>()
    var pingRunning by mutableStateOf(false)

    val settings = mutableStateMapOf<String, String>()
    val settingsDraft = mutableStateMapOf<String, String>()
    var settingsLoaded by mutableStateOf(false)
    var settingsProgress by mutableStateOf<Pair<Int, Int>?>(null)

    fun addSnippet(raw: String): Boolean {
        val cmd = raw.trim()
        if (cmd.isEmpty() || cmd in customCliSnippets || CliHelper.isSecretWriteCommand(cmd)) return false
        customCliSnippets.add(cmd)
        while (customCliSnippets.size > 16) customCliSnippets.removeAt(0)
        snippetStore.save(customCliSnippets)
        return true
    }

    fun removeSnippet(cmd: String) {
        if (customCliSnippets.remove(cmd)) snippetStore.save(customCliSnippets)
    }

    fun replaceSnippets(items: List<String>) {
        customCliSnippets.clear()
        for (s in items) {
            val c = s.trim()
            if (c.isNotEmpty() && c !in customCliSnippets && !CliHelper.isSecretWriteCommand(c)) {
                customCliSnippets.add(c)
            }
        }
        while (customCliSnippets.size > 16) customCliSnippets.removeAt(0)
        snippetStore.save(customCliSnippets)
    }

    fun saveProfile(rawName: String, sourceSettings: Map<String, String>): Boolean {
        val name = rawName.trim()
        val clean = ConfigProfileLogic.extractTemplateSettings(sourceSettings)
        if (name.isEmpty() || clean.isEmpty()) return false
        val idx = configProfiles.indexOfFirst { it.name.equals(name, ignoreCase = true) }
        val profile = ConfigProfile(name, clean)
        if (idx >= 0) configProfiles[idx] = profile else configProfiles.add(profile)
        while (configProfiles.size > 20) configProfiles.removeAt(0)
        profileStore.save(configProfiles)
        return true
    }

    fun deleteProfile(name: String) {
        if (configProfiles.removeAll { it.name == name }) {
            profileStore.save(configProfiles)
        }
    }

    fun mergeProfiles(imported: List<ConfigProfile>) {
        for (p in imported) {
            val clean = ConfigProfileLogic.extractTemplateSettings(p.settings)
            if (p.name.isBlank() || clean.isEmpty()) continue
            val idx = configProfiles.indexOfFirst { it.name.equals(p.name, ignoreCase = true) }
            val next = ConfigProfile(p.name.trim(), clean)
            if (idx >= 0) configProfiles[idx] = next else configProfiles.add(next)
        }
        while (configProfiles.size > 20) configProfiles.removeAt(0)
        profileStore.save(configProfiles)
    }

    fun appendCli(isCompanion: Boolean, text: String, kind: CliLineKind, durationMs: Long? = null) {
        val list = if (isCompanion) companionCliLog else repeaterCliLog
        list.add(CliLine(System.currentTimeMillis(), text, kind, durationMs))
        while (list.size > 200) list.removeAt(0)
    }

    fun clearForNewRepeater() {
        toolSection = 0
        neighborsExpanded = false
        neighbors.clear()
        neighborsLoaded = false
        neighborsError = null
        lastClockReply = null
        needsRelogin = false
        pingSamples.clear()
        pingRunning = false
        settings.clear()
        settingsDraft.clear()
        settingsLoaded = false
        settingsProgress = null
        repeaterCliLog.clear()
    }
}

/** One locally cached firmware entry shown in the Offline -> Firmware tab. */
data class CachedFirmwareItem(
    val firmware: FirmwareFile,
    val hasCachedOta: Boolean,
    val hasCachedDfu: Boolean,
    /** The cached LoRa-OTA .bin is an ESP32 app image (Wi-Fi flashable even with no repeater logged in). */
    val otaIsEsp32: Boolean = false,
)

/**
 * Encapsulates fleet pins, cached health badges, Health Sweep state, Offline viewer state,
 * observed mesh neighbors topology, and Field Preparation board selection state.
 */
class FleetToolsState(ctx: Context) {
    private val pinPrefs = ctx.getSharedPreferences("pins", Context.MODE_PRIVATE)
    private val deviceNamePrefs = ctx.getSharedPreferences("device_names", Context.MODE_PRIVATE)
    private val neighborsStore = ObservedNeighborsStore(ctx)

    val pinnedKeys = mutableStateListOf<String>().apply { addAll(pinPrefs.all.keys) }
    val fleetHealth = mutableStateMapOf<String, HealthStore.Record>()
    val knownDeviceNames = mutableStateMapOf<String, String>().apply {
        for ((k, v) in deviceNamePrefs.all) if (v is String) put(k, v)
    }
    val observedNeighbors = mutableStateMapOf<String, List<NeighborEntry>>().apply {
        putAll(neighborsStore.load())
    }

    val sweepPicked = mutableStateMapOf<String, Boolean>()
    val sweepEntries = mutableStateListOf<SweepEntry>()
    var sweepRunning by mutableStateOf(false)
    var sweepFinished by mutableStateOf(false)
    /** When true, Health Sweep also syncs each repeater's UTC clock from the phone during the sweep. */
    var sweepSyncClock by mutableStateOf(false)

    val offlineHealth = mutableStateMapOf<String, List<HealthStore.Record>>()
    val offlineBackups = mutableStateListOf<Backup>()
    val offlineFirmwares = mutableStateListOf<CachedFirmwareItem>()
    var offlineLoading by mutableStateOf(false)
    /** Selected device role ("repeater", "companion", "room_server") for offline GitHub builds. */
    var offlineBuildRole by mutableStateOf("repeater")
    /** Rotation-safe holder for exporting a cached firmware file via SAF CreateDocument. */
    var exportingFirmware by mutableStateOf<Pair<FirmwareFile, Boolean>?>(null)
    /** Rotation-safe password for exporting/importing full encrypted workspace archives. */
    var workspaceArchivePassword by mutableStateOf("")
    /** Pending encrypted workspace JSON waiting for password prompt during import. */
    var pendingWorkspaceImportJson by mutableStateOf<String?>(null)

    // Field preparation board picker state
    var fieldPrepProgress by mutableStateOf<String?>(null)
    val fieldPrepCandidates = mutableStateListOf<FirmwareFile>()
    val fieldPrepPicked = mutableStateMapOf<String, Boolean>()
    val fieldPrepAlreadyCached = mutableStateMapOf<String, Boolean>()
    var showFieldPrepDialog by mutableStateOf(false)

    fun togglePin(keyHex: String) {
        if (keyHex in pinnedKeys) {
            pinnedKeys.remove(keyHex)
            pinPrefs.edit().remove(keyHex).apply()
        } else {
            pinnedKeys.add(keyHex)
            pinPrefs.edit().putBoolean(keyHex, true).apply()
        }
    }

    fun mergePins(keys: Collection<String>) {
        val editor = pinPrefs.edit()
        for (k in keys) {
            if (k.isNotEmpty() && k !in pinnedKeys) {
                pinnedKeys.add(k)
                editor.putBoolean(k, true)
            }
        }
        editor.apply()
    }

    fun recordObservedNeighbors(observerKeyHex: String, list: List<NeighborEntry>) {
        if (observerKeyHex.isEmpty()) return
        observedNeighbors[observerKeyHex] = list.toList()
        neighborsStore.save(observedNeighbors)
    }

    fun mergeObservedNeighbors(imported: Map<String, List<NeighborEntry>>) {
        if (imported.isEmpty()) return
        for ((k, v) in imported) {
            if (k.isNotEmpty() && v.isNotEmpty()) observedNeighbors[k] = v
        }
        neighborsStore.save(observedNeighbors)
    }

    fun rememberDeviceName(keyHex: String, name: String) {
        if (keyHex.isEmpty() || name.isBlank()) return
        if (knownDeviceNames[keyHex] != name) {
            knownDeviceNames[keyHex] = name
            deviceNamePrefs.edit().putString(keyHex, name).apply()
        }
    }

    fun rememberDeviceNames(pairs: Map<String, String>) {
        val editor = deviceNamePrefs.edit()
        var changed = false
        for ((k, name) in pairs) {
            if (k.isNotEmpty() && name.isNotBlank() && knownDeviceNames[k] != name) {
                knownDeviceNames[k] = name
                editor.putString(k, name)
                changed = true
            }
        }
        if (changed) editor.apply()
    }
}

