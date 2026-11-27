package io.github.n3tk0.loraota.tools

import android.content.SharedPreferences

/**
 * Each repeater's mailbox recipients from the last read, by the repeater's public key (hex), so the repeater lists can
 * show who gets mail with no request over the air. A repeater with no mailbox (or whose mailbox is off and empty) is dropped.
 */
class MailboxCache(private val prefs: SharedPreferences) {
    fun get(key: String): MailboxSummary? = RepeaterMailbox.fromJson(prefs.getString(key, null))

    fun all(): Map<String, MailboxSummary> =
        prefs.all.mapNotNull { (k, v) -> (v as? String)?.let { RepeaterMailbox.fromJson(it) }?.let { k to it } }.toMap()

    fun put(key: String, s: MailboxSummary) {
        if (key.isEmpty()) return
        if (!s.enabled && s.recipients.isEmpty()) remove(key)
        else prefs.edit().putString(key, RepeaterMailbox.toJson(s)).apply()
    }

    fun remove(key: String) {
        if (prefs.contains(key)) prefs.edit().remove(key).apply()
    }
}
