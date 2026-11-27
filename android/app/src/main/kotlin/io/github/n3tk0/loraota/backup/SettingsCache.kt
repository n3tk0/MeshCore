package io.github.n3tk0.loraota.backup

/**
 * The settings of each repeater as the last read through its console saw them (about 25 requests over LoRa), so a
 * restore and the settings editor need no new read for a while. Every read fills it; every write by the app updates the
 * written keys (or [invalidate]s the repeater when it is not sure what the repeater has now).
 *
 * Only settings the console gives out: never the admin password (the app adds the one it logs in with by itself).
 * Kept in memory only (the guest password is among the settings, and nothing secret goes to disk in the clear).
 */
class SettingsCache(private val clock: () -> Long = System::currentTimeMillis) {
    /**
     * [time]: when the repeater was read; [withOta]: `ota status` was asked too (our firmware only); [withFilter]: the
     * packet filter (`filter.*` items) was read too.
     */
    class Entry(val time: Long, val withOta: Boolean, val withFilter: Boolean, val items: List<BackupItem>) {
        val values: Map<String, String> get() = items.associate { it.id to it.value }
    }

    companion object {
        /** As long as an automatic backup counts as made 'just now'. */
        /** Repeaters kept (the oldest read goes first). */
        const val MAX_ENTRIES = 16

        const val FRESH_MS = 30 * 60_000L

        fun ageMinutes(now: Long, time: Long): Int = ((now - time).coerceAtLeast(0) / 60_000L).toInt()
    }

    private val map = LinkedHashMap<String, Entry>()

    private fun keep(items: List<BackupItem>) =
        items.filter { it.group == BackupItem.GROUP_SETTING && it.id != RepeaterBackup.PASSWORD }

    /** A successful read of [key]'s settings (a repeater's public key, hex). */
    @Synchronized
    fun put(key: String, items: List<BackupItem>, withOta: Boolean, withFilter: Boolean = false) {
        if (key.isEmpty()) return
        map.remove(key)
        map[key] = Entry(clock(), withOta, withFilter, keep(items))
        while (map.size > MAX_ENTRIES) map.remove(map.keys.first())
    }

    /** What was read less than [FRESH_MS] ago (see [Entry.withOta]: a read without `ota status` has no `ota`). */
    @Synchronized
    fun fresh(key: String): Entry? {
        val e = map[key] ?: return null
        if (clock() - e.time >= FRESH_MS || clock() < e.time) return null
        return e
    }

    /** Minutes since [e] was read. */
    fun ageMinutes(e: Entry): Int = ageMinutes(clock(), e.time)

    /** The repeater took these values: the cache has them now (the time of the read stays: the rest is as old as it was). */
    @Synchronized
    fun update(key: String, written: Map<String, String>) {
        val e = map[key] ?: return
        val values = LinkedHashMap<String, BackupItem>()
        for (i in e.items) values[i.id] = i
        for ((k, v) in written) {
            if (k == RepeaterBackup.PASSWORD) continue
            values[k] = BackupItem(BackupItem.GROUP_SETTING, k, k, v)
        }
        map[key] = Entry(e.time, e.withOta, e.withFilter, values.values.toList())
    }

    /** Not sure what the repeater has now (a failed or partial write, a reboot, a new firmware): read again next time. */
    @Synchronized
    fun invalidate(key: String) {
        map.remove(key)
    }

    @Synchronized
    fun clear() {
        map.clear()
    }

    @Synchronized
    fun has(key: String) = key in map
}
