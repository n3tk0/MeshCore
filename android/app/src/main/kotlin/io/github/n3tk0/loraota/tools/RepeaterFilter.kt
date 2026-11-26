package io.github.n3tk0.loraota.tools

/** The lists of the repeater's `filter` command: `filter <name> list|add|remove`. */
enum class FilterList(val cmd: String) {
    BLOCK("block"), PATH("path"), CHANNEL("channel"), WATCH("watch"), SENDER("sender"), TEXT("text");

    val listCommand get() = "filter $cmd list"
}

/** One list entry: [display] as the repeater shows it, [key] what `remove` takes, [removable] false for the fixed ones (Public). */
data class FilterEntry(val display: String, val key: String, val removable: Boolean = true)

/** Ingress limit: [n] packets per [secs] seconds per entry repeater, [soft] the soft threshold (0 = none). n = 0: off. */
data class FilterIngress(val n: Int, val secs: Int, val soft: Int) {
    val off get() = n <= 0
}

/** Small quota for new keys: [hours] a key counts as new, [mins] between their adverts. hours = 0: off. */
data class FilterNewKey(val hours: Int, val mins: Int) {
    val off get() = hours <= 0
}

/**
 * What the repeater's filter says now. [ingress] and [newKey] are null when the firmware did not answer them
 * (an older filter without these); [ageMin] 0 means off.
 */
data class FilterConfig(
    val enabled: Boolean = false,
    val dryRun: Boolean = false,
    val hash: Int = 1,
    val advertHours: Int = 0,
    val ageMin: Int = 0,
    val ageInactive: Boolean = false,
    val malformed: Boolean = false,
    val ingress: FilterIngress? = null,
    val newKey: FilterNewKey? = null,
    val lists: Map<FilterList, List<FilterEntry>> = emptyMap(),
    /** Lists whose reply was cut by the 160-byte reply buffer (ends in `..`). */
    val truncated: Set<FilterList> = emptySet(),
    val statusRaw: String = "",
)

/** Parsing of the repeater's `filter` replies (Dutch-MeshCore format) and building of its commands. Defensive: no reply shape throws. */
object RepeaterFilter {
    const val STATUS = "filter"
    const val DRYRUN = "filter dryrun"
    const val HASH = "filter hash"
    const val ADVERT = "filter advert"
    const val AGE = "filter age"
    const val MALFORMED = "filter malformed"
    const val INGRESS = "filter ingress"
    const val NEWKEY = "filter newkey"
    const val STATS_AIR = "filter stats air"

    /** The reads of a full load, in this order (`filter` first: it tells if the firmware has a filter at all). */
    val LOAD_QUERIES: List<String> =
        listOf(STATUS, DRYRUN, HASH, ADVERT, AGE, MALFORMED, INGRESS, NEWKEY) + FilterList.entries.map { it.listCommand }

    const val MAX_ENTRIES_NOTE = ".."

    /** The reply without the leading "> " and "Filter:". */
    fun clean(reply: String?): String =
        (reply ?: "").trim().removePrefix(">").trim().let {
            if (it.startsWith("Filter:", ignoreCase = true)) it.substring(7).trim() else it
        }

    /** The repeater has no `filter` command (stock firmware): its error for an unknown command. */
    fun isUnsupported(reply: String?): Boolean {
        val s = (reply ?: "").trim().removePrefix(">").trim().lowercase()
        if (s.isEmpty()) return false
        return s.startsWith("unknown") || s.startsWith("??") || s.startsWith("unsupported") ||
            s.startsWith("error: unsupported") || s.startsWith("err - unknown") || s.contains("unknown command") ||
            s.startsWith("invalid command") || s.startsWith("unrecognized command")
    }

    /** A reply that is an error or a failure (a change did not take). A blank reply counts as one. */
    fun isError(reply: String?): Boolean {
        val s = (reply ?: "").trim().lowercase()
        if (s.isEmpty()) return true
        val c = s.removePrefix(">").trim().removePrefix("filter:").trim()
        return ERROR_START.containsMatchIn(c) || c.contains("command error") || isUnsupported(reply)
    }

    private val ERROR_START = Regex("""^(error|err|fail|failed)\b""")

    fun isOk(reply: String?) = !isError(reply)

    /** A short text for an error reply, for the message line. */
    fun errorText(reply: String?): String {
        val c = clean(reply).removePrefix("error").removePrefix(":").trim()
        return c.ifEmpty { "Failed" }.take(120)
    }

    private val INT = Regex("""\d+""")
    private val WORD_ON = Regex("""\bon\b""", RegexOption.IGNORE_CASE)
    private val WORD_OFF = Regex("""\boff\b""", RegexOption.IGNORE_CASE)

    private fun ints(s: String) = INT.findAll(s).mapNotNull { it.value.toIntOrNull() }.toList()

    /** "Filter on: Blocked [...]" / "Filter off ..." / "Filter: on": the enabled flag, and the (dry-run) marker. */
    fun parseStatus(reply: String?): Pair<Boolean, Boolean>? {
        val s = (reply ?: "").trim().removePrefix(">").trim()
        val m = Regex("""^Filter:?\s+(on|off)\b""", RegexOption.IGNORE_CASE).find(s) ?: return null
        return m.groupValues[1].equals("on", true) to s.contains("(dry-run)", ignoreCase = true)
    }

    /** "dry-run on" / "dry-run off" (or just on/off); null when it says neither. */
    fun parseDryRun(reply: String?): Boolean? {
        val s = clean(reply)
        Regex("""dry-?run\W*(on|off)\b""", RegexOption.IGNORE_CASE).find(s)?.let { return it.groupValues[1].equals("on", true) }
        return onOff(s)
    }

    /** "malformed text scan off" / "malformed scan on". */
    fun parseMalformed(reply: String?): Boolean? = onOff(clean(reply))

    private fun onOff(s: String): Boolean? {
        val on = WORD_ON.containsMatchIn(s)
        val off = WORD_OFF.containsMatchIn(s)
        return when {
            on && !off -> true
            off && !on -> false
            else -> null
        }
    }

    /** "minimal 1 bytes path hash size": 1..3, or null. */
    fun parseHash(reply: String?): Int? {
        val s = clean(reply)
        val n = Regex("""minimal\s+(\d+)""", RegexOption.IGNORE_CASE).find(s)?.groupValues?.get(1)?.toIntOrNull()
            ?: ints(s).firstOrNull()
        return n?.takeIf { it in 1..3 }
    }

    /** "advert origin window 48h (cache 87/256)": the hours (0 = off), or null. */
    fun parseAdvertHours(reply: String?): Int? {
        val s = clean(reply)
        val n = Regex("""window\s+(\d+)\s*h""", RegexOption.IGNORE_CASE).find(s)?.groupValues?.get(1)?.toIntOrNull()
            ?: ints(s).firstOrNull()
        return n?.takeIf { it in 0..720 }
    }

    /** "Message age: max 60m, dropped 0" -> 60 (+ inactive when "clock not set"); "off" -> 0; null when unreadable. */
    fun parseAge(reply: String?): Pair<Int, Boolean>? {
        val s = clean(reply)
        if (s.isEmpty() || isError(reply)) return null
        val inactive = s.contains("clock not set", ignoreCase = true)
        val m = Regex("""max\s+(\d+)\s*m""", RegexOption.IGNORE_CASE).find(s)?.groupValues?.get(1)?.toIntOrNull()
        if (m != null) return m to inactive
        if (WORD_OFF.containsMatchIn(s)) return 0 to inactive
        return null
    }

    /** ingress: "off", or numbers n, secs and (after "soft") the soft threshold; the exact wording is the firmware's. */
    fun parseIngress(reply: String?): FilterIngress? {
        if (isError(reply)) return null
        val s = clean(reply)
        if (WORD_OFF.containsMatchIn(s)) return FilterIngress(0, 0, 0)
        val soft = Regex("""soft\W*(\d+)""", RegexOption.IGNORE_CASE).find(s)?.groupValues?.get(1)?.toIntOrNull()
        val head = ints(s.substringBefore("soft", s))
        val n = head.getOrNull(0) ?: return null
        return FilterIngress(n, head.getOrNull(1) ?: 0, soft ?: 0)
    }

    /** newkey: "off", or hours and (optionally) minutes. */
    fun parseNewKey(reply: String?): FilterNewKey? {
        if (isError(reply)) return null
        val s = clean(reply)
        if (WORD_OFF.containsMatchIn(s)) return FilterNewKey(0, 0)
        val n = ints(s)
        val h = n.getOrNull(0) ?: return null
        return FilterNewKey(h, n.getOrNull(1) ?: 60)
    }

    /** A list reply: comma-separated, `None` (or blank) for empty. Returns null for an error reply. */
    fun parseList(list: FilterList, reply: String?): Pair<List<FilterEntry>, Boolean>? {
        if (isError(reply)) return null
        var s = clean(reply)
        val truncated = s.endsWith(MAX_ENTRIES_NOTE)
        if (truncated) s = s.removeSuffix(MAX_ENTRIES_NOTE).trimEnd(',', ' ')
        if (s.isEmpty() || s.equals("none", ignoreCase = true)) return emptyList<FilterEntry>() to false
        val out = s.split(',').map { it.trim() }.filter { it.isNotEmpty() }.map { entry(list, it) }
        return out to truncated
    }

    private val RULE_MODE = Regex("""^(.+?)\s+(block|throttle|dose)\b.*$""", RegexOption.IGNORE_CASE)
    private val CHANNEL_HASH = Regex("""\s*\([0-9a-fA-F]{2}\)\s*$""")

    internal fun entry(list: FilterList, raw: String): FilterEntry = when (list) {
        FilterList.BLOCK, FilterList.PATH -> FilterEntry(raw, raw.substringBefore(' ').trim())
        FilterList.CHANNEL, FilterList.WATCH -> {
            val name = raw.replace(CHANNEL_HASH, "").trim()
            FilterEntry(raw, name, removable = !(list == FilterList.WATCH && name.equals("Public", true)))
        }
        FilterList.SENDER, FilterList.TEXT -> FilterEntry(raw, RULE_MODE.find(raw)?.groupValues?.get(1)?.trim() ?: raw)
    }

    /** One parsed reply into [cfg], for the read command [query]; an unreadable reply leaves the setting as it was. */
    fun apply(cfg: FilterConfig, query: String, reply: String?): FilterConfig {
        val q = query.trim()
        return when (q) {
            STATUS -> {
                val st = parseStatus(reply)
                if (st == null) cfg.copy(statusRaw = clean(reply)) else cfg.copy(enabled = st.first, dryRun = st.second, statusRaw = (reply ?: "").trim().removePrefix(">").trim())
            }
            DRYRUN -> parseDryRun(reply)?.let { cfg.copy(dryRun = it) } ?: cfg
            HASH -> parseHash(reply)?.let { cfg.copy(hash = it) } ?: cfg
            ADVERT -> parseAdvertHours(reply)?.let { cfg.copy(advertHours = it) } ?: cfg
            AGE -> parseAge(reply)?.let { cfg.copy(ageMin = it.first, ageInactive = it.second) } ?: cfg
            MALFORMED -> parseMalformed(reply)?.let { cfg.copy(malformed = it) } ?: cfg
            INGRESS -> cfg.copy(ingress = parseIngress(reply))
            NEWKEY -> cfg.copy(newKey = parseNewKey(reply))
            else -> {
                val list = FilterList.entries.firstOrNull { it.listCommand == q } ?: return cfg
                val parsed = parseList(list, reply) ?: return cfg
                cfg.copy(
                    lists = cfg.lists + (list to parsed.first),
                    truncated = if (parsed.second) cfg.truncated + list else cfg.truncated - list,
                )
            }
        }
    }

    // ---- commands ----

    fun cmdOnOff(base: String, on: Boolean) = "$base ${if (on) "on" else "off"}"

    fun cmdEnable(on: Boolean) = if (on) "filter on" else "filter off"

    fun cmdHash(n: Int) = "filter hash $n"

    fun cmdAdvert(hours: Int) = "filter advert $hours"

    fun cmdAge(minutes: Int?) = "filter age ${minutes?.toString() ?: "off"}"

    fun cmdIngress(i: FilterIngress?) =
        if (i == null || i.off) "filter ingress off"
        else "filter ingress ${i.n} ${i.secs}" + if (i.soft > 0) " ${i.soft}" else ""

    fun cmdNewKey(k: FilterNewKey?) =
        if (k == null || k.off) "filter newkey off" else "filter newkey ${k.hours} ${k.mins}"

    fun cmdAdd(list: FilterList, value: String, secs: Int? = null, prob: Int? = null): String {
        val extra = buildString {
            if (list == FilterList.SENDER || list == FilterList.TEXT) {
                if (secs != null || prob != null) append(' ').append(secs ?: 0)
                if (prob != null) append(' ').append(prob)
            }
        }
        return "filter ${list.cmd} add $value$extra"
    }

    fun cmdRemove(list: FilterList, key: String) = "filter ${list.cmd} remove $key"

    // ---- validation of what the user types (null = fine) ----

    enum class Err { EMPTY, HEX, BLOCK_LEN, PATH_LEN, ODD, NAME_HASH, NAME_SPACE, NAME_LONG, SENDER_LONG, TEXT_LONG, SECS, PROB, RANGE }

    private val HEX = Regex("""^[0-9a-fA-F]+$""")

    fun validateBlock(v: String): Err? {
        val s = v.trim()
        return when {
            s.isEmpty() -> Err.EMPTY
            !HEX.matches(s) -> Err.HEX
            s.length < 8 || s.length > 64 -> Err.BLOCK_LEN
            s.length % 2 != 0 -> Err.ODD
            else -> null
        }
    }

    fun validatePath(v: String): Err? {
        val s = v.trim()
        return when {
            s.isEmpty() -> Err.EMPTY
            !HEX.matches(s) -> Err.HEX
            s.length < 2 || s.length > 8 -> Err.PATH_LEN
            s.length % 2 != 0 -> Err.ODD
            else -> null
        }
    }

    /** Channel (block list) takes `#name` or `Public`; watch takes `#name`. */
    fun validateChannel(v: String, allowPublic: Boolean): Err? {
        val s = v.trim()
        return when {
            s.isEmpty() -> Err.EMPTY
            s.any { it.isWhitespace() || it == ',' } -> Err.NAME_SPACE
            allowPublic && s.equals("Public", ignoreCase = true) -> null
            !s.startsWith("#") || s.length < 2 -> Err.NAME_HASH
            s.length > 30 -> Err.NAME_LONG
            else -> null
        }
    }

    fun validateSender(v: String): Err? {
        val s = v.trim()
        return when {
            s.isEmpty() -> Err.EMPTY
            s.any { it.isWhitespace() || it == ',' } -> Err.NAME_SPACE
            s.length > 15 -> Err.SENDER_LONG
            else -> null
        }
    }

    fun validateText(v: String): Err? {
        val s = v.trim()
        return when {
            s.isEmpty() -> Err.EMPTY
            s.any { it.isWhitespace() || it == ',' } -> Err.NAME_SPACE
            s.length > 23 -> Err.TEXT_LONG
            else -> null
        }
    }

    /** Optional secs (0..65535) and prob (1..100) of a sender/text rule; empty = not given. */
    fun validateRuleNumbers(secs: String, prob: String): Err? {
        val s = secs.trim()
        val p = prob.trim()
        if (s.isNotEmpty()) {
            val n = s.toIntOrNull()
            if (n == null || n !in 0..65535) return Err.SECS
        }
        if (p.isNotEmpty()) {
            val n = p.toIntOrNull()
            if (n == null || n !in 1..100) return Err.PROB
        }
        return null
    }

    fun validateHash(v: String): Err? = if (v.trim().toIntOrNull() in 1..3) null else Err.RANGE
    fun validateAdvert(v: String): Err? = if (v.trim().toIntOrNull() in 0..720) null else Err.RANGE
    fun validateAge(v: String): Err? = if (v.trim().toIntOrNull() in 1..10080) null else Err.RANGE
    fun validateIngress(n: String, secs: String, soft: String): Err? {
        val nn = n.trim().toIntOrNull()
        val ss = secs.trim().toIntOrNull()
        if (nn == null || nn !in 1..65535 || ss == null || ss !in 1..65535) return Err.RANGE
        if (soft.isNotBlank()) {
            val so = soft.trim().toIntOrNull()
            if (so == null || so < 1 || so >= nn) return Err.RANGE
        }
        return null
    }
    fun validateNewKey(hours: String, mins: String): Err? {
        val h = hours.trim().toIntOrNull()
        if (h == null || h !in 1..8760) return Err.RANGE
        if (mins.isNotBlank()) {
            val m = mins.trim().toIntOrNull()
            if (m == null || m !in 1..1440) return Err.RANGE
        }
        return null
    }

    // ---- backup of the filter in a repeater backup ----

    /** Ids of the filter items in a backup (group `setting`); the values: on/off, numbers, or a list as the repeater shows it. */
    const val BACKUP_PREFIX = "filter."

    fun backupId(name: String) = BACKUP_PREFIX + name

    fun isBackupId(id: String) = id.startsWith(BACKUP_PREFIX)

    /**
     * What to send to bring a list to the saved value [saved] (the list reply as saved) given the one now [current]:
     * the entries the repeater lacks, as `add` commands. Entries it has and the saved list lacks are left alone,
     * as everywhere in a restore.
     */
    fun restoreAdds(list: FilterList, saved: String, current: List<FilterEntry>): List<String> {
        val have = current.map { it.key.lowercase() }.toSet()
        val entries = parseList(list, saved)?.first ?: return emptyList()
        return entries.filter { it.key.lowercase() !in have }.mapNotNull { e ->
            when (list) {
                FilterList.SENDER, FilterList.TEXT -> {
                    // "Bob throttle 60s", "Bot* block 50%", "SpamBot block": name [secs] [prob]
                    val secs = Regex("""(\d+)\s*s\b""").find(e.display)?.groupValues?.get(1)?.toIntOrNull()
                    val prob = Regex("""(\d+)\s*%""").find(e.display)?.groupValues?.get(1)?.toIntOrNull()
                    cmdAdd(list, e.key, secs, prob)
                }
                FilterList.WATCH -> if (e.removable) cmdAdd(list, e.key) else null
                else -> cmdAdd(list, e.key)
            }
        }
    }

    /** The commands that bring the scalars back, enabled last: (backup name, value) -> command; null for one that makes no sense. */
    fun restoreScalar(name: String, value: String): String? {
        val v = value.trim()
        return when (name) {
            "enabled" -> if (v == "on" || v == "off") "filter $v" else null
            "dryrun" -> if (v == "on" || v == "off") "filter dryrun $v" else null
            "malformed" -> if (v == "on" || v == "off") "filter malformed $v" else null
            "hash" -> v.toIntOrNull()?.takeIf { it in 1..3 }?.let { cmdHash(it) }
            "advert" -> v.toIntOrNull()?.takeIf { it in 0..720 }?.let { cmdAdvert(it) }
            "age" -> if (v == "off") cmdAge(null) else v.toIntOrNull()?.takeIf { it in 1..10080 }?.let { cmdAge(it) }
            "ingress" -> if (v == "off") cmdIngress(null) else {
                val p = v.split(' ').mapNotNull { it.toIntOrNull() }
                if (p.size >= 2 && p[0] > 0) cmdIngress(FilterIngress(p[0], p[1], p.getOrElse(2) { 0 })) else null
            }
            "newkey" -> if (v == "off") cmdNewKey(null) else {
                val p = v.split(' ').mapNotNull { it.toIntOrNull() }
                if (p.isNotEmpty() && p[0] > 0) cmdNewKey(FilterNewKey(p[0], p.getOrElse(1) { 60 })) else null
            }
            else -> null
        }
    }

    /** The scalars of [cfg] as backup values. */
    fun backupScalars(cfg: FilterConfig): List<Pair<String, String>> = buildList {
        add("enabled" to if (cfg.enabled) "on" else "off")
        add("dryrun" to if (cfg.dryRun) "on" else "off")
        add("malformed" to if (cfg.malformed) "on" else "off")
        add("hash" to cfg.hash.toString())
        add("advert" to cfg.advertHours.toString())
        add("age" to if (cfg.ageMin > 0) cfg.ageMin.toString() else "off")
        cfg.ingress?.let { add("ingress" to if (it.off) "off" else "${it.n} ${it.secs}" + if (it.soft > 0) " ${it.soft}" else "") }
        cfg.newKey?.let { add("newkey" to if (it.off) "off" else "${it.hours} ${it.mins}") }
    }

    /** Scalars restore in this order: the rules first, `enabled` last (a half-restored filter never runs). */
    val RESTORE_SCALAR_ORDER = listOf("dryrun", "malformed", "hash", "advert", "age", "ingress", "newkey", "enabled")
}
