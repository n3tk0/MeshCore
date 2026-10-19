package io.github.n3tk0.loraota.ble

import kotlinx.coroutines.TimeoutCancellationException
import kotlinx.coroutines.withTimeout
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
}

class CompanionError(msg: String) : Exception(msg)

/** Companion radio commands the OTA needs (see examples/companion_radio/MyMesh.cpp). */
class CompanionRadio(private val link: CompanionLink) {
    private companion object {
        const val CMD_APP_START = 1
        const val CMD_GET_CONTACTS = 4
        const val CMD_ADD_UPDATE_CONTACT = 9
        const val CMD_SEND_LOGIN = 26
        const val CMD_SEND_BINARY_REQ = 50
        const val CMD_RUN_CLI_COMMAND = 66

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

    private suspend fun await(timeoutMs: Long, match: (ByteArray) -> Boolean): ByteArray {
        val it = pending.iterator()
        while (it.hasNext()) {
            val f = it.next()
            if (match(f)) {
                it.remove()
                return f
            }
        }
        return withTimeout(timeoutMs) {
            while (true) {
                val f = link.frames.receive()
                if (match(f)) return@withTimeout f
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

    private suspend fun command(frame: ByteArray, timeoutMs: Long = 5000, vararg codes: Int): ByteArray {
        link.send(frame)
        val f = await(timeoutMs) { code(it) in codes || code(it) == RESP_ERR }
        if (code(f) == RESP_ERR) throw CompanionError("companion error ${if (f.size > 1) f[1].toInt() else 0}")
        return f
    }

    suspend fun start() {
        pending.clear()
        while (link.frames.tryReceive().isSuccess) { }
        val name = "LoRa OTA".toByteArray()
        val f = command(byteArrayOf(CMD_APP_START.toByte(), 3, 0, 0, 0, 0, 0, 0) + name, 5000, RESP_SELF_INFO)
        // self info: ... name at the end (after the fixed fields)
        nodeName = if (f.size > 58) String(f, 58, f.size - 58, Charsets.UTF_8).trimEnd('\u0000') else ""
    }

    suspend fun contacts(): List<Contact> {
        link.send(byteArrayOf(CMD_GET_CONTACTS.toByte()))
        val out = ArrayList<Contact>()
        while (true) {
            val f = await(10_000) { code(it) in intArrayOf(2, RESP_CONTACT, RESP_END_OF_CONTACTS, RESP_ERR) }
            when (code(f)) {
                RESP_CONTACT -> if (f.size >= 148) out.add(Contact(f))
                RESP_END_OF_CONTACTS -> return out
                RESP_ERR -> throw CompanionError("contacts")
            }
        }
    }

    /** Admin login; true when the repeater accepted it with admin rights. */
    suspend fun login(contact: Contact, password: String): Boolean {
        command(byteArrayOf(CMD_SEND_LOGIN.toByte()) + contact.publicKey + password.toByteArray(), 5000, RESP_SENT)
        val prefix = contact.publicKey.copyOfRange(0, 6)
        val f = try {
            await(30_000) {
                (code(it) == PUSH_LOGIN_SUCCESS || code(it) == PUSH_LOGIN_FAIL) && it.size >= 8 &&
                    it.copyOfRange(2, 8).contentEquals(prefix)
            }
        } catch (e: TimeoutCancellationException) {
            throw CompanionError("no answer from the repeater")
        }
        return code(f) == PUSH_LOGIN_SUCCESS && (f[1].toInt() and 0x03) == 3   // PERM_ACL_ADMIN
    }

    /** Requests to this contact go with no hop: only a repeater next to us hears them, nobody repeats them. */
    suspend fun setZeroHop(contact: Contact) {
        val f = contact.frame.copyOf()
        f[0] = CMD_ADD_UPDATE_CONTACT.toByte()
        f[35] = 0
        command(f, 5000, RESP_OK)
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
    suspend fun awaitResponse(tag: Int, timeoutMs: Long): ByteArray? = try {
        val f = await(timeoutMs) {
            code(it) == PUSH_BINARY_RESPONSE && it.size >= 6 &&
                ByteBuffer.wrap(it, 2, 4).order(ByteOrder.LITTLE_ENDIAN).int == tag
        }
        f.copyOfRange(6, f.size)
    } catch (e: TimeoutCancellationException) {
        null
    }

    suspend fun cli(command: String): String {
        val f = command(byteArrayOf(CMD_RUN_CLI_COMMAND.toByte()) + command.toByteArray(), 5000, RESP_CLI_REPLY)
        return String(f, 1, f.size - 1, Charsets.UTF_8)
    }
}
