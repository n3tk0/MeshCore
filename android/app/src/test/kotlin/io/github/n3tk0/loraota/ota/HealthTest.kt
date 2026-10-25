package io.github.n3tk0.loraota.ota

import kotlinx.coroutines.runBlocking
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.file.Files
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

class HealthTest {
    @Test
    fun parsesAGoodSelfTest() {
        val t = SelfTest.parse("ok radio=ok rx=120 tx=40 nf=-112 i2c=ok qspi=ok batt=3987mV/72% left=41h day=+420/-310 mail=3 ota=on ps=normal up=3d04h")!!
        assertTrue(t.ok)
        assertTrue(t.problems.isEmpty())
        assertEquals(3987, t.batteryMv)
        assertEquals(72, t.batteryPercent)
        assertEquals(41, t.hoursLeft)
        assertEquals(3, t.mailbox)
        assertEquals("normal", t.powerStep)
        assertTrue(HealthCheck.warnings(t, null).isEmpty())
    }

    @Test
    fun parsesWarningsAsTheFirmwareWritesThem() {
        // "WARN " + "i2c,batt," (a comma after each), then the values
        val t = SelfTest.parse("WARN i2c,batt, radio=ok rx=1 tx=2 nf=-110 i2c=off:1 batt=3420mV left=chg mail=off ota=on,new ps=critical up=0d01h")!!
        assertFalse(t.ok)
        assertEquals(listOf("i2c", "batt"), t.problems)
        assertNull(t.batteryPercent)
        assertEquals(-2, t.hoursLeft)
        assertNull(t.mailbox)
        val before = SelfTest.parse("ok radio=ok i2c=ok ps=normal")
        val w = HealthCheck.warnings(t, before)
        assertTrue(HealthWarning.I2C_OFF in w)
        assertTrue(HealthWarning.I2C_NEW_OFF in w)
        assertTrue(HealthWarning.BATTERY_STEP in w)
        assertTrue(HealthWarning.NEW_FIRMWARE in w)
        // off the time before too: not new
        assertFalse(HealthWarning.I2C_NEW_OFF in HealthCheck.warnings(t, t))
    }

    @Test
    fun oneProblemPerThingWorstFirst() {
        val p = HealthCheck.problems(listOf(HealthWarning.I2C_OFF, HealthWarning.I2C_NEW_OFF, HealthWarning.NEW_FIRMWARE, HealthWarning.BATTERY_STEP))
        assertEquals(listOf(HealthWarning.BATTERY_STEP, HealthWarning.I2C_OFF, HealthWarning.NEW_FIRMWARE), p.map { it.warning })
        assertEquals(listOf(HealthSeverity.DANGER, HealthSeverity.WARN, HealthSeverity.INFO), p.map { it.severity })
        assertTrue(p[1].isNew)
    }

    @Test
    fun fieldsWithValuesComeAfterTheValues() {
        val b = piece(0, 2, 0, listOf(1, 2)) + byteArrayOf(0x81.toByte(), 0x00)
        assertEquals(0x81, HistorySeries.parsePiece(b)!!.fields)
        assertNull(HistorySeries.parsePiece(piece(0, 2, 0, listOf(1, 2)))!!.fields)
    }

    @Test
    fun battSaveAndCapacityReplies() {
        val b = BattSave.parse("on, step normal, 3987 mV ext; low 3600 crit 3450 off 0")!!
        assertTrue(b.enabled && b.external)
        assertEquals(3987, b.mv)
        assertEquals(0, b.off)
        assertTrue(b.matches(BattSave.recommended(false)))
        assertFalse(b.matches(BattSave.recommended(true)))
        assertEquals("battsave 3600 3450 3300", BattSave.command(BattSave.recommended(true)))
        assertNull(BattSave.parse("Unknown command"))
        assertEquals(2000, BattCapacity.parse("> 2000 mAh (learnt)")!!.mah)
        assertTrue(BattCapacity.parse("> 2000 mAh (learnt)")!!.learnt)
        assertNull(BattCapacity.parse("Err - no current sensor (INA219) on the battery"))
    }

    @Test
    fun olderRepeaterAnswerIsNotASelfTest() {
        assertNull(SelfTest.parse("Unknown command"))
        assertNull(SelfTest.parse(""))
    }

    @Test
    fun fasterDrainNeedsTwiceTheWeeksFall() {
        // the week: 0.5 %/h; the last 6 h: 2 %/h (every 10 minutes a third of a percent, as whole steps)
        val week = (0 until 48).map { 90 - it / 2 }
        val fast = (0 until 36).map { 80 - it / 3 }
        val usual = (0 until 36).map { 80 - it / 12 }
        assertTrue(HealthCheck.fasterDrain(fast, week))
        assertFalse(HealthCheck.fasterDrain(usual, week))
        // charging steps (a rise) are left out, and too few samples say nothing
        assertNull(HealthCheck.fallPerHour(listOf(50, 51, 52), 1.0))
        assertFalse(HealthCheck.fasterDrain(fast, listOf(null, 50, null)))
    }

    private fun piece(ring: Int, total: Int, start: Int, values: List<Int?>, time: Int = 1_760_000_000): ByteArray {
        val b = ByteBuffer.allocate(12 + 2 * values.size).order(ByteOrder.LITTLE_ENDIAN)
        b.put(ring.toByte()).put(HistorySeries.F_BATT_MV.toByte()).putShort(total.toShort()).putShort(start.toShort())
        b.put(values.size.toByte()).putInt(time).put(if (ring == 0) 10 else 60)
        for (v in values) b.putShort((v ?: HistorySeries.NONE).toShort())
        return b.array()
    }

    @Test
    fun loadsASeriesPieceByPiece() = runBlocking {
        val all = (0 until 100).map { if (it == 5) null else 3700 + it }
        val asked = ArrayList<Pair<Int, Int>>()
        val s = HistorySeries.load(HistorySeries.RING_DAY, HistorySeries.F_BATT_MV) { start, max ->
            asked.add(start to max)
            piece(0, all.size, start, all.subList(start, minOf(all.size, start + max)))
        }!!
        assertEquals(listOf(0 to 64, 64 to 64), asked)
        assertEquals(all, s.values)
        assertEquals(10, s.intervalMinutes)
        assertEquals(1_760_000_000L, s.timeOf(99))
        assertEquals(1_760_000_000L - 99 * 600, s.timeOf(0))
        assertEquals(3.7, HistorySeries.display(HistorySeries.F_BATT_MV, 3700))
        assertEquals(1013.2, HistorySeries.display(HistorySeries.F_PRESSURE, 1132), 0.001)
    }

    @Test
    fun noAnswerIsNoHistoryButALostLaterPieceKeepsWhatCame() = runBlocking {
        assertNull(HistorySeries.load(0, 0) { _, _ -> null })
        val s = HistorySeries.load(0, 0) { start, _ -> if (start == 0) piece(0, 100, 0, List(64) { 1 }) else null }
        assertNotNull(s)
        assertEquals(64, s.values.size)
        // a node without a clock: no times
        val noClock = HistorySeries.load(0, 0) { _, _ -> piece(0, 2, 0, listOf(1, 2), time = 0) }!!
        assertNull(noClock.timeOf(0))
    }

    @Test
    fun storeKeepsTheNewestPerDevice() {
        val dir = Files.createTempDirectory("health").toFile()
        try {
            val st = HealthStore(dir)
            for (i in 0 until HealthStore.MAX + 5) st.add("aa", "ok n=$i", i.toLong())
            st.add("bb", "ok", 1)
            val a = st.records("aa")
            assertEquals(HealthStore.MAX, a.size)
            assertEquals("ok n=5", a.first().line)
            assertEquals("ok n=${HealthStore.MAX + 4}", a.last().line)
            assertEquals(1, st.records("bb").size)
            assertTrue(st.records("cc").isEmpty())
        } finally {
            File(dir, "health.json").delete()
            dir.delete()
        }
    }
}
