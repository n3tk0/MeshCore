package io.github.n3tk0.loraota.backup

import io.github.n3tk0.loraota.ble.CompanionRadio
import io.github.n3tk0.loraota.ble.Contact
import io.github.n3tk0.loraota.ota.hex
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
            val field = ByteArray(32).also { name.toByteArray().copyInto(it, endIndex = minOf(31, name.toByteArray().size)) }
            out.add(BackupItem(BackupItem.GROUP_CHANNEL, i.toString(), name, hex(field + slot.copyOfRange(33, 49))))
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
            if (r.isSuccess) done++ else failed.add(item to (r.exceptionOrNull()?.message ?: ""))
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

    suspend fun read(c: CompanionRadio, r: Contact, withOta: Boolean, progress: (Int, Int) -> Unit): List<BackupItem> {
        val keys = if (withOta) KEYS + OTA else KEYS
        val out = ArrayList<BackupItem>()
        var missed = 0
        try {
            for ((i, k) in keys.withIndex()) {
                progress(i + 1, keys.size)
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
        } finally {
            withContext(NonCancellable) { runCatching { c.captureCli(0) } }   // also after Stop
        }
        return out
    }

    suspend fun apply(c: CompanionRadio, r: Contact, items: List<BackupItem>, progress: (Int, Int) -> Unit): RestoreResult {
        val failed = ArrayList<Pair<BackupItem, String>>()
        var done = 0
        try {
            // the admin password last: the rest still goes with the one logged in with
            for ((i, item) in items.sortedBy { it.id == PASSWORD }.withIndex()) {
                progress(i + 1, items.size)
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
