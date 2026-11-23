package io.github.n3tk0.loraota.ui

import android.content.Context
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import io.github.n3tk0.loraota.backup.Backup
import io.github.n3tk0.loraota.ota.FirmwareFile
import io.github.n3tk0.loraota.ota.HealthStore
import io.github.n3tk0.loraota.tools.NeighborEntry
import org.json.JSONArray

/**
 * Persists user-pinned custom CLI snippets (quick commands) in SharedPreferences.
 */
class CliSnippetStore(ctx: Context) {
    private val prefs = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    fun load(): List<String> {
        val raw = prefs.getString(KEY, null) ?: return emptyList()
        return runCatching {
            val arr = JSONArray(raw)
            (0 until arr.length()).mapNotNull { arr.optString(it).takeIf { s -> s.isNotBlank() } }
        }.getOrDefault(emptyList())
    }

    fun save(list: List<String>) {
        val arr = JSONArray()
        for (s in list) arr.put(s)
        prefs.edit().putString(KEY, arr.toString()).apply()
    }

    private companion object {
        const val PREFS = "cli_snippets"
        const val KEY = "items"
    }
}

/**
 * Encapsulates state for interactive CLI console, quick actions, neighbors, and visual repeater settings,
 * keeping AppViewModel modular and easier to maintain.
 */
class RepeaterToolsState(ctx: Context) {
    private val snippetStore = CliSnippetStore(ctx)

    val repeaterCliLog = mutableStateListOf<CliLine>()
    val companionCliLog = mutableStateListOf<CliLine>()
    val cliCommandHistory = mutableStateListOf<String>()
    val customCliSnippets = mutableStateListOf<String>().apply { addAll(snippetStore.load()) }

    val neighbors = mutableStateListOf<NeighborEntry>()
    var neighborsLoaded by mutableStateOf(false)
    var neighborsError by mutableStateOf<String?>(null)
    var lastClockReply by mutableStateOf<String?>(null)
    /** Set after a remote reboot command so Overview/Tools offers 1-tap re-login with the saved password. */
    var needsRelogin by mutableStateOf(false)

    val settings = mutableStateMapOf<String, String>()
    val settingsDraft = mutableStateMapOf<String, String>()
    var settingsLoaded by mutableStateOf(false)
    var settingsProgress by mutableStateOf<Pair<Int, Int>?>(null)

    fun addSnippet(raw: String) {
        val cmd = raw.trim()
        if (cmd.isEmpty() || cmd in customCliSnippets) return
        customCliSnippets.add(cmd)
        while (customCliSnippets.size > 16) customCliSnippets.removeAt(0)
        snippetStore.save(customCliSnippets)
    }

    fun removeSnippet(cmd: String) {
        if (customCliSnippets.remove(cmd)) snippetStore.save(customCliSnippets)
    }

    fun appendCli(isCompanion: Boolean, text: String, kind: CliLineKind, durationMs: Long? = null) {
        val list = if (isCompanion) companionCliLog else repeaterCliLog
        list.add(CliLine(System.currentTimeMillis(), text, kind, durationMs))
        while (list.size > 200) list.removeAt(0)
    }

    fun clearForNewRepeater() {
        neighbors.clear()
        neighborsLoaded = false
        neighborsError = null
        lastClockReply = null
        needsRelogin = false
        settings.clear()
        settingsDraft.clear()
        settingsLoaded = false
        settingsProgress = null
        repeaterCliLog.clear()
    }
}

/**
 * Encapsulates fleet pins, cached health badges, Health Sweep state, Offline viewer state,
 * and Field Preparation board selection state.
 */
class FleetToolsState(ctx: Context) {
    private val pinPrefs = ctx.getSharedPreferences("pins", Context.MODE_PRIVATE)
    private val deviceNamePrefs = ctx.getSharedPreferences("device_names", Context.MODE_PRIVATE)

    val pinnedKeys = mutableStateListOf<String>().apply { addAll(pinPrefs.all.keys) }
    val fleetHealth = mutableStateMapOf<String, HealthStore.Record>()
    val knownDeviceNames = mutableStateMapOf<String, String>().apply {
        for ((k, v) in deviceNamePrefs.all) if (v is String) put(k, v)
    }

    val sweepPicked = mutableStateMapOf<String, Boolean>()
    val sweepEntries = mutableStateListOf<SweepEntry>()
    var sweepRunning by mutableStateOf(false)
    var sweepFinished by mutableStateOf(false)

    val offlineHealth = mutableStateMapOf<String, List<HealthStore.Record>>()
    val offlineBackups = mutableStateListOf<Backup>()

    // Field preparation board picker state
    var fieldPrepProgress by mutableStateOf<String?>(null)
    val fieldPrepCandidates = mutableStateListOf<FirmwareFile>()
    val fieldPrepPicked = mutableStateMapOf<String, Boolean>()
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
