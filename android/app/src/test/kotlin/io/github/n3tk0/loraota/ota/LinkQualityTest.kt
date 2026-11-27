package io.github.n3tk0.loraota.ota

import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

class LinkQualityTest {
    @Test
    fun barsFollowTheSnrSteps() {
        assertEquals(0, LinkQuality.bars(null))
        assertEquals(5, LinkQuality.bars(12.0))
        assertEquals(5, LinkQuality.bars(10.0))
        assertEquals(4, LinkQuality.bars(9.75))
        assertEquals(4, LinkQuality.bars(5.0))
        assertEquals(3, LinkQuality.bars(4.75))
        assertEquals(3, LinkQuality.bars(0.0))
        assertEquals(2, LinkQuality.bars(-0.25))
        assertEquals(2, LinkQuality.bars(-7.5))
        assertEquals(1, LinkQuality.bars(-7.75))
        assertEquals(1, LinkQuality.bars(-20.0))
    }

    @Test
    fun colourFollowsTheBarCount() {
        assertEquals(BarTier.NONE, LinkQuality.tier(0))
        assertEquals(BarTier.RED, LinkQuality.tier(1))
        assertEquals(BarTier.ORANGE, LinkQuality.tier(2))
        assertEquals(BarTier.ORANGE, LinkQuality.tier(3))
        assertEquals(BarTier.OWN, LinkQuality.tier(4))
        assertEquals(BarTier.OWN, LinkQuality.tier(5))
    }

    @Test
    fun commandFrame() {
        val f = LinkTrace.command(0x01020304, 0, 0, byteArrayOf(0x7A))
        // CMD_SEND_TRACE_PATH, tag and auth little endian, flags, then the path
        assertContentEquals(byteArrayOf(36, 4, 3, 2, 1, 0, 0, 0, 0, 0, 0x7A), f)
        // the companion needs more than 10 bytes: at least one hash
        assertEquals(11, f.size)
    }

    @Test
    fun sentFrame() {
        val f = byteArrayOf(6, 0, 4, 3, 2, 1, 0xB8.toByte(), 0x0B, 0, 0)   // timeout 3000 ms
        val s = assertNotNull(LinkTrace.parseSent(f))
        assertEquals(0x01020304, s.tag)
        assertEquals(3000L, s.estTimeoutMs)
        assertNull(LinkTrace.parseSent(byteArrayOf(6, 0, 1)))
        assertNull(LinkTrace.parseSent(byteArrayOf(5, 0, 4, 3, 2, 1, 0, 0, 0, 0)))
    }

    /** The push as onTraceRecv writes it: [0x89][0][path len][flags][tag][auth][hashes][snrs][final snr]. */
    private fun push(tag: Int, flags: Int, hashes: ByteArray, snrs: IntArray, final: Int): ByteArray =
        byteArrayOf(0x89.toByte(), 0, hashes.size.toByte(), flags.toByte()) + LinkTrace.le(tag) + LinkTrace.le(0) +
            hashes + ByteArray(snrs.size) { snrs[it].toByte() } + byteArrayOf(final.toByte())

    @Test
    fun directTraceHasBothDirections() {
        // the repeater heard us at 7.25 dB (29), we heard it at -3.5 dB (-14)
        val f = push(0x55AA, 0, byteArrayOf(0x7A), intArrayOf(29), -14)
        val d = assertNotNull(LinkTrace.parseData(f))
        assertEquals(0x55AA, d.tag)
        assertEquals(0x55AA, LinkTrace.tagOf(f))
        assertEquals(listOf(7.25), d.snrs)
        assertEquals(-3.5, d.finalSnr)
        val r = assertNotNull(LinkTrace.reading(d, byteArrayOf(0x7A)))
        assertEquals(7.25, r.outSnr)
        assertEquals(-3.5, r.inSnr)
    }

    @Test
    fun twoByteHashesHaveOneSnrPerHop() {
        val f = push(9, 1, byteArrayOf(1, 2), intArrayOf(8), 20)   // path of 2 bytes = 1 hop
        val d = assertNotNull(LinkTrace.parseData(f))
        assertEquals(1, d.snrs.size)
        assertEquals(2.0, d.snrs[0])
        assertEquals(5.0, d.finalSnr)
    }

    @Test
    fun aLongerPathIsNotADirectReading() {
        val d = assertNotNull(LinkTrace.parseData(push(9, 0, byteArrayOf(1, 2), intArrayOf(8, 4), 20)))
        assertEquals(2, d.snrs.size)
        assertNull(LinkTrace.reading(d, byteArrayOf(1, 2)))
    }

    @Test
    fun hashSizeAndFlagsFollowTheFirmwareShift() {
        assertEquals(1, LinkTrace.hashSizeForMode(0))
        assertEquals(2, LinkTrace.hashSizeForMode(1))
        assertEquals(4, LinkTrace.hashSizeForMode(2))
        assertEquals(0, LinkTrace.flagsForHashSize(1))
        assertEquals(1, LinkTrace.flagsForHashSize(2))
        assertEquals(2, LinkTrace.flagsForHashSize(4))
        // four-byte hashes: one hop is 4 path bytes and flags 2 (path len shr 2)
        val h = byteArrayOf(1, 2, 3, 4)
        val d = assertNotNull(LinkTrace.parseData(push(9, 2, h, intArrayOf(8), 20)))
        assertEquals(1, d.snrs.size)
        assertNotNull(LinkTrace.reading(d, h))
    }

    @Test
    fun aPushThroughAnotherNodeIsNotOurs() {
        val ours = byteArrayOf(0x7A)
        val f = push(5, 0, byteArrayOf(0x7B), intArrayOf(29), -14)
        assertFalse(LinkTrace.isAnswer(f, 5, ours))
        assertTrue(LinkTrace.isAnswer(push(5, 0, ours, intArrayOf(29), -14), 5, ours))
        assertFalse(LinkTrace.isAnswer(push(6, 0, ours, intArrayOf(29), -14), 5, ours))
        assertNull(LinkTrace.reading(assertNotNull(LinkTrace.parseData(f)), ours))
    }

    @Test
    fun brokenFramesAreNotParsed() {
        assertNull(LinkTrace.parseData(byteArrayOf(0x89.toByte(), 0, 1, 0)))
        // says one hop but the SNR bytes are cut off
        assertNull(LinkTrace.parseData(push(1, 0, byteArrayOf(1), intArrayOf(4), 4).copyOf(13)))
        assertNull(LinkTrace.parseData(byteArrayOf(0x8C.toByte(), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)))
        assertNull(LinkTrace.tagOf(byteArrayOf(0x8C.toByte(), 0, 0, 0, 1, 0, 0, 0)))
    }

    @Test
    fun waitComesFromTheEstimateWithinLimits() {
        assertEquals(3000, LinkTrace.waitMs(100))
        assertEquals(4500, LinkTrace.waitMs(3000))
        assertEquals(8000, LinkTrace.waitMs(60_000))
    }

    @Test
    fun onlyAnUnhoppedRouteIsTraced() {
        assertTrue(LinkTrace.canTrace(0))
        assertFalse(LinkTrace.canTrace(1))
        assertFalse(LinkTrace.canTrace(3))
        assertFalse(LinkTrace.canTrace(null))
    }

    @Test
    fun traceNeedsRoomInTheAirtimeBudget() {
        var now = 0L
        val b = AirtimeBudget(0.01) { now }   // 36 s an hour
        val secs = SideRadio(869525, 250000, 11, 5).airtime(LinkTrace.packetBytes(1))
        assertTrue(secs > 0.05 && secs < 2.0, "airtime $secs")
        assertTrue(LinkTrace.fitsBudget(b, secs))
        b.add(35.9)
        assertFalse(LinkTrace.fitsBudget(b, secs))
        now = 3_600_001
        assertTrue(LinkTrace.fitsBudget(b, secs))
    }

    @Test
    fun followKeepsTheLastSamplesAndTheirStats() {
        val f = LinkFollow(limit = 3)
        assertNull(f.outStat())
        f.add(LinkSample(0, 2.0, 6.0))
        f.add(LinkSample(1, 4.0, null))
        f.add(LinkSample(2, -2.0, 8.0))
        f.add(LinkSample(3, 6.0, 4.0))   // the first one goes
        assertEquals(3, f.samples.size)
        val o = assertNotNull(f.outStat())
        assertEquals(-2.0, o.min)
        assertEquals(6.0, o.max)
        assertEquals(2.666, o.avg, 0.01)
        val i = assertNotNull(f.inStat())
        assertEquals(2, i.count)
        assertEquals(6.0, i.avg)
        f.clear()
        assertTrue(f.samples.isEmpty())
    }

    @Test
    fun twoMinutesAtFiveSecondsIsTwentyFourTraces() {
        assertEquals(5_000L, LinkFollow.INTERVAL_MS)
        assertEquals(24, LinkFollow.TICKS)
    }

    @Test
    fun asymmetryNeedsSixDb() {
        assertEquals(Asymmetry.NONE, LinkQuality.asymmetry(5.0, null))
        assertEquals(Asymmetry.NONE, LinkQuality.asymmetry(5.0, 0.25))
        assertEquals(Asymmetry.PHONE_WEAK, LinkQuality.asymmetry(6.0, 0.0))
        assertEquals(Asymmetry.REPEATER_WEAK, LinkQuality.asymmetry(-2.0, 4.0))
        assertEquals(Asymmetry.NONE, LinkQuality.asymmetry(-2.0, 3.75))
        assertEquals("+6.5", LinkQuality.shortSnr(6.5))
        assertEquals("-3.0", LinkQuality.shortSnr(-3.0))
    }

    private val rep = ByteArray(32) { (0x70 + it).toByte() }

    @Test
    fun planDirectIsTheRepeaterHash() {
        val p = assertNotNull(LinkTrace.plan(rep, 0, null, 1, 2))
        assertEquals(0, p.hops)
        assertContentEquals(byteArrayOf(0x70, 0x71), p.path)
        assertNull(LinkTrace.plan(rep, null, null, 1, 1))   // flood: no trace
    }

    @Test
    fun planGoesOutAndBackAlongTheOutPath() {
        val p = assertNotNull(LinkTrace.plan(rep, 2, byteArrayOf(0x11, 0x22), 1, 1))
        assertContentEquals(byteArrayOf(0x11, 0x22, 0x70, 0x22, 0x11), p.path)
        assertEquals(2, p.hops)
        assertContentEquals(byteArrayOf(0x70), p.target())
        assertEquals(2, p.hopHashes().size)
        // two-byte hashes keep their bytes together when reversed
        val q = assertNotNull(LinkTrace.plan(rep, 2, byteArrayOf(1, 2, 3, 4), 2, 1))
        assertContentEquals(byteArrayOf(1, 2, 3, 4, 0x70, 0x71, 3, 4, 1, 2), q.path)
        assertEquals(2, q.hashSize)
    }

    @Test
    fun planFallsBackWhenThereIsNoUsablePath() {
        assertNull(LinkTrace.plan(rep, 2, null, 1, 1))                       // route known, path not in the frame
        assertNull(LinkTrace.plan(rep, 3, byteArrayOf(1, 2), 1, 1))          // path shorter than the hops say
        assertNull(LinkTrace.plan(rep, 2, byteArrayOf(1, 2, 3, 4, 5, 6), 3, 1))   // 3-byte hashes: no trace flag for them
        // 64 bytes of hashes at most: 2k+1 hashes
        assertNotNull(LinkTrace.plan(rep, 31, ByteArray(31), 1, 1))
        assertEquals(63, LinkTrace.plan(rep, 31, ByteArray(31), 1, 1)!!.path.size)
        assertNull(LinkTrace.plan(rep, 32, ByteArray(32), 1, 1))             // 65 bytes
        assertNull(LinkTrace.plan(rep, 16, ByteArray(32), 2, 1))             // 33 hashes of 2 bytes
        assertEquals(1, LinkTrace.outHashSize(0x02))
        assertEquals(2, LinkTrace.outHashSize(0x42))
        assertEquals(3, LinkTrace.outHashSize(0x82))
    }

    @Test
    fun routeSplitsTheSnrsByDirectionAndLink() {
        val p = assertNotNull(LinkTrace.plan(rep, 2, byteArrayOf(0x11, 0x22), 1, 1))
        // R1 heard us 8, R2 heard R1 4, repeater heard R2 -2 | R2 heard repeater 6, R1 heard R2 -8 | we heard R1 10 (x4)
        val f = push(7, 0, p.path, intArrayOf(32, 16, -8, 24, -32), 40)
        val d = assertNotNull(LinkTrace.parseData(f))
        val r = assertNotNull(LinkTrace.route(d, p))
        assertEquals(3, r.hops.size)
        assertEquals(listOf(8.0, 4.0, -2.0), r.hops.map { it.out })
        // link 0 (us, R1): we heard R1 at 10; link 1 (R1, R2): R1 heard R2 at -8; link 2 (R2, repeater): R2 heard it at 6
        assertEquals(listOf(10.0, -8.0, 6.0), r.hops.map { it.inn })
        assertEquals(-2.0, r.weakestOut)
        assertEquals(-8.0, r.weakestIn)
    }

    @Test
    fun routeRejectsAnotherPathOrSnrCount() {
        val p = assertNotNull(LinkTrace.plan(rep, 1, byteArrayOf(0x11), 1, 1))
        val ok = assertNotNull(LinkTrace.parseData(push(1, 0, p.path, intArrayOf(1, 2, 3), 4)))
        assertNotNull(LinkTrace.route(ok, p))
        // a node with the same first hash further on is not the repeater we traced
        val other = assertNotNull(LinkTrace.parseData(push(1, 0, byteArrayOf(0x11, 0x71, 0x11), intArrayOf(1, 2, 3), 4)))
        assertNull(LinkTrace.route(other, p))
        // a direct reply for a multi-hop plan
        val direct = assertNotNull(LinkTrace.parseData(push(1, 0, byteArrayOf(0x70), intArrayOf(1), 4)))
        assertNull(LinkTrace.route(direct, p))
        // and a direct plan reads one link both ways
        val dp = assertNotNull(LinkTrace.plan(rep, 0, null, 1, 1))
        val r = assertNotNull(LinkTrace.route(direct, dp))
        assertEquals(0.25, r.hops.single().out)
        assertEquals(1.0, r.hops.single().inn)
        assertTrue(LinkTrace.isAnswer(push(1, 0, p.path, intArrayOf(1, 2, 3), 4), 1, p.path))
        assertFalse(LinkTrace.isAnswer(push(2, 0, p.path, intArrayOf(1, 2, 3), 4), 1, p.path))
    }
}
