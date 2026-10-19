package io.github.n3tk0.loraota.ota

import io.github.n3tk0.loraota.R
import io.github.n3tk0.loraota.ble.CompanionRadio
import io.github.n3tk0.loraota.ble.Contact
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext
import kotlin.math.ceil

/** An error for the user: a string resource and its arguments; [resumable]: the repeater keeps what it has. */
class OtaFailed(val res: Int, val args: List<Any> = emptyList(), val resumable: Boolean = false) : Exception("OtaFailed($res)")

enum class Phase { START, SWITCH, SEND, WAIT_AIR, FINISH, RESTART }

/**
 * One update: BEGIN on the normal channel, then both sides go to the side channel, DATA in windows
 * with a STATUS after each (lost packets are sent again from where the repeater is), END.
 * The same BEGIN again goes on with an update the repeater paused.
 */
class OtaSession(
    private val companion: CompanionRadio,
    private val repeater: Contact,
    private val radio: SideRadio,
    private val budget: AirtimeBudget,
    private val log: (String) -> Unit,
    private val phase: (Phase, waitUntil: Long) -> Unit,
    private val progress: (sent: Int, total: Int) -> Unit,
) {
    companion object {
        const val CHUNK = 128
        const val WINDOW = 8
        const val SESSION_TIMEOUT_SECS = 90           // repeater pauses the session after this much silence
        const val SIDE_RADIO_DELAY_MS = 3500L         // repeater switches 2.5 s after its BEGIN reply
        const val COMPANION_LEASE_MINS = 2            // renewed with every window; back by itself if the app is gone
        const val PAUSE_AFTER_MS = 45_000L            // a longer wait for time on air goes back to the normal channel
        const val KEEP_PAUSED_EVERY_MS = 15 * 60_000L // the repeater keeps a paused update 30 min from the last request
        const val PACKET_GAP = 2.0                    // the companion waits as long as a packet took (airtime factor 1)
    }

    private var onSide = false

    private suspend fun request(data: ByteArray, timeoutMs: Long, tries: Int): OtaProtocol.Reply? {
        repeat(tries) {
            if (onSide) budget.add(radio.airtime(requestPacketSize(data.size)))
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
        if (!r.contains("OK")) throw OtaFailed(R.string.err_side_channel, listOf(r.trim()), resumable = onSide)
    }

    private suspend fun normalRadio() {
        withContext(NonCancellable) { runCatching { companion.cli("tempradio off") } }
        onSide = false
    }

    private fun statusTimeoutMs(): Long = (radio.airtime(40) * 2 * 1000).toLong() + 4000

    suspend fun info(timeoutMs: Long = 20_000): OtaProtocol.Info {
        val r = request(OtaProtocol.info(), timeoutMs, 2) ?: throw OtaFailed(R.string.err_no_answer)
        if (r.status != 0) throw OtaFailed(OtaProtocol.statusRes(r.status))
        return OtaProtocol.parseInfo(r)
    }

    /** BEGIN (a new update or the paused one), then both on the side channel; where the repeater is. */
    private suspend fun start(begin: ByteArray): OtaProtocol.Status {
        phase(Phase.START, 0)
        log("BEGIN")
        val r = request(begin, 20_000, 2)
        if (r != null && r.status != 0) throw OtaFailed(OtaProtocol.statusRes(r.status))
        // no reply may only mean the reply was lost, or the repeater is still on the side channel
        phase(Phase.SWITCH, 0)
        delay(SIDE_RADIO_DELAY_MS)
        sideLease()
        onSide = true
        log("side channel ${radio.cliArgs()}")
        val st = status() ?: throw OtaFailed(if (r == null) R.string.err_no_answer else R.string.err_no_side, resumable = true)
        if (st.state != OtaProtocol.STATE_RECEIVING) throw OtaFailed(R.string.err_not_started)
        return st
    }

    /** Waits for time on air on the normal channel; the repeater keeps what it has. */
    private suspend fun pauseFor(waitMs: Long) {
        log("pause ${waitMs / 60_000} min for time on air")
        request(OtaProtocol.pause(), statusTimeoutMs(), 2)
        normalRadio()
        val until = System.currentTimeMillis() + waitMs
        phase(Phase.WAIT_AIR, until)
        while (true) {
            val left = until - System.currentTimeMillis()
            if (left <= 0) break
            delay(minOf(left, KEEP_PAUSED_EVERY_MS))
            if (until - System.currentTimeMillis() > 0) request(OtaProtocol.status(), 20_000, 1)   // keep it
        }
    }

    suspend fun run(old: ByteArray, new: ByteArray, patch: ByteArray) {
        val oldSha = io.github.n3tk0.loraota.delta.DetoolsPatch.sha256(old)
        val newSha = io.github.n3tk0.loraota.delta.DetoolsPatch.sha256(new)
        val total = (patch.size + CHUNK - 1) / CHUNK
        val begin = OtaProtocol.begin(patch.size, new.size, newSha, oldSha, CHUNK, radio, SESSION_TIMEOUT_SECS)
        var stopped = false
        try {
            var st = start(begin)
            var next = st.nextSeq
            progress(next, total)
            while (next < total) {
                val all = (next until minOf(total, next + WINDOW)).map { seq -> patch.copyOfRange(seq * CHUNK, minOf(patch.size, (seq + 1) * CHUNK)) }
                fun needFor(n: Int) = all.take(n).sumOf { radio.airtime(requestPacketSize(4 + it.size)) } + radio.airtime(requestPacketSize(2))
                // a window must fit in one hour's budget, or the wait for it never ends
                var count = all.size
                while (count > 1 && needFor(count) > budget.limitSecs) count--
                val need = needFor(count)
                if (need > budget.limitSecs) throw OtaFailed(R.string.err_air_too_small)
                val packets = all.take(count)
                val wait = budget.waitMs(need)
                if (wait > PAUSE_AFTER_MS) {
                    pauseFor(wait)
                    st = start(begin)
                    next = st.nextSeq
                    continue
                }
                if (wait > 0) {
                    phase(Phase.WAIT_AIR, System.currentTimeMillis() + wait)
                    delay(wait)
                }
                phase(Phase.SEND, 0)
                sideLease()
                packets.forEachIndexed { i, bytes ->
                    val t = radio.airtime(requestPacketSize(4 + bytes.size))
                    budget.add(t)
                    companion.sendRequest(repeater, OtaProtocol.data(next + i, bytes))
                    delay((t * PACKET_GAP * 1000).toLong())
                }
                st = status() ?: lost()
                if (st.state != OtaProtocol.STATE_RECEIVING) throw OtaFailed(R.string.err_dropped)
                if (st.nextSeq < next) log("resending from ${st.nextSeq}")
                next = st.nextSeq
                progress(next, total)
            }

            phase(Phase.FINISH, 0)
            log("END")
            var e: OtaProtocol.Reply? = null
            for (attempt in 1..3) {
                e = request(OtaProtocol.end(), statusTimeoutMs(), 2)
                if (e != null) break
                // the END or its reply was lost: READY means it got there, RECEIVING means send it again
                val s = status() ?: break
                if (s.state != OtaProtocol.STATE_RECEIVING) break
                log("END again")
            }
            if (e != null && e.status != 0) {
                stopped = true   // the repeater gave up on this one
                throw OtaFailed(OtaProtocol.statusRes(e.status))
            }
            phase(Phase.RESTART, System.currentTimeMillis() + 30_000)
            normalRadio()
            if (e == null) log("no answer to END, asking the repeater on the normal channel")
            // the repeater restarts 10 s after END; ask which firmware it runs (this also confirms it on ESP32)
            delay(30_000)
            val i = runCatching { info(30_000) }.getOrNull() ?: runCatching { info(30_000) }.getOrNull()
                ?: throw OtaFailed(R.string.err_after_restart)
            // the END never got there: the repeater paused the update and keeps it
            if (i.state == OtaProtocol.STATE_PAUSED) throw OtaFailed(R.string.err_end_lost, resumable = true)
            if (!i.imageSha.contentEquals(newSha)) throw OtaFailed(R.string.err_old_still_runs, listOf(i.ident))
            log("the repeater runs the new firmware")
        } catch (e: Throwable) {
            if (e is CancellationException && onSide) {
                // stopped by the user: drop it on the repeater too
                withContext(NonCancellable) { runCatching { request(OtaProtocol.abort(), statusTimeoutMs(), 2) } }
                stopped = true
            }
            throw if (e is OtaFailed || e is CancellationException || stopped) e
                  else OtaFailed(R.string.err_paused, listOf(e.message ?: e.toString()), resumable = true)
        } finally {
            if (onSide) normalRadio()
        }
    }

    /**
     * No answer on the side channel. A repeater that gave up (patch or flash error) is back on the
     * normal channel: ask there, so such an error is not offered as one to go on with.
     */
    private suspend fun lost(): Nothing {
        normalRadio()
        val r = request(OtaProtocol.status(), 20_000, 1)
        if (r != null && OtaProtocol.parseStatus(r).state == OtaProtocol.STATE_FAILED)
            throw OtaFailed(OtaProtocol.statusRes(r.status))
        throw OtaFailed(R.string.err_lost, resumable = true)
    }

    private suspend fun status(): OtaProtocol.Status? {
        val r = request(OtaProtocol.status(), statusTimeoutMs(), 4) ?: return null
        val s = OtaProtocol.parseStatus(r)
        if (s.state == OtaProtocol.STATE_FAILED) throw OtaFailed(OtaProtocol.statusRes(r.status))
        return s
    }
}

/** Estimated minutes for a delta of [patchSize] bytes, with the hourly limit on time on air. */
fun estimateMinutes(radio: SideRadio, budget: AirtimeBudget, patchSize: Int): Double {
    val packets = (patchSize + OtaSession.CHUNK - 1) / OtaSession.CHUNK
    val perPacket = radio.airtime(requestPacketSize(4 + OtaSession.CHUNK))
    val statuses = (packets / OtaSession.WINDOW + 1) * radio.airtime(requestPacketSize(2))
    val onAir = packets * perPacket + statuses
    val sending = packets * perPacket * OtaSession.PACKET_GAP + (packets / OtaSession.WINDOW + 1) * 3.0 + 40
    val free = (budget.limitSecs - budget.usedSecs()).coerceAtLeast(0.0)
    val extraHours = if (onAir <= free) 0.0 else ceil((onAir - free) / budget.limitSecs)
    return (sending + extraHours * 3600) / 60.0
}
