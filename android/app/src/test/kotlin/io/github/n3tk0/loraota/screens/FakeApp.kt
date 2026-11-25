package io.github.n3tk0.loraota.screens

import android.app.Application
import android.content.Context
import android.content.SharedPreferences
import java.io.File

/** SharedPreferences kept in memory only (nothing is written to disk). */
class MemoryPrefs(initial: Map<String, Any?> = emptyMap()) : SharedPreferences {
    private val map = LinkedHashMap<String, Any?>(initial)

    override fun getAll(): MutableMap<String, *> = LinkedHashMap(map)
    override fun getString(key: String?, defValue: String?) = map[key] as? String ?: defValue
    @Suppress("UNCHECKED_CAST")
    override fun getStringSet(key: String?, defValues: MutableSet<String>?) = map[key] as? MutableSet<String> ?: defValues
    override fun getInt(key: String?, defValue: Int) = map[key] as? Int ?: defValue
    override fun getLong(key: String?, defValue: Long) = map[key] as? Long ?: defValue
    override fun getFloat(key: String?, defValue: Float) = map[key] as? Float ?: defValue
    override fun getBoolean(key: String?, defValue: Boolean) = map[key] as? Boolean ?: defValue
    override fun contains(key: String?) = map.containsKey(key)
    override fun registerOnSharedPreferenceChangeListener(l: SharedPreferences.OnSharedPreferenceChangeListener?) {}
    override fun unregisterOnSharedPreferenceChangeListener(l: SharedPreferences.OnSharedPreferenceChangeListener?) {}

    override fun edit(): SharedPreferences.Editor = object : SharedPreferences.Editor {
        private val pending = LinkedHashMap<String, Any?>()
        private val removed = HashSet<String>()
        private var clear = false
        override fun putString(key: String, value: String?) = apply { pending[key] = value }
        override fun putStringSet(key: String, values: MutableSet<String>?) = apply { pending[key] = values }
        override fun putInt(key: String, value: Int) = apply { pending[key] = value }
        override fun putLong(key: String, value: Long) = apply { pending[key] = value }
        override fun putFloat(key: String, value: Float) = apply { pending[key] = value }
        override fun putBoolean(key: String, value: Boolean) = apply { pending[key] = value }
        override fun remove(key: String) = apply { removed.add(key) }
        override fun clear() = apply { clear = true }
        override fun commit(): Boolean { apply(); return true }
        override fun apply() {
            if (clear) map.clear()
            for (k in removed) map.remove(k)
            map.putAll(pending)
        }
    }
}

/**
 * An Application for the screenshot tests: a temp directory as filesDir and in-memory preferences, so a
 * real [io.github.n3tk0.loraota.ui.AppViewModel] can be built without a device.
 */
class FakeApp(base: Context, private val dir: File, private val prefs: Map<String, Map<String, Any?>>) : Application() {
    private val stores = HashMap<String, MemoryPrefs>()

    init { attachBaseContext(base) }

    override fun getApplicationContext(): Context = this
    override fun getFilesDir(): File = dir
    override fun getCacheDir(): File = File(dir, "cache").also { it.mkdirs() }
    override fun getSharedPreferences(name: String, mode: Int): SharedPreferences =
        stores.getOrPut(name) { MemoryPrefs(prefs[name].orEmpty()) }
    override fun getSystemService(name: String): Any? = null
}
