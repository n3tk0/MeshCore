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
    /** Lists whose reply was cut by the 158-byte reply buffer (ends in `..`). */
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

    /** The repeater has no `filter` command (stock firmware): its error for an unknown command. Never a list entry. */
    fun isUnsupported(reply: String?): Boolean {
        val s = (reply ?: "").trim().removePrefix(">").trim().lowercase()
        if (s.isEmpty()) return false
        return s.startsWith("unknown command") || UNKNOWN_Q.containsMatchIn(s) || s == "unsupported" ||
            s.startsWith("error: unsupported") || s.startsWith("err - unknown") ||
            s.startsWith("invalid command") || s.startsWith("unrecognized command")
    }

    private val UNKNOWN_Q = Regex("""^\?\?(:|$)""")

    /**
     * The firmware's own error replies: "> Filter: error ...", "> Filter: syntax error ...", "> Filter: command error", the
     * bare "Failed", its usage line ("> filter path [ list ..."), or an unknown-command reply. Only these: the text of a
     * list entry ("Error*", "Fail", "Unknown") must never pass for an error. A blank reply counts as one.
     */
    fun isError(reply: String?): Boolean {
        val s = (reply ?: "").trim().lowercase()
        if (s.isEmpty()) return true
        val c = s.removePrefix(">").trim()
        return c.startsWith("filter: error") || c.startsWith("filter: syntax error") || c.startsWith("filter: command error") ||
            c == "failed" || USAGE.containsMatchIn(c) || isUnsupported(reply)
    }

    private val USAGE = Regex("""^filter( \w+)* \[ """)

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
        // firmware: "> Newkey: keys under 24h get 1 advert per 60m (12 keys known)"
        val h = Regex("""(\d+)\s*h""", RegexOption.IGNORE_CASE).find(s)?.groupValues?.get(1)?.toIntOrNull()
            ?: ints(s).getOrNull(0) ?: return null
        val m = Regex("""per\s+(\d+)\s*m""", RegexOption.IGNORE_CASE).find(s)?.groupValues?.get(1)?.toIntOrNull()
            ?: Regex("""(\d+)\s*min""", RegexOption.IGNORE_CASE).find(s)?.groupValues?.get(1)?.toIntOrNull()
            ?: 60
        return FilterNewKey(h, m)
    }

    /** The tail of a paged block list: " [6/10 p0]" (end, total, page); the older wording had a comma. */
    private val PAGE_TAIL = Regex("""\s*\[(\d+)/(\d+)(?:\s+p|,\s*page\s*)(\d+)\]\s*$""")

    /** (entries shown up to and including this page, total) from a paged list reply; null when it has no tail. */
    fun pageInfo(reply: String?): Pair<Int, Int>? {
        var s = clean(reply)
        if (s.endsWith(MAX_ENTRIES_NOTE)) s = s.removeSuffix(MAX_ENTRIES_NOTE).trimEnd(',', ' ')
        val m = PAGE_TAIL.find(s) ?: return null
        val end = m.groupValues[1].toIntOrNull() ?: return null
        val total = m.groupValues[2].toIntOrNull() ?: return null
        return end to total
    }

    fun blockPageInfo(reply: String?) = pageInfo(reply)

    /** A list reply: comma-separated, `None` (or blank) for empty. Returns null for an error reply. */
    fun parseList(list: FilterList, reply: String?): Pair<List<FilterEntry>, Boolean>? {
        if (isError(reply)) return null
        var s = clean(reply)
        val truncated = s.endsWith(MAX_ENTRIES_NOTE)
        if (truncated) s = s.removeSuffix(MAX_ENTRIES_NOTE).trimEnd(',', ' ')
        s = s.replace(PAGE_TAIL, "")
        if (s.isEmpty() || s.equals("none", ignoreCase = true)) return emptyList<FilterEntry>() to false
        val out = s.split(',').map { it.trim() }.filter { it.isNotEmpty() }.map { entry(list, it) }
        return out to truncated
    }

    /**
     * `filter <list> list [page] [full]`: page 0 of the short form is the plain command (older firmware has no pages).
     * Every list that can outgrow the 158-byte reply is paged the same way; `full` only means something for the block list.
     */
    fun cmdListPage(list: FilterList, page: Int, full: Boolean = false) =
        if (page == 0 && !full) list.listCommand else list.listCommand + " $page" + if (full && list == FilterList.BLOCK) " full" else ""

    fun cmdBlockPage(page: Int, full: Boolean) = cmdListPage(FilterList.BLOCK, page, full)

    /** The list a read command (`filter sender list`) belongs to, or null. */
    fun listOfQuery(query: String): FilterList? = FilterList.entries.firstOrNull { it.listCommand == query.trim() }

    /**
     * A whole list, page by page ([ask] sends one command and gives its reply). [full]: whole keys (block list only, for a
     * backup), else the first 8 bytes of each with a `+` for longer ones. The Boolean is true when the list is not complete
     * (a page did not come, was cut by the reply buffer, or the pages do not add up). Null when the first page did not come
     * or was an error. Entries are never merged: two long keys that share their first 8 bytes are two entries.
     */
    suspend fun readList(
        list: FilterList, full: Boolean = false, maxPages: Int = 40, ask: suspend (String) -> String?,
    ): Pair<List<FilterEntry>, Boolean>? {
        val firstReply = ask(cmdListPage(list, 0, full)) ?: return null
        val first = parseList(list, firstReply) ?: return null
        val all = ArrayList(first.first)
        var incomplete = first.second
        var info = pageInfo(firstReply)
        var page = 0
        while (info != null && info.first < info.second && page < maxPages) {
            page++
            val reply = ask(cmdListPage(list, page, full))
            val parsed = reply?.let { parseList(list, it) }
            val next = pageInfo(reply)
            // no reply, an error, "no page N", or a page that does not move on (a firmware that ignores the page number)
            if (parsed == null || parsed.first.isEmpty() || clean(reply).startsWith("no page") || next == null || next.first <= info.first) {
                incomplete = true
                break
            }
            all.addAll(parsed.first)
            if (parsed.second) incomplete = true
            info = next
        }
        if (info != null && info.first < info.second) incomplete = true
        if (info != null && all.size != info.second) incomplete = true   // what was collected must add up to the total
        return all to incomplete
    }

    suspend fun readBlock(full: Boolean, maxPages: Int = 40, ask: suspend (String) -> String?) = readList(FilterList.BLOCK, full, maxPages, ask)

    /** [cfg] with the list of a [readList] result. */
    fun withList(cfg: FilterConfig, list: FilterList, read: Pair<List<FilterEntry>, Boolean>): FilterConfig = cfg.copy(
        lists = cfg.lists + (list to read.first),
        truncated = if (read.second) cfg.truncated + list else cfg.truncated - list,
    )

    fun withBlock(cfg: FilterConfig, read: Pair<List<FilterEntry>, Boolean>) = withList(cfg, FilterList.BLOCK, read)

    private val RULE_MODE = Regex("""^(.+?)\s+(block|throttle|dose)\b(.*)$""", RegexOption.IGNORE_CASE)
    private val CHANNEL_HASH = Regex("""\s*\([0-9a-fA-F]{2}\)\s*$""")

    internal fun entry(list: FilterList, raw: String): FilterEntry = when (list) {
        // block: "A0A1A2A3A4A5A6A7+" = the first 8 bytes of a longer key; what `remove` takes is the hex without the '+'
        FilterList.BLOCK -> FilterEntry(raw, raw.substringBefore(' ').trim().trimEnd('+'))
        FilterList.PATH -> FilterEntry(raw, raw.substringBefore(' ').trim())
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
                // the (dry-run) marker is NOT taken from here: the summary line can be cut; `filter dryrun` says it
                if (st == null) cfg.copy(statusRaw = clean(reply)) else cfg.copy(enabled = st.first, statusRaw = (reply ?: "").trim().removePrefix(">").trim())
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

    /** `filter stats air`: [ms] the airtime the drops saved (null: nothing yet), [dry] it is what a dry run WOULD have saved. */
    data class AirSaved(val ms: Long?, val dry: Boolean)

    /** "saved airtime 1234 ms (1s)", "no airtime saved yet", and the dry-run wordings; null when it is none of them. */
    fun parseAir(reply: String?): AirSaved? {
        val s = clean(reply)
        val dry = s.contains("dry-run", ignoreCase = true)
        Regex("""airtime\s+(\d+)\s*ms""", RegexOption.IGNORE_CASE).find(s)?.let { return AirSaved(it.groupValues[1].toLongOrNull(), dry) }
        if (s.contains("no airtime", ignoreCase = true)) return AirSaved(null, dry)
        return null
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

    /** What the firmware takes: `Public` is exactly that, whatever the case typed; the rest as typed. */
    fun normalizeChannel(v: String): String = v.trim().let { if (it.equals("Public", ignoreCase = true)) "Public" else it }

    fun cmdAdd(list: FilterList, value: String, secs: Int? = null, prob: Int? = null): String {
        val extra = buildString {
            if (list == FilterList.SENDER || list == FilterList.TEXT) {
                if (secs != null || prob != null) append(' ').append(secs ?: 0)
                if (prob != null) append(' ').append(prob)
            }
        }
        val v = if (list == FilterList.CHANNEL || list == FilterList.WATCH) normalizeChannel(value) else value
        return "filter ${list.cmd} add $v$extra"
    }

    fun cmdRemove(list: FilterList, key: String) =
        "filter ${list.cmd} remove ${if (list == FilterList.CHANNEL || list == FilterList.WATCH) normalizeChannel(key) else key}"

    // ---- validation of what the user types (null = fine) ----

    enum class Err { EMPTY, HEX, BLOCK_LEN, PATH_LEN, ODD, NAME_HASH, NAME_SPACE, NAME_LONG, SENDER_LONG, TEXT_LONG, SECS, PROB, RANGE }

    // the firmware's limits (FILTER_CHAN_NAME_LEN 24 with the NUL, NEWKEY_MAX_*, ingress ranges)
    const val CHANNEL_NAME_MAX = 23
    const val NEWKEY_MAX_HOURS = 720
    const val NEWKEY_MAX_MINS = 1440
    const val INGRESS_MAX_N = 10000
    const val INGRESS_MAX_SECS = 3600

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
            s.length > CHANNEL_NAME_MAX -> Err.NAME_LONG
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
        if (nn == null || nn !in 1..INGRESS_MAX_N || ss == null || ss !in 1..INGRESS_MAX_SECS) return Err.RANGE
        if (soft.isNotBlank()) {
            val so = soft.trim().toIntOrNull()
            if (so == null || so < 1 || so >= nn) return Err.RANGE
        }
        return null
    }
    fun validateNewKey(hours: String, mins: String): Err? {
        val h = hours.trim().toIntOrNull()
        if (h == null || h !in 1..NEWKEY_MAX_HOURS) return Err.RANGE
        if (mins.isNotBlank()) {
            val m = mins.trim().toIntOrNull()
            if (m == null || m !in 1..NEWKEY_MAX_MINS) return Err.RANGE
        }
        return null
    }

    // ---- backup of the filter in a repeater backup ----

    /** Ids of the filter items in a backup (group `setting`); the values: on/off, numbers, or a list as the repeater shows it. */
    const val BACKUP_PREFIX = "filter."

    fun backupId(name: String) = BACKUP_PREFIX + name

    /** A note in a backup: these filter queries got no answer (value = the list of them), so the filter in it is partial. */
    const val INCOMPLETE_ID = BACKUP_PREFIX + "incomplete"

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
                    // "Bob throttle 60s", "Bot* block 50%", "SpamBot block": name, then the mode token, then secs / prob.
                    // Digits in the NAME ("Rover7s block") are not a throttle: only what follows the mode counts.
                    val m = RULE_MODE.find(e.display)
                    val tail = m?.groupValues?.get(3).orEmpty()
                    val throttle = m?.groupValues?.get(2).equals("throttle", ignoreCase = true)
                    val secs = if (throttle) Regex("""^\s*(\d+)\s*s\b""").find(tail)?.groupValues?.get(1)?.toIntOrNull() else null
                    val prob = Regex("""(\d+)\s*%""").find(tail)?.groupValues?.get(1)?.toIntOrNull()
                    cmdAdd(list, e.key, secs, prob)
                }
                FilterList.WATCH -> if (e.removable) cmdAdd(list, e.key) else null
                // only a whole key can be restored: "...+" is a cut-down prefix of a longer one
                FilterList.BLOCK -> if (e.display.trim().endsWith("+")) null else cmdAdd(list, e.key)
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
                if (p.size >= 2 && p[0] in 1..INGRESS_MAX_N && p[1] in 1..INGRESS_MAX_SECS) cmdIngress(FilterIngress(p[0], p[1], p.getOrElse(2) { 0 })) else null
            }
            "newkey" -> if (v == "off") cmdNewKey(null) else {
                val p = v.split(' ').mapNotNull { it.toIntOrNull() }
                val mins = p.getOrElse(1) { 60 }
                if (p.isNotEmpty() && p[0] in 1..NEWKEY_MAX_HOURS && mins in 1..NEWKEY_MAX_MINS) cmdNewKey(FilterNewKey(p[0], mins)) else null
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
