package io.github.n3tk0.loraota.tools

import org.json.JSONArray
import org.json.JSONObject

/** The repeater's `mailbox` reply: "on, owners 1, kept 0/64, ttl 72h, away 10min". */
data class MailboxStatus(
    val enabled: Boolean,
    val owners: Int,
    val kept: Int,
    val max: Int,
    val ttlHours: Int,
    val awayMins: Int,
)

/** One line of `mailbox list` ("a1b2c3d4: 2 here"): the first 4 key bytes, the messages kept for it, heard with no hop just now. */
data class MailboxOwner(val prefix: String, val kept: Int, val here: Boolean)

/**
 * A recipient as the card shows it: [name] from the companion's contacts (null: not a contact), [isSelf] the companion
 * the app is connected to, [fullKey] the contact's whole key when known.
 */
data class MailboxRecipient(
    val prefix: String,
    val kept: Int,
    val here: Boolean,
    val name: String?,
    val isSelf: Boolean,
    val fullKey: String?,
)

/** What the phone remembers of a repeater's mailbox from the last read (for the ✉ marks with no request over the air). */
data class MailboxSummary(
    val enabled: Boolean,
    val recipients: List<MailboxRecipient>,
    val time: Long,
) {
    /** As read (the companion connected then); kept for the start screen, when no companion is connected. */
    val selfIncluded get() = recipients.any { it.isSelf }

    /** [selfKey] the companion connected now ("" when none: then as read), so another companion is never shown as "you". */
    fun includes(selfKey: String): Boolean =
        if (selfKey.length < RepeaterMailbox.PREFIX_HEX) selfIncluded
        else recipients.any { selfKey.lowercase().startsWith(it.prefix) }
}

/** The repeater's `mailbox ...` console commands (examples/simple_repeater/MyMesh.cpp handleMailboxCommand). */
object RepeaterMailbox {
    const val STATUS = "mailbox"
    const val LIST = "mailbox list"
    /** MAILBOX_MAX_OWNERS in the firmware. */
    const val MAX_OWNERS = 4
    const val KEY_HEX = 64
    const val PREFIX_HEX = 8
    val TTL_RANGE = 1..720
    val AWAY_RANGE = 0..1440

    fun cmdEnable(on: Boolean) = if (on) "mailbox on" else "mailbox off"
    fun cmdAdd(keyHex: String) = "mailbox add ${keyHex.lowercase()}"
    fun cmdDel(prefix: String) = "mailbox del ${prefix.lowercase()}"
    fun cmdTtl(hours: Int) = "mailbox ttl $hours"
    fun cmdAway(mins: Int) = "mailbox away $mins"
    const val CLEAR = "mailbox clear"

    private fun clean(reply: String?) = (reply ?: "").trim().removePrefix(">").trim()

    /** A firmware with no mailbox: its unknown-command reply (stock, or built without WITH_MAILBOX). */
    fun isUnsupported(reply: String?) = RepeaterFilter.isUnsupported(reply)

    /** "OK", "OK - add an owner too"; anything else ("Err - ...", unknown command, blank) did not take. */
    fun isOk(reply: String?) = clean(reply).let { it == "OK" || it.startsWith("OK ") || it.startsWith("OK-") }

    /** The firmware's error text without its "Err - " head. */
    fun errorText(reply: String?): String {
        val c = clean(reply)
        val t = Regex("""^err\s*-?\s*""", RegexOption.IGNORE_CASE).replace(c, "")
        return t.ifEmpty { "Failed" }.take(120)
    }

    private val STATUS_RE = Regex(
        """^(on|off),\s*owners\s+(\d+),\s*kept\s+(\d+)/(\d+),\s*ttl\s+(\d+)h,\s*away\s+(\d+)min""",
        RegexOption.IGNORE_CASE,
    )

    fun parseStatus(reply: String?): MailboxStatus? {
        val m = STATUS_RE.find(clean(reply)) ?: return null
        val g = m.groupValues
        return MailboxStatus(g[1].equals("on", true), g[2].toInt(), g[3].toInt(), g[4].toInt(), g[5].toInt(), g[6].toInt())
    }

    private val OWNER_RE = Regex("""([0-9a-fA-F]{8}):\s*(\d+)(\s+here)?""")

    /** `mailbox list`; "no owners" is an empty list, a reply that is neither is null. */
    fun parseList(reply: String?): List<MailboxOwner>? {
        val c = clean(reply)
        if (c.equals("no owners", true)) return emptyList()
        val found = OWNER_RE.findAll(c).map {
            MailboxOwner(it.groupValues[1].lowercase(), it.groupValues[2].toInt(), it.groupValues[3].isNotEmpty())
        }.toList()
        return found.ifEmpty { null }
    }

    /**
     * The owners with names: [contacts] by full key (hex) -> name, [selfKey] the connected companion's key (hex, may be "").
     * A prefix matching more than one contact takes none of the names (4 bytes rarely collide, but never guess).
     */
    fun recipients(owners: List<MailboxOwner>, contacts: Map<String, String>, selfKey: String): List<MailboxRecipient> =
        owners.map { o ->
            val isSelf = selfKey.length >= PREFIX_HEX && selfKey.lowercase().startsWith(o.prefix)
            val matches = contacts.filterKeys { it.lowercase().startsWith(o.prefix) }
            val one = matches.entries.singleOrNull()
            MailboxRecipient(
                prefix = o.prefix,
                kept = o.kept,
                here = o.here,
                name = if (isSelf) null else one?.value?.takeIf { it.isNotBlank() },
                isSelf = isSelf,
                fullKey = if (isSelf) selfKey.lowercase() else one?.key?.lowercase(),
            )
        }

    /** A whole public key (64 hex), spaces and a "0x" head allowed in what is pasted. */
    fun normalizeKey(input: String): String? {
        val s = input.trim().lowercase().removePrefix("0x").replace(" ", "").replace(":", "").replace("-", "")
        return if (s.length == KEY_HEX && s.all { it in '0'..'9' || it in 'a'..'f' }) s else null
    }

    /** True when [keyHex] is already a recipient (its first 4 bytes are one of the owners). */
    fun isOwner(owners: List<MailboxOwner>, keyHex: String) = owners.any { keyHex.lowercase().startsWith(it.prefix) }

    fun validTtl(s: String) = s.trim().toIntOrNull()?.takeIf { it in TTL_RANGE }
    fun validAway(s: String) = s.trim().toIntOrNull()?.takeIf { it in AWAY_RANGE }

    // ---- the summary kept on the phone ----

    fun toJson(s: MailboxSummary): String = JSONObject()
        .put("on", s.enabled)
        .put("t", s.time)
        .put("r", JSONArray().apply {
            for (r in s.recipients) put(
                JSONObject().put("p", r.prefix).put("n", r.kept).put("self", r.isSelf)
                    .apply { r.name?.let { put("name", it) } },
            )
        })
        .toString()

    fun fromJson(raw: String?): MailboxSummary? = try {
        if (raw.isNullOrEmpty()) null else {
            val o = JSONObject(raw)
            val arr = o.optJSONArray("r") ?: JSONArray()
            val list = (0 until arr.length()).map { i ->
                val r = arr.getJSONObject(i)
                MailboxRecipient(
                    prefix = r.getString("p"), kept = r.optInt("n"), here = false,
                    name = if (r.has("name")) r.getString("name") else null,
                    isSelf = r.optBoolean("self"), fullKey = null,
                )
            }
            MailboxSummary(o.optBoolean("on"), list, o.optLong("t"))
        }
    } catch (_: Exception) {
        null
    }
}
