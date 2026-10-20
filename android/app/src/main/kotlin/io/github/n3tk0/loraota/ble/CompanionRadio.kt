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
        const val CMD_ADD_UPDATE_CONTACT = 9
        const val CMD_RESET_PATH = 13
        const val CMD_GET_CONTACT_BY_KEY = 30
        const val CMD_SEND_LOGIN = 26
        const val CMD_SEND_BINARY_REQ = 50
        const val CMD_RUN_CLI_COMMAND = 66

        const val TXT_TYPE_CLI_DATA = 1

        const val RESP_OK = 0
        const val RESP_ERR = 1
        const val RESP_CONTACT = 3
        const val RESP_END_OF_CONTACTS = 4
        const val RESP_SELF_INFO = 5
        const val RESP_SENT = 6
        const val RESP_CLI_REPLY = 29
        const val PUSH_LOGIN_SUCCESS = 0x85
        const val PUSH_LOGIN_FAIL = 0x86
        const val PUSH_BINARY_RESPONSE = 0x8C
    }

    var nodeName: String = ""
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
                if ((f[0].toInt() and 0xff) == PUSH_BINARY_RESPONSE) {
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
        // self info: ... name at the end (after the fixed fields)
        nodeName = if (f.size > 58) String(f, 58, f.size - 58, Charsets.UTF_8).trimEnd('\u0000') else ""
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
}
