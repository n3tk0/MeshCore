package io.github.n3tk0.loraota.ota

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull
import kotlin.test.assertTrue

class OtaQueueTest {
    private fun fw(release: String, env: String, sha: String, ota: Boolean = true) =
        FirmwareFile(release, env, release, if (ota) "$env.bin" else "", 1, sha, "")

    @Test
    fun keptAndReadBackGoesOnWhereItWas() {
        val q = OtaQueueState("xiao-repeater-v2", "v2", listOf(QueueEntry("aa", "Roof"), QueueEntry("bb", "Hill")))
        q.entries[0].result = QueueResult.DONE
        q.entries[0].note = "v2 ok"
        val back = OtaQueueState.fromJson(q.toJson())!!
        assertEquals("xiao-repeater-v2", back.release)
        assertEquals(QueueResult.DONE, back.entries[0].result)
        assertEquals("v2 ok", back.entries[0].note)
        assertEquals("bb", back.next!!.key)
        back.entries[1].result = QueueResult.FAILED
        assertTrue(back.finished)
        back.retryFailed()
        assertEquals("bb", back.next!!.key)
        assertEquals(QueueResult.DONE, back.entries[0].result)
        assertNull(OtaQueueState.fromJson("not json"))
        assertNull(OtaQueueState.fromJson(null))
    }

    @Test
    fun directFirstThenByHopsUnknownLast() {
        val items = listOf("c" to null, "b" to 2, "z" to 0, "a" to 0)
        val order = OtaQueueState.order(items, { it.second }, { it.first }).map { it.first }
        assertEquals(listOf("a", "z", "b", "c"), order)
    }

    @Test
    fun targetIsTheSameBoardInThePickedRelease() {
        val files = listOf(fw("r2", "Xiao_nrf52_repeater", "n2"), fw("r2", "Xiao_C3_repeater", "c2"),
            fw("r1", "Xiao_nrf52_repeater", "n1"), fw("r1", "Faketec", "f1", ota = false))
        assertEquals(listOf("r2", "r1"), OtaQueueState.releases(files).map { it.first })
        val running = files[2]
        assertEquals("n2", OtaQueueState.target(files, "r2", running)!!.sha256)
        assertEquals("n1", OtaQueueState.target(files, "r1", running)!!.sha256)
        assertNull(OtaQueueState.target(files, "r3", running))
    }
}
