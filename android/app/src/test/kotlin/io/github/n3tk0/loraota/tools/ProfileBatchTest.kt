package io.github.n3tk0.loraota.tools

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class ProfileBatchTest {
    private val current = mapOf(
        "name" to "Hill", "lat" to "42.1", "radio" to "869.618,62.5,8,8", "tx" to "14", "af" to "1.0",
        "advert.interval" to "120", "repeat" to "on", "guest.password" to "x",
    )

    @Test
    fun onlyDifferingKeysAreWritten() {
        val p = ProfilePlanner.plan(mapOf("tx" to "14", "af" to "1.0", "repeat" to "off", "advert.interval" to "180"), current)
        assertEquals(mapOf("repeat" to "off", "advert.interval" to "180"), p.changes)
        assertTrue(p.invalid.isEmpty())
        assertFalse(p.unchanged)
    }

    @Test
    fun identicalProfileIsUnchangedEvenWithWhitespaceOrDecimalComma() {
        val p = ProfilePlanner.plan(mapOf("tx" to " 14 ", "af" to "1,0"), current)
        assertTrue(p.changes.isEmpty())
        assertTrue(p.unchanged)
    }

    @Test
    fun identityAndSecretsAreNeverWritten() {
        val p = ProfilePlanner.plan(
            mapOf("name" to "Other", "lat" to "1", "lon" to "2", "guest.password" to "pw", "password" to "p", "tx" to "10"), current,
        )
        assertEquals(mapOf("tx" to "10"), p.changes)
    }

    @Test
    fun valuesGoThroughTheEditorsValidation() {
        val p = ProfilePlanner.plan(mapOf("tx" to "99", "advert.interval" to "10", "af" to "2,5"), current)
        assertEquals(mapOf("af" to "2.5"), p.changes)
        assertEquals(setOf("tx", "advert.interval"), p.invalid.map { it.first }.toSet())
        assertFalse(p.unchanged)
    }

    @Test
    fun keysTheRepeaterLacksAreSkipped() {
        val p = ProfilePlanner.plan(mapOf("radio.rxgain" to "on", "tx" to "14"), current)
        assertEquals(listOf("radio.rxgain"), p.unsupported)
        assertTrue(p.unchanged)
    }

    @Test
    fun radioChangeNeedsRestart() {
        assertTrue(ProfilePlanner.needsRestart(ProfilePlanner.plan(mapOf("radio" to "868,125,9,5"), current)))
        assertFalse(ProfilePlanner.needsRestart(ProfilePlanner.plan(mapOf("tx" to "10"), current)))
    }

    @Test
    fun estimateRoundsUp() {
        assertEquals(0, ProfilePlanner.estimateMinutes(0))
        assertEquals(2, ProfilePlanner.estimateMinutes(1))
        assertEquals(10, ProfilePlanner.estimateMinutes(5))
    }

    @Test
    fun retryFailedPutsFailedAndSkippedBackInLine() {
        val a = BatchEntry("a", "A", BatchResult.APPLIED, "", 3)
        val b = BatchEntry("b", "B", BatchResult.FAILED, "no answer")
        val c = BatchEntry("c", "C", BatchResult.UNCHANGED)
        val d = BatchEntry("d", "D", BatchResult.SKIPPED, "no password")
        val s = ProfileBatchState("P", listOf(a, b, c, d))
        assertTrue(s.finished)
        assertEquals(4, s.done)
        assertEquals(3, s.appliedKeys)
        s.retryFailed()
        assertFalse(s.finished)
        assertEquals("b", s.next!!.key)
        assertEquals(BatchResult.PENDING, d.result)
        assertEquals("", b.note)
        assertEquals(BatchResult.APPLIED, a.result)
        assertEquals(BatchResult.UNCHANGED, c.result)
        assertNull(ProfileBatchState("P", listOf(a, c)).next)
    }
}
