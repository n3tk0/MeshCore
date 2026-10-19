package io.github.n3tk0.loraota

import android.content.Context
import android.content.res.Configuration
import java.util.Locale

/** The app's language, chosen in the app: Bulgarian or English (empty: the phone's). */
object Lang {
    private const val PREFS = "settings"
    private const val KEY = "lang"

    fun get(ctx: Context): String = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(KEY, "") ?: ""

    /** The one the screen shows now. */
    fun current(ctx: Context): String = get(ctx).ifEmpty {
        if (ctx.resources.configuration.locales[0].language == "en") "en" else "bg"
    }

    fun set(ctx: Context, lang: String) {
        ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putString(KEY, lang).apply()
    }

    fun wrap(ctx: Context): Context {
        val lang = get(ctx)
        if (lang.isEmpty()) return ctx
        val config = Configuration(ctx.resources.configuration)
        config.setLocale(Locale(lang))
        return ctx.createConfigurationContext(config)
    }
}
