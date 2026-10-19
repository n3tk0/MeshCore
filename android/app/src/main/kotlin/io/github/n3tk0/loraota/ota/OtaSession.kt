package io.github.n3tk0.loraota.ota

import io.github.n3tk0.loraota.ble.CompanionRadio
import io.github.n3tk0.loraota.ble.Contact
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext

class OtaFailed(msg: String) : Exception(msg)

/**
 * One update: BEGIN on the normal channel, then both sides go to the side channel, DATA in windows
 * with a STATUS after each (lost packets are sent again from where the repeater is), END.
 */
class OtaSession(
    private val companion: CompanionRadio,
    private val repeater: Contact,
    private val radio: SideRadio,
    private val dutyCycle: Double,                   // 0.1 = 10 %
    private val log: (String) -> Unit,
    private val progress: (sent: Int, total: Int) -> Unit,
) {
    companion object {
        const val CHUNK = 128
        const val WINDOW = 8
        const val SESSION_TIMEOUT_SECS = 90           // repeater drops the session after this much silence
        const val SIDE_RADIO_DELAY_MS = 3500L         // repeater switches 2.5 s after its BEGIN reply
        const val COMPANION_LEASE_MINS = 2              // renewed with every window; back by itself if the app is gone
    }

    private var onSide = false

    private suspend fun request(data: ByteArray, timeoutMs: Long, tries: Int): OtaProtocol.Reply? {
        repeat(tries) {
            val tag = companion.sendRequest(repeater, data)
            val resp = companion.awaitResponse(tag, timeoutMs)
            if (resp != null) {
                val r = OtaProtocol.parse(resp)
                if (r != null && r.op == (data[1].toInt() and 0xff)) return r
            }
        }
        return null
    }

    private suspend fun sideLease() {
        val r = companion.cli("tempradio ${radio.cliArgs()} $COMPANION_LEASE_MINS")
        if (!r.contains("OK")) throw OtaFailed("the companion cannot use the side channel: $r")
    }

    private suspend fun pace(bytes: Int) {
        val t = radio.airtime(bytes)
        delay(((t / dutyCycle) * 1000).toLong())
    }

    private fun statusTimeoutMs(): Long = (radio.airtime(40) * 2 * 1000).toLong() + 4000

    suspend fun info(timeoutMs: Long = 20_000): OtaProtocol.Info {
        val r = request(OtaProtocol.info(), timeoutMs, 2) ?: throw OtaFailed("no answer from the repeater (is it next to you, with no hop?)")
        if (r.status != 0) throw OtaFailed(OtaProtocol.statusText(r.status))
        return OtaProtocol.parseInfo(r)
    }

    suspend fun run(old: ByteArray, new: ByteArray, patch: ByteArray) {
        val oldSha = io.github.n3tk0.loraota.delta.DetoolsPatch.sha256(old)
        val newSha = io.github.n3tk0.loraota.delta.DetoolsPatch.sha256(new)
        val total = (patch.size + CHUNK - 1) / CHUNK
        try {
            log("BEGIN")
            val begin = OtaProtocol.begin(patch.size, new.size, newSha, oldSha, CHUNK, radio, SESSION_TIMEOUT_SECS)
            val r = request(begin, 20_000, 2)
            if (r != null && r.status != 0) throw OtaFailed(OtaProtocol.statusText(r.status))
            // no reply may only mean the reply was lost: the repeater may already be on the side channel
            delay(SIDE_RADIO_DELAY_MS)
            sideLease()
            onSide = true
            log("side channel ${radio.cliArgs()}")

            var st = status() ?: throw OtaFailed("the repeater is not on the side channel")
            if (st.state != OtaProtocol.STATE_RECEIVING) throw OtaFailed("the repeater did not start the update")

            var next = st.nextSeq
            progress(next, total)
            while (next < total) {
                sideLease()
                val end = minOf(total, next + WINDOW)
                for (seq in next until end) {
                    val bytes = patch.copyOfRange(seq * CHUNK, minOf(patch.size, (seq + 1) * CHUNK))
                    companion.sendRequest(repeater, OtaProtocol.data(seq, bytes))
                    pace(requestPacketSize(4 + bytes.size))
                }
                st = status() ?: throw OtaFailed("lost the repeater")
                if (st.state == OtaProtocol.STATE_FAILED) throw OtaFailed("repeater stopped the update")
                if (st.state != OtaProtocol.STATE_RECEIVING) throw OtaFailed("the repeater dropped the update")
                if (st.nextSeq < next) log("resending from ${st.nextSeq}")
                next = st.nextSeq
                progress(next, total)
            }

            log("END")
            val e = request(OtaProtocol.end(), statusTimeoutMs(), 2)
            if (e != null && e.status != 0) throw OtaFailed(OtaProtocol.statusText(e.status))
            if (e == null) {
                // the reply may be what was lost: the repeater reboots into the new image on its own,
                // so ask it on the normal channel which firmware it runs
                log("no answer to END, asking the repeater on the normal channel")
                companion.cli("tempradio off")
                onSide = false
                delay(20_000)
                val i = info(30_000)
                if (!i.imageSha.contentEquals(newSha)) throw OtaFailed("no answer to END, the repeater runs its old firmware")
            }
            log("new firmware checked, the repeater restarts with it")
        } catch (e: Throwable) {
            if (onSide) withContext(NonCancellable) {
                runCatching { request(OtaProtocol.abort(), statusTimeoutMs(), 2) }
            }
            if (e is CancellationException) throw e
            throw e
        } finally {
            if (onSide) withContext(NonCancellable) {
                runCatching { companion.cli("tempradio off") }
                onSide = false
            }
        }
    }

    private suspend fun status(): OtaProtocol.Status? {
        val r = request(OtaProtocol.status(), statusTimeoutMs(), 4) ?: return null
        val s = OtaProtocol.parseStatus(r)
        if (s.state == OtaProtocol.STATE_FAILED) throw OtaFailed(OtaProtocol.statusText(r.status))
        return s
    }

    /** Estimated minutes for a delta of [patchSize] bytes. */
    fun estimateMinutes(patchSize: Int): Double {
        val packets = (patchSize + CHUNK - 1) / CHUNK
        val perPacket = radio.airtime(requestPacketSize(4 + CHUNK)) / dutyCycle
        val status = (packets / WINDOW + 1) * (radio.airtime(40) * 2 / dutyCycle)
        return (packets * perPacket + status) / 60.0
    }
}
