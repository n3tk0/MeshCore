package io.github.n3tk0.loraota.ble

import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withTimeoutOrNull
import java.nio.ByteBuffer
import java.nio.ByteOrder

class Contact(val frame: ByteArray) {
    val publicKey: ByteArray get() = frame.copyOfRange(1, 33)
    val type: Int get() = frame[33].toInt() and 0xff
    val outPathLen: Int get() = frame[35].toInt() and 0xff
    val name: String
        get() {
            val raw = frame.copyOfRange(36 + 64, 36 + 64 + 32)
            val end = raw.indexOf(0).let { if (it < 0) raw.size else it }
            return String(raw, 0, end, Charsets.UTF_8)
        }
    val isRepeater: Boolean get() = type == 2
    /** Hops on the route the companion knows; null: no route yet (messages flood). */
    val hops: Int? get() = if (outPathLen == 0xFF) null else outPathLen and 63
}

class CompanionError(msg: String) : Exception(msg)

/** Companion radio commands the OTA needs (see examples/companion_radio/MyMesh.cpp). */
class CompanionRadio(private val link: CompanionLink) {
    private companion object {
        const val CMD_APP_START = 1
        const val CMD_SEND_TXT_MSG = 2
        const val CMD_GET_CONTACTS = 4
        const val CMD_DEVICE_QUERY = 22
        const val CMD_ADD_UPDATE_CONTACT = 9
        const val CMD_RESET_PATH = 13
        const val CMD_GET_CONTACT_BY_KEY = 30
        const val CMD_SEND_LOGIN = 26
        const val CMD_SEND_BINARY_REQ = 50
        const val CMD_RUN_CLI_COMMAND = 66
        // the companion's own settings (backup and restore)
        const val CMD_SET_ADVERT_NAME = 8
        const val CMD_SET_RADIO_PARAMS = 11
        const val CMD_SET_RADIO_TX_POWER = 12
        const val CMD_SET_ADVERT_LATLON = 14
        const val CMD_SET_TUNING_PARAMS = 21
        const val CMD_EXPORT_PRIVATE_KEY = 23
        const val CMD_IMPORT_PRIVATE_KEY = 24
        const val CMD_GET_CHANNEL = 31
        const val CMD_SET_CHANNEL = 32
        const val CMD_SET_OTHER_PARAMS = 38
        const val CMD_GET_CUSTOM_VARS = 40
        const val CMD_SET_CUSTOM_VAR = 41
        const val CMD_GET_TUNING_PARAMS = 43
        const val CMD_SET_AUTOADD_CONFIG = 58
        const val CMD_GET_AUTOADD_CONFIG = 59
        const val CMD_SET_DEFAULT_FLOOD_SCOPE = 63
        const val CMD_GET_DEFAULT_FLOOD_SCOPE = 64
        const val CMD_SET_DEVICE_PIN = 37
        const val CMD_SET_PATH_HASH_MODE = 61
        const val CMD_CAPTURE_CLI_REPLIES = 120   // our companion firmware only
        const val CMD_GET_TELEMETRY_HISTORY = 121 // our companion firmware only

        const val TXT_TYPE_CLI_DATA = 1

        const val RESP_OK = 0
        const val RESP_ERR = 1
        const val RESP_CONTACT = 3
        const val RESP_END_OF_CONTACTS = 4
        const val RESP_SELF_INFO = 5
        const val RESP_SENT = 6
        const val RESP_DEVICE_INFO = 13
        const val RESP_CLI_REPLY = 29
        const val RESP_PRIVATE_KEY = 14
        const val RESP_DISABLED = 15
        const val RESP_CHANNEL_INFO = 18
        const val RESP_CUSTOM_VARS = 21
        const val RESP_TUNING_PARAMS = 23
        const val RESP_AUTOADD_CONFIG = 25
        const val RESP_DEFAULT_FLOOD_SCOPE = 28
        const val PUSH_LOGIN_SUCCESS = 0x85
        const val PUSH_LOGIN_FAIL = 0x86
        const val PUSH_BINARY_RESPONSE = 0x8C
        const val PUSH_CLI_REPLY_CAPTURED = 0xC0   // [code][sender prefix 6][timestamp 4][text]
        const val RESP_TELEMETRY_HISTORY = 0xC1    // [code] + a history piece (see HistorySeries)
    }

    var nodeName: String = ""
        private set

    /** The companion's answer to the app's start: its own settings (see [SelfInfo]). */
    var selfInfo: ByteArray = ByteArray(0)
        private set

    /** The device query's answer: PIN, client repeat and path hash mode are in it (see [deviceInfo]). */
    var deviceInfoRaw: ByteArray = ByteArray(0)
        private set

    /** The most channels it has (from the device query). */
    var maxChannels: Int = 0
        private set

    // frames that came while waiting for something else (a late response, a push)
    private val pending = ArrayDeque<ByteArray>()
    // one command and its reply at a time: a second caller would take the first one's reply
    private val lock = Mutex()

    private suspend fun await(timeoutMs: Long, match: (ByteArray) -> Boolean): ByteArray? {
        val it = pending.iterator()
        while (it.hasNext()) {
            val f = it.next()
            if (match(f)) {
                it.remove()
                return f
            }
        }
        // null on a timeout: a TimeoutCancellationException would look like the user's Stop
        return withTimeoutOrNull(timeoutMs) {
            while (true) {
                val f = link.frames.receive()
                if (match(f)) return@withTimeoutOrNull f
                if ((f[0].toInt() and 0xff) == PUSH_BINARY_RESPONSE || (f[0].toInt() and 0xff) == PUSH_CLI_REPLY_CAPTURED) {
                    pending.addLast(f)
                    while (pending.size > 32) pending.removeFirst()
                }
            }
            @Suppress("UNREACHABLE_CODE")
            ByteArray(0)
        }
    }

    private fun code(f: ByteArray) = f[0].toInt() and 0xff

    private suspend fun command(frame: ByteArray, timeoutMs: Long = 5000, vararg codes: Int): ByteArray = lock.withLock {
        link.send(frame)
        val f = await(timeoutMs) { code(it) in codes || code(it) == RESP_ERR } ?: throw CompanionError("no reply from the companion")
        if (code(f) == RESP_ERR) throw CompanionError("companion error ${if (f.size > 1) f[1].toInt() else 0}")
        f
    }

    suspend fun start() {
        pending.clear()
        while (link.frames.tryReceive().isSuccess) { }
        val name = "LoRa OTA".toByteArray()
        val f = command(byteArrayOf(CMD_APP_START.toByte(), 3, 0, 0, 0, 0, 0, 0) + name, 5000, RESP_SELF_INFO)
        selfInfo = f
        // self info: ... name at the end (after the fixed fields)
        nodeName = if (f.size > 58) String(f, 58, f.size - 58, Charsets.UTF_8).trimEnd('\u0000') else ""
    }

    /** The companion's board (manufacturer name) and firmware version; empty when it does not say. */
    suspend fun deviceInfo(): Pair<String, String> {
        val f = command(byteArrayOf(CMD_DEVICE_QUERY.toByte(), 3), 5000, RESP_DEVICE_INFO)
        // [code][ver][contacts/2][channels][pin 4][build date 12][manufacturer 40][version 20]
        deviceInfoRaw = f
        if (f.size > 3) maxChannels = f[3].toInt() and 0xff
        fun text(from: Int, len: Int): String {
            if (f.size <= from) return ""
            val raw = f.copyOfRange(from, minOf(f.size, from + len))
            val end = raw.indexOf(0).let { if (it < 0) raw.size else it }
            return String(raw, 0, end, Charsets.UTF_8)
        }
        return text(20, 40) to text(60, 20)
    }

    suspend fun contacts(): List<Contact> = lock.withLock {
        link.send(byteArrayOf(CMD_GET_CONTACTS.toByte()))
        val out = ArrayList<Contact>()
        var end = false
        while (!end) {
            val f = await(10_000) { code(it) in intArrayOf(2, RESP_CONTACT, RESP_END_OF_CONTACTS, RESP_ERR) }
                ?: throw CompanionError("contacts")
            when (code(f)) {
                RESP_CONTACT -> if (f.size >= 148) out.add(Contact(f))
                RESP_END_OF_CONTACTS -> end = true
                RESP_ERR -> throw CompanionError("contacts")
            }
        }
        out
    }

    /** Admin login; true when the repeater accepted it with admin rights. */
    suspend fun login(contact: Contact, password: String): Boolean {
        command(byteArrayOf(CMD_SEND_LOGIN.toByte()) + contact.publicKey + password.toByteArray(), 5000, RESP_SENT)
        val prefix = contact.publicKey.copyOfRange(0, 6)
        val f = lock.withLock { await(30_000) {
            (code(it) == PUSH_LOGIN_SUCCESS || code(it) == PUSH_LOGIN_FAIL) && it.size >= 8 &&
                it.copyOfRange(2, 8).contentEquals(prefix)
        } } ?: throw CompanionError("no answer from the repeater")
        // byte 1 is 'is admin'; newer repeaters also send the permissions (byte 7 of their reply, here 12)
        return code(f) == PUSH_LOGIN_SUCCESS &&
            (f[1].toInt() == 1 || (f.size > 12 && (f[12].toInt() and 0x03) == 3))
    }

    /** Forgets the route to the contact: the next message (the login) floods, and the route is learned again. */
    suspend fun resetPath(contact: Contact) {
        command(byteArrayOf(CMD_RESET_PATH.toByte()) + contact.publicKey, 5000, RESP_OK)
    }

    /** The contact as the companion has it now (with the route it learned). */
    suspend fun contact(publicKey: ByteArray): Contact? = try {
        Contact(command(byteArrayOf(CMD_GET_CONTACT_BY_KEY.toByte()) + publicKey, 5000, RESP_CONTACT))
    } catch (e: CompanionError) {
        null
    }

    /** Restores the path the contact had before. */
    suspend fun restorePath(contact: Contact) {
        val f = contact.frame.copyOf()
        f[0] = CMD_ADD_UPDATE_CONTACT.toByte()
        command(f, 5000, RESP_OK)
    }

    /** Sends a request; returns its tag (to match the response with). */
    suspend fun sendRequest(contact: Contact, data: ByteArray): Int {
        val f = command(byteArrayOf(CMD_SEND_BINARY_REQ.toByte()) + contact.publicKey + data, 5000, RESP_SENT)
        return ByteBuffer.wrap(f, 2, 4).order(ByteOrder.LITTLE_ENDIAN).int
    }

    /** The response to a request: the data after its tag, or null if none came in time. */
    suspend fun awaitResponse(tag: Int, timeoutMs: Long): ByteArray? {
        val f = lock.withLock {
            await(timeoutMs) {
                code(it) == PUSH_BINARY_RESPONSE && it.size >= 6 &&
                    ByteBuffer.wrap(it, 2, 4).order(ByteOrder.LITTLE_ENDIAN).int == tag
            }
        } ?: return null
        return f.copyOfRange(6, f.size)
    }

    /**
     * A command for the repeater's console (as an admin). Its reply goes to the companion's message
     * queue like any console reply, so the caller checks the result another way.
     */
    suspend fun sendRemoteCli(contact: Contact, text: String) {
        // the companion puts its own time on console commands
        val f = byteArrayOf(CMD_SEND_TXT_MSG.toByte(), TXT_TYPE_CLI_DATA.toByte(), 0, 0, 0, 0, 0) +
            contact.publicKey.copyOfRange(0, 6) + text.toByteArray()
        command(f, 5000, RESP_SENT)
    }

    suspend fun cli(command: String): String {
        val f = command(byteArrayOf(CMD_RUN_CLI_COMMAND.toByte()) + command.toByteArray(), 5000, RESP_CLI_REPLY)
        return String(f, 1, f.size - 1, Charsets.UTF_8)
    }

    /**
     * Console replies straight to the app for [secs] (0: back to the message queue), so reading a
     * repeater's settings leaves the user's unread messages alone. False: the companion has no such
     * command (stock firmware).
     */
    suspend fun captureCli(secs: Int): Boolean = try {
        command(byteArrayOf(CMD_CAPTURE_CLI_REPLIES.toByte(), secs.coerceIn(0, 120).toByte()), 5000, RESP_OK)
        true
    } catch (e: CompanionError) {
        false
    }

    /**
     * The contact's console reply that starts with "[tag]|" (a repeater sends back the 2-letter tag a
     * command starts with), while [captureCli] is on; without the tag. Null if none came in time.
     */
    suspend fun awaitCliReply(contact: Contact, tag: String, timeoutMs: Long): String? {
        val prefix = contact.publicKey.copyOfRange(0, 6)
        val head = "$tag|".toByteArray()
        val f = lock.withLock {
            await(timeoutMs) {
                code(it) == PUSH_CLI_REPLY_CAPTURED && it.size >= 11 + head.size && it.copyOfRange(1, 7).contentEquals(prefix) &&
                    it.copyOfRange(11, 11 + head.size).contentEquals(head)
            }
        } ?: return null
        return String(f, 11 + head.size, f.size - 11 - head.size, Charsets.UTF_8)
    }

    /** Console replies that came late (to a command that timed out): dropped, they would only pile up. */
    suspend fun dropCliReplies(contact: Contact) = lock.withLock {
        val prefix = contact.publicKey.copyOfRange(0, 6)
        fun his(f: ByteArray) = code(f) == PUSH_CLI_REPLY_CAPTURED && f.size >= 11 && f.copyOfRange(1, 7).contentEquals(prefix)
        while (true) {
            val f = link.frames.tryReceive().getOrNull() ?: break
            if (code(f) == PUSH_BINARY_RESPONSE || (code(f) == PUSH_CLI_REPLY_CAPTURED && !his(f))) pending.addLast(f)
        }
        pending.removeAll { his(it) }
    }

    /**
     * A piece of the companion's own telemetry history ([args]: see HistorySeries.args); null when its
     * firmware keeps none (stock, or built without it).
     */
    suspend fun history(args: ByteArray): ByteArray? = try {
        val f = command(byteArrayOf(CMD_GET_TELEMETRY_HISTORY.toByte()) + args, 5000, RESP_TELEMETRY_HISTORY, RESP_DISABLED)
        if (code(f) == RESP_TELEMETRY_HISTORY) f.copyOfRange(1, f.size) else null
    } catch (e: CompanionError) {
        null
    }

    // ---- the companion's own settings: backup and restore ----

    /** A command that answers OK (or an error). */
    private suspend fun ok(frame: ByteArray) {
        command(frame, 5000, RESP_OK)
    }

    private fun le(v: Int) = ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN).putInt(v).array()

    /** [rx delay, airtime factor], each x1000. */
    suspend fun tuning(): Pair<Int, Int> {
        val f = command(byteArrayOf(CMD_GET_TUNING_PARAMS.toByte()), 5000, RESP_TUNING_PARAMS)
        val b = ByteBuffer.wrap(f, 1, 8).order(ByteOrder.LITTLE_ENDIAN)
        return b.int to b.int
    }

    suspend fun setTuning(rx: Int, af: Int) = ok(byteArrayOf(CMD_SET_TUNING_PARAMS.toByte()) + le(rx) + le(af))

    /** [config, max hops]. */
    suspend fun autoAdd(): Pair<Int, Int> {
        val f = command(byteArrayOf(CMD_GET_AUTOADD_CONFIG.toByte()), 5000, RESP_AUTOADD_CONFIG)
        return (f[1].toInt() and 0xff) to (if (f.size > 2) f[2].toInt() and 0xff else 0)
    }

    suspend fun setAutoAdd(config: Int, maxHops: Int) =
        ok(byteArrayOf(CMD_SET_AUTOADD_CONFIG.toByte(), config.toByte(), maxHops.toByte()))

    /** The default flood scope: its name (31 bytes) and key (16), or empty for none. */
    suspend fun floodScope(): ByteArray {
        val f = command(byteArrayOf(CMD_GET_DEFAULT_FLOOD_SCOPE.toByte()), 5000, RESP_DEFAULT_FLOOD_SCOPE)
        return f.copyOfRange(1, f.size)
    }

    suspend fun setFloodScope(nameAndKey: ByteArray) = ok(byteArrayOf(CMD_SET_DEFAULT_FLOOD_SCOPE.toByte()) + nameAndKey)

    /** "name:value,name:value" (sensor and GPS settings). */
    suspend fun customVars(): String {
        val f = command(byteArrayOf(CMD_GET_CUSTOM_VARS.toByte()), 5000, RESP_CUSTOM_VARS)
        return String(f, 1, f.size - 1, Charsets.UTF_8)
    }

    suspend fun setCustomVar(name: String, value: String) =
        ok(byteArrayOf(CMD_SET_CUSTOM_VAR.toByte()) + "$name:$value".toByteArray())

    /** A channel slot: [idx][name 32][secret 16]; null when there is no such slot. */
    suspend fun channel(idx: Int): ByteArray? = try {
        command(byteArrayOf(CMD_GET_CHANNEL.toByte(), idx.toByte()), 5000, RESP_CHANNEL_INFO).copyOfRange(1, 50)
    } catch (e: CompanionError) {
        null
    }

    suspend fun setChannel(slot: ByteArray) = ok(byteArrayOf(CMD_SET_CHANNEL.toByte()) + slot)

    /** The 64-byte identity, or null when its firmware does not give it out. */
    suspend fun privateKey(): ByteArray? {
        val f = command(byteArrayOf(CMD_EXPORT_PRIVATE_KEY.toByte()), 5000, RESP_PRIVATE_KEY, RESP_DISABLED)
        return if (code(f) == RESP_PRIVATE_KEY && f.size >= 65) f.copyOfRange(1, 65) else null
    }

    suspend fun setPrivateKey(key: ByteArray) = ok(byteArrayOf(CMD_IMPORT_PRIVATE_KEY.toByte()) + key)

    suspend fun setName(name: String) = ok(byteArrayOf(CMD_SET_ADVERT_NAME.toByte()) + name.toByteArray())

    /** [repeat]: the client repeat goes with the radio settings (left out it would be turned off). */
    suspend fun setRadio(freqKhz: Int, bwHz: Int, sf: Int, cr: Int, repeat: Int) =
        ok(byteArrayOf(CMD_SET_RADIO_PARAMS.toByte()) + le(freqKhz) + le(bwHz) + byteArrayOf(sf.toByte(), cr.toByte(), repeat.toByte()))

    suspend fun setTxPower(dbm: Int) = ok(byteArrayOf(CMD_SET_RADIO_TX_POWER.toByte(), dbm.toByte()))

    suspend fun setLatLon(lat: Int, lon: Int) = ok(byteArrayOf(CMD_SET_ADVERT_LATLON.toByte()) + le(lat) + le(lon))

    /** [manual add, telemetry modes, advert location policy, multi acks]. */
    suspend fun setOther(manualAdd: Int, telemetry: Int, locPolicy: Int, multiAcks: Int) =
        ok(byteArrayOf(CMD_SET_OTHER_PARAMS.toByte(), manualAdd.toByte(), telemetry.toByte(), locPolicy.toByte(), multiAcks.toByte()))

    suspend fun setPin(pin: Int) = ok(byteArrayOf(CMD_SET_DEVICE_PIN.toByte()) + le(pin))

    suspend fun setHashMode(mode: Int) = ok(byteArrayOf(CMD_SET_PATH_HASH_MODE.toByte(), 0, mode.toByte()))

    /** Adds (or updates) a contact from its frame as [contacts] gives it. */
    suspend fun addContact(frame: ByteArray) = restorePath(Contact(frame))
}
