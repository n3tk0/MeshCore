package io.github.n3tk0.loraota.backup

import io.github.n3tk0.loraota.ble.CompanionRadio
import io.github.n3tk0.loraota.ble.Contact
import io.github.n3tk0.loraota.ota.hex
import io.github.n3tk0.loraota.tools.FilterConfig
import io.github.n3tk0.loraota.tools.FilterEntry
import io.github.n3tk0.loraota.tools.FilterList
import io.github.n3tk0.loraota.tools.RepeaterFilter
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.withContext
import java.nio.ByteBuffer
import java.nio.ByteOrder

private fun unhex(s: String) = ByteArray(s.length / 2) { s.substring(it * 2, it * 2 + 2).toInt(16).toByte() }

private fun setting(id: String, value: Any) = BackupItem(BackupItem.GROUP_SETTING, id, id, value.toString())

/** What came back from a restore: the items that did not take, with the reason. */
class RestoreResult(val done: Int, val failed: List<Pair<BackupItem, String>>)

/**
 * The companion's settings, channels, contacts and identity, over its app protocol (see
 * examples/companion_radio/MyMesh.cpp). Each part on its own: older firmware without a command just has
 * less in its backup.
 */
object CompanionBackup {
    /** The settings as raw numbers, so that a restore writes back exactly what was read. */
    suspend fun read(c: CompanionRadio): List<BackupItem> {
        c.start()   // a fresh self info
        runCatching { c.deviceInfo() }
        val out = ArrayList<BackupItem>()
        val s = c.selfInfo
        val pub = if (s.size >= 36) hex(s.copyOfRange(4, 36)) else ""
        out.add(BackupItem(BackupItem.GROUP_KEY, BackupItem.KEY_ID, "identity", pub))
        val d = c.deviceInfoRaw
        if (s.size >= 58) {
            val b = ByteBuffer.wrap(s).order(ByteOrder.LITTLE_ENDIAN)
            out.add(setting("name", c.nodeName))
            val repeat = if (d.size > 80) d[80].toInt() and 0xff else 0
            out.add(setting("radio", "${b.getInt(48)},${b.getInt(52)},${s[56].toInt() and 0xff},${s[57].toInt() and 0xff},$repeat"))
            out.add(setting("tx", s[2].toInt()))
            out.add(setting("latlon", "${b.getInt(36)},${b.getInt(40)}"))
            // manual add, telemetry modes, location policy, multi acks: the order of CMD_SET_OTHER_PARAMS
            out.add(setting("other", "${s[47].toInt() and 0xff},${s[46].toInt() and 0xff},${s[45].toInt() and 0xff},${s[44].toInt() and 0xff}"))
        }
        if (d.size >= 8) out.add(setting("pin", ByteBuffer.wrap(d, 4, 4).order(ByteOrder.LITTLE_ENDIAN).int))
        if (d.size > 81) out.add(setting("hash", d[81].toInt() and 0xff))
        runCatching { c.tuning() }.onSuccess { (rx, af) -> out.add(setting("tuning", "$rx,$af")) }
        runCatching { c.autoAdd() }.onSuccess { (cfg, hops) -> out.add(setting("autoadd", "$cfg,$hops")) }
        runCatching { c.floodScope() }.onSuccess { out.add(setting("scope", hex(it))) }
        runCatching { c.customVars() }.onSuccess { vars ->
            for (kv in vars.split(',')) {
                val k = kv.substringBefore(':', "")
                if (k.isNotEmpty()) out.add(setting("var.$k", kv.substringAfter(':')))
            }
        }
        for (i in 0 until c.maxChannels) {
            val slot = c.channel(i) ?: break
            val name = String(slot, 1, 32, Charsets.UTF_8).substringBefore('\u0000')
            // an empty slot: nothing to keep (and a restore leaves the slot as it is)
            if (name.isEmpty() && slot.copyOfRange(33, 49).all { it.toInt() == 0 }) continue
            // the name field has whatever the companion's buffer had after the name's end: zeros instead
            var safeName = name
            while (safeName.toByteArray(Charsets.UTF_8).size > 31) {
                // a whole character off the end, not half of an emoji's surrogate pair
                safeName = safeName.substring(0, safeName.offsetByCodePoints(safeName.length, -1))
            }
            val nameBytes = safeName.toByteArray(Charsets.UTF_8)
            val field = ByteArray(32).also { nameBytes.copyInto(it, endIndex = nameBytes.size) }
            out.add(BackupItem(BackupItem.GROUP_CHANNEL, i.toString(), safeName, hex(field + slot.copyOfRange(33, 49))))
        }
        for (ct in c.contacts()) out.add(BackupItem(BackupItem.GROUP_CONTACT, hex(ct.publicKey), ct.name, hex(ct.frame)))
        return out
    }

    /** The identity, or null when the firmware does not give it out (no ENABLE_PRIVATE_KEY_EXPORT). */
    suspend fun privateKey(c: CompanionRadio): ByteArray? = runCatching { c.privateKey() }.getOrNull()

    /** Writes the chosen items: the identity first (it reloads the contacts), the contacts last. */
    suspend fun apply(c: CompanionRadio, items: List<BackupItem>, privateKey: ByteArray?): RestoreResult {
        val failed = ArrayList<Pair<BackupItem, String>>()
        var done = 0
        val order = listOf(BackupItem.GROUP_KEY, BackupItem.GROUP_SETTING, BackupItem.GROUP_CHANNEL, BackupItem.GROUP_CONTACT)
        for (item in items.sortedBy { order.indexOf(it.group) }) {
            val r = runCatching { applyOne(c, item, privateKey) }
            val ex = r.exceptionOrNull()
            if (ex is kotlinx.coroutines.CancellationException) throw ex
            if (r.isSuccess) done++ else failed.add(item to (ex?.message ?: ""))
        }
        return RestoreResult(done, failed)
    }

    private suspend fun applyOne(c: CompanionRadio, item: BackupItem, privateKey: ByteArray?) {
        val v = item.value
        val n by lazy { v.split(',').map { it.trim().toInt() } }   // numbers only for the settings that are numbers
        when (item.group) {
            BackupItem.GROUP_KEY -> c.setPrivateKey(privateKey ?: throw IllegalStateException("no private key"))
            BackupItem.GROUP_CHANNEL -> c.setChannel(byteArrayOf(item.id.toInt().toByte()) + unhex(v))
            BackupItem.GROUP_CONTACT -> c.addContact(unhex(v))
            BackupItem.GROUP_SETTING -> when {
                item.id == "name" -> c.setName(v)
                item.id == "radio" -> c.setRadio(n[0], n[1], n[2], n[3], n.getOrElse(4) { 0 })
                item.id == "tx" -> c.setTxPower(n[0])
                item.id == "latlon" -> c.setLatLon(n[0], n[1])
                item.id == "other" -> c.setOther(n[0], n[1], n[2], n[3])
                item.id == "pin" -> c.setPin(n[0])
                item.id == "hash" -> c.setHashMode(n[0])
                item.id == "tuning" -> c.setTuning(n[0], n[1])
                item.id == "autoadd" -> c.setAutoAdd(n[0], n[1])
                item.id == "scope" -> c.setFloodScope(unhex(v))
                item.id.startsWith("var.") -> c.setCustomVar(item.id.removePrefix("var."), v)
                else -> throw IllegalArgumentException(item.id)
            }
        }
    }
}

/** Thrown when the repeater stops answering its console part way. */
class NoAnswer : Exception("no answer")

/**
 * A repeater's settings through its console (`get` / `set`, the same on stock MeshCore), with the replies
 * taken straight from our companion firmware (CMD_CAPTURE_CLI_REPLIES). Not readable this way: the admin
 * password, the access list, the private key and the regions.
 */
object RepeaterBackup {
    /** `get <key>` and `set <key> <value>`; the last one, `ota`, only on our firmware. */
    val KEYS = listOf(
        "name", "radio", "tx", "af", "lat", "lon", "repeat", "advert.interval", "flood.advert.interval",
        "guest.password", "owner.info", "flood.max", "flood.max.unscoped", "flood.max.advert", "loop.detect",
        "allow.read.only", "int.thresh", "cad", "radio.rxgain", "rxdelay", "txdelay", "direct.txdelay",
        "agc.reset.interval", "path.hash.mode", "multi.acks",
    )
    const val OTA = "ota"

    /** The admin password: not readable from the repeater, the app keeps the one it logs in with. */
    const val PASSWORD = "password"

    /** Settings the repeater takes only after a restart. */
    val NEEDS_RESTART = setOf("radio")

    private const val REPLY_MS = 12_000L

    private var tags = 0

    /**
     * One console command and its reply; null when none came. The command starts with a 2-letter tag that
     * the repeater sends back, so a late reply to an earlier command cannot pass for this one.
     */
    internal suspend fun ask(c: CompanionRadio, r: Contact, command: String): String? {
        c.captureCli(60)   // kept on while the commands go, off again at the end
        c.dropCliReplies(r)
        val n = tags++ % (26 * 26)
        val tag = "${'a' + n / 26}${'a' + n % 26}"
        c.sendRemoteCli(r, "$tag|$command")
        return c.awaitCliReply(r, tag, REPLY_MS)
    }

    /** False: the companion cannot hand over console replies (stock firmware). */
    suspend fun supported(c: CompanionRadio) = c.captureCli(0)

    /**
     * [withFilter]: also the packet filter (`filter ...`, ids `filter.*`), when the firmware has one: its settings and
     * lists, so a restore brings the whole config back. Not for the settings editor (these are no `get` / `set` keys).
     */
    suspend fun read(
        c: CompanionRadio, r: Contact, withOta: Boolean, withFilter: Boolean = false, progress: (Int, Int) -> Unit,
    ): List<BackupItem> {
        val keys = if (withOta) KEYS + OTA else KEYS
        val total = keys.size + if (withFilter) RepeaterFilter.LOAD_QUERIES.size else 0
        val out = ArrayList<BackupItem>()
        var missed = 0
        try {
            for ((i, k) in keys.withIndex()) {
                progress(i + 1, total)
                val reply = ask(c, r, if (k == OTA) "ota status" else "get $k")
                if (reply == null) {
                    if (++missed >= 3) throw NoAnswer()
                    continue
                }
                missed = 0
                // "> value"; anything else ("??: key", "Error: unsupported") is a setting this repeater lacks
                if (!reply.startsWith("> ")) continue
                val v = reply.removePrefix("> ")
                out.add(setting(k, if (k == OTA) v.substringBefore(',').trim() else v))
            }
            // the filter is read whenever the console answered at all (a single lost reply above does not skip it)
            if (withFilter) out.addAll(readFilter(c, r, keys.size, total, progress))
        } finally {
            withContext(NonCancellable) { runCatching { c.captureCli(0) } }   // also after Stop
        }
        return out
    }

    /** One console command, asked up to [tries] times (a LoRa reply can be lost); null when none came. */
    private suspend fun askRetry(c: CompanionRadio, r: Contact, command: String, tries: Int = 3): String? {
        repeat(tries) { ask(c, r, command)?.let { return it } }
        return null
    }

    /**
     * The filter as backup items; nothing when the firmware has none (or stops answering: the settings are already read).
     * A list that is cut (a page that never came, a reply cut by the repeater's buffer) is kept as far as it was read and
     * marked [BackupItem.incomplete]; a query that got no answer at all adds the [RepeaterFilter.INCOMPLETE_ID] item, so a
     * partial backup is never taken for a whole one.
     */
    private suspend fun readFilter(c: CompanionRadio, r: Contact, offset: Int, total: Int, progress: (Int, Int) -> Unit): List<BackupItem> {
        var cfg = FilterConfig()
        val unanswered = ArrayList<String>()
        val incompleteLists = HashSet<FilterList>()
        for ((i, q) in RepeaterFilter.LOAD_QUERIES.withIndex()) {
            progress(offset + i + 1, total)
            val list = RepeaterFilter.listOfQuery(q)
            if (list != null) {
                var silent = false
                // a backup keeps the WHOLE keys (the short list shows only the first 8 bytes of a long one)
                val read = RepeaterFilter.readList(list, full = true) { cmd -> askRetry(c, r, cmd).also { if (it == null) silent = true } }
                if (read != null) {
                    cfg = RepeaterFilter.withList(cfg, list, read)
                    if (read.second) incompleteLists.add(list)
                } else if (silent) {
                    unanswered.add(q)
                    incompleteLists.add(list)
                }
                continue
            }
            val reply = askRetry(c, r, q)
            if (reply == null) {
                if (i == 0) return emptyList()   // no answer to `filter` at all: nothing to say about a filter
                unanswered.add(q)
                continue
            }
            if (i == 0 && (RepeaterFilter.isUnsupported(reply) || RepeaterFilter.parseStatus(reply) == null)) return emptyList()
            cfg = RepeaterFilter.apply(cfg, q, reply)
        }
        val out = ArrayList<BackupItem>()
        for ((name, v) in RepeaterFilter.backupScalars(cfg)) out.add(setting(RepeaterFilter.backupId(name), v))
        for (l in FilterList.entries) {
            val entries = cfg.lists[l].orEmpty()
            val inc = l in incompleteLists
            if (entries.isNotEmpty() || inc) {
                out.add(setting(RepeaterFilter.backupId("list.${l.cmd}"), entries.joinToString(",") { it.display }).copy(incomplete = inc))
            }
        }
        if (unanswered.isNotEmpty()) out.add(setting(RepeaterFilter.INCOMPLETE_ID, unanswered.joinToString(",")).copy(incomplete = true))
        return out
    }

    /** One filter item of a restore: a setting back, or a list's missing entries added; the reason in the Exception message. */
    private suspend fun applyFilter(c: CompanionRadio, r: Contact, item: BackupItem): String? {
        val name = item.id.removePrefix(RepeaterFilter.BACKUP_PREFIX)
        val cmds: List<String> = if (name.startsWith("list.")) {
            val list = FilterList.entries.firstOrNull { it.cmd == name.removePrefix("list.") } ?: return "?"
            // every page, whole keys for the block list: to tell which saved entries the repeater already has
            val cur: List<FilterEntry> = RepeaterFilter.readList(list, full = true) { ask(c, r, it) }?.first ?: return ""
            RepeaterFilter.restoreAdds(list, item.value, cur)
        } else {
            listOf(RepeaterFilter.restoreScalar(name, item.value) ?: return "?")
        }
        for (cmd in cmds) {
            val reply = ask(c, r, cmd) ?: return ""
            if (!RepeaterFilter.isOk(reply)) return RepeaterFilter.errorText(reply)
        }
        return null
    }

    /**
     * `get <key>` for each of [keys] after a write: what the repeater keeps now (it may round or normalize a value).
     * Null when any reply is missing or is no value, so the caller reads everything again later instead.
     */
    suspend fun readBack(c: CompanionRadio, r: Contact, keys: List<String>): Map<String, String>? {
        val out = LinkedHashMap<String, String>()
        try {
            for (k in keys) {
                val reply = ask(c, r, "get $k") ?: return null
                if (!reply.startsWith("> ")) return null
                out[k] = reply.removePrefix("> ")
            }
        } finally {
            withContext(NonCancellable) { runCatching { c.captureCli(0) } }   // also after Stop
        }
        return out
    }

    /** The order of a restore's filter items: lists first, the settings, `enabled` last (a half-restored filter never runs). */
    private fun filterRank(item: BackupItem): Int {
        val name = item.id.removePrefix(RepeaterFilter.BACKUP_PREFIX)
        return if (name.startsWith("list.")) 0 else 1 + RepeaterFilter.RESTORE_SCALAR_ORDER.indexOf(name).let { if (it < 0) 0 else it }
    }

    suspend fun apply(c: CompanionRadio, r: Contact, items: List<BackupItem>, progress: (Int, Int) -> Unit): RestoreResult {
        val failed = ArrayList<Pair<BackupItem, String>>()
        var done = 0
        try {
            // the admin password last: the rest still goes with the one logged in with
            val ordered = items.filter { !RepeaterFilter.isBackupId(it.id) && it.id != PASSWORD } +
                items.filter { RepeaterFilter.isBackupId(it.id) }.sortedBy { filterRank(it) } +
                items.filter { it.id == PASSWORD }
            for ((i, item) in ordered.withIndex()) {
                progress(i + 1, items.size)
                if (item.id == RepeaterFilter.INCOMPLETE_ID) continue   // a note about the backup, nothing to write
                if (RepeaterFilter.isBackupId(item.id)) {
                    val err = applyFilter(c, r, item)
                    if (err == null) done++ else failed.add(item to err)
                    continue
                }
                val pw = item.id == PASSWORD
                val reply = ask(c, r, if (pw) "password ${item.value}" else "set ${item.id} ${item.value}")
                when {
                    reply == null -> failed.add(item to "")
                    reply.startsWith(if (pw) "password now" else "OK") -> done++
                    else -> failed.add(item to reply)
                }
            }
        } finally {
            withContext(NonCancellable) { runCatching { c.captureCli(0) } }   // also after Stop
        }
        return RestoreResult(done, failed)
    }
}
