package io.github.n3tk0.loraota

import android.content.Context

/** App theme override: follow system, always light, or always dark. */
enum class ThemeMode(val key: String) {
    SYSTEM("system"),
    LIGHT("light"),
    DARK("dark");

    companion object {
        fun byKey(k: String?): ThemeMode = entries.firstOrNull { it.key == k } ?: SYSTEM
    }
}

/** App-level user preferences stored in SharedPreferences ("settings", alongside Lang). */
object AppPrefs {
    private const val PREFS = "settings"
    private const val KEY_THEME = "theme"
    private const val KEY_DYNAMIC_COLOR = "dynamic_color"
    private const val KEY_AUTO_UPDATE = "auto_update_check"
    private const val KEY_LAST_UPDATE_CHECK = "last_update_check"

    private fun prefs(ctx: Context) = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    fun theme(ctx: Context): ThemeMode = ThemeMode.byKey(prefs(ctx).getString(KEY_THEME, null))

    fun setTheme(ctx: Context, mode: ThemeMode) {
        prefs(ctx).edit().putString(KEY_THEME, mode.key).apply()
    }

    /** Material You colors from the wallpaper (Android 12+ only); off by default. */
    fun dynamicColor(ctx: Context): Boolean = prefs(ctx).getBoolean(KEY_DYNAMIC_COLOR, false)

    fun setDynamicColor(ctx: Context, enabled: Boolean) {
        prefs(ctx).edit().putBoolean(KEY_DYNAMIC_COLOR, enabled).apply()
    }

    fun autoUpdateCheck(ctx: Context): Boolean = prefs(ctx).getBoolean(KEY_AUTO_UPDATE, true)

    fun setAutoUpdateCheck(ctx: Context, enabled: Boolean) {
        prefs(ctx).edit().putBoolean(KEY_AUTO_UPDATE, enabled).apply()
    }

    fun lastUpdateCheck(ctx: Context): Long = prefs(ctx).getLong(KEY_LAST_UPDATE_CHECK, 0L)

    fun setLastUpdateCheck(ctx: Context, timeMs: Long = System.currentTimeMillis()) {
        prefs(ctx).edit().putLong(KEY_LAST_UPDATE_CHECK, timeMs).apply()
    }
}
