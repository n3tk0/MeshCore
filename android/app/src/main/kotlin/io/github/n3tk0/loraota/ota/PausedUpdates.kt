package io.github.n3tk0.loraota.ota

import android.content.SharedPreferences

/**
 * The repeaters whose LoRa update the app paused or stopped in a way that can go on (the repeater keeps what it has),
 * by public key (hex). Kept on the phone, so the Overview can say it with no request over the air; cleared when the
 * update completes or is dropped.
 */
class PausedUpdates(private val prefs: SharedPreferences) {
    private companion object {
        const val KEY = "keys"
    }

    private fun all(): Set<String> = prefs.getStringSet(KEY, emptySet()).orEmpty().toSet()

    fun keys(): Set<String> = all()

    fun has(key: String): Boolean = key.isNotEmpty() && key in all()

    fun mark(key: String) {
        if (key.isEmpty()) return
        prefs.edit().putStringSet(KEY, all() + key).apply()
    }

    fun clear(key: String) {
        val now = all()
        if (key in now) prefs.edit().putStringSet(KEY, now - key).apply()
    }
}
