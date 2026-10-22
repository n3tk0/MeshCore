package io.github.n3tk0.loraota.ota

import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull

class RepeaterStatusTest {
    @Test
    fun readsTheRepeaterStatsLayout() {
        // RepeaterStats (56 bytes) and the zero padding of the encrypted reply
        val b = ByteBuffer.allocate(60).order(ByteOrder.LITTLE_ENDIAN)
        b.putShort(4105).putShort(2).putShort(-112).putShort(-80)
        b.putInt(1234).putInt(567).putInt(720).putInt(-1)   // uptime above 2^31 s stays positive
        b.putInt(1).putInt(2).putInt(3).putInt(4)
        b.putShort(0).putShort(-30)
        val s = RepeaterStatus.parse(b.array())!!
        assertEquals(4105, s.battMv)
        assertEquals(2, s.txQueue)
        assertEquals(-112, s.noiseFloor)
        assertEquals(-80, s.lastRssi)
        assertEquals(1234, s.packetsRecv)
        assertEquals(567, s.packetsSent)
        assertEquals(720, s.airTimeSecs)
        assertEquals(0xffffffffL, s.upTimeSecs)
        assertEquals(-7.5, s.lastSnr)
    }

    @Test
    fun olderShorterStatusHasNoSnr() {
        val s = RepeaterStatus.parse(ByteArray(32))!!
        assertNull(s.lastSnr)
        assertNull(RepeaterStatus.parse(ByteArray(10)))
    }
}
