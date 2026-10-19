package io.github.n3tk0.loraota.ota

import kotlin.test.Test
import kotlin.test.assertEquals

class AirtimeBudgetTest {
    @Test
    fun tenPercentOfAnHour() {
        var now = 0L
        val b = AirtimeBudget(0.1) { now }
        assertEquals(360.0, b.limitSecs)
        assertEquals(0, b.waitMs(10.0))
        b.add(300.0)
        now = 1000
        b.add(50.0)
        assertEquals(0, b.waitMs(10.0))
        // 20 s more only fit when the first 300 s leave the hour
        assertEquals(3_599_000L, b.waitMs(20.0))
        now = 3_600_000
        assertEquals(0, b.waitMs(20.0))
        assertEquals(50.0, b.usedSecs())
    }

    @Test
    fun smallDeltaGoesInOneGo() {
        val b = AirtimeBudget(0.1) { 0L }
        // nRF: about 26 KB, a few minutes and no wait for the limit
        val m = estimateMinutes(SideRadio.DEFAULT, b, 26 * 1024)
        kotlin.test.assertTrue(m in 1.0..6.0, "minutes $m")
    }
}
