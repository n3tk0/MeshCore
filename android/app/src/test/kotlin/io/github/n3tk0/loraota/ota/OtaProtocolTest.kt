package io.github.n3tk0.loraota.ota

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class OtaProtocolTest {
    @Test
    fun beginHasTheLayoutTheRepeaterReads() {
        val b = OtaProtocol.begin(1000, 2000, ByteArray(32) { 1 }, ByteArray(32) { 2 }, 128, SideRadio.DEFAULT, 90)
        assertEquals(2 + 86, b.size)   // LoraOta.h handleBegin: 85 bytes after type and op, then the flags
        assertEquals(0, b[87].toInt())
        val inPlace = OtaProtocol.begin(1000, 2000, ByteArray(32), ByteArray(32), 128, SideRadio.DEFAULT, 90, inPlace = true)
        assertEquals(OtaProtocol.BEGIN_IN_PLACE, inPlace[87].toInt())
        assertEquals(0x40, b[0].toInt())
        assertEquals(OtaProtocol.OP_BEGIN, b[1].toInt())
        assertEquals(128, b[74].toInt() and 0xff)
        assertEquals(869470, java.nio.ByteBuffer.wrap(b, 75, 4).order(java.nio.ByteOrder.LITTLE_ENDIAN).int)
    }

    @Test
    fun infoReadsTheInPlaceLimit() {
        val b = java.nio.ByteBuffer.allocate(2 + 3 + 4 + 32 + 4 + 4 + 4).order(java.nio.ByteOrder.LITTLE_ENDIAN)
        b.put(OtaProtocol.OP_INFO.toByte()).put(0).put(1).put(0).put(1).putInt(460_000).put(ByteArray(32))
        b.putInt(352_000).putInt(480_000).put("v1".toByteArray())
        val i = OtaProtocol.parseInfo(OtaProtocol.parse(b.array())!!)
        assertEquals(352_000, i.maxNew)
        assertEquals(480_000, i.maxInPlace)
        assertEquals("v1", i.ident)
        assertTrue(!i.inPlace(350_000) && i.inPlace(470_000) && !i.fits(490_000))
    }

    @Test
    fun dataFitsOneCompanionFrame() {
        val d = OtaProtocol.data(3, ByteArray(OtaSession.CHUNK))
        assertTrue(1 + 32 + d.size <= 176)       // CMD_SEND_BINARY_REQ frame, MAX_FRAME_SIZE
        assertTrue(requestPacketSize(d.size) <= 2 + 184)
    }

    @Test
    fun airtimeIsSane() {
        // SF7 / 125 kHz, 150 bytes, 32 symbol preamble: about 0.3 s
        val t = SideRadio.DEFAULT.airtime(150)
        assertTrue(t in 0.2..0.35, "airtime $t")
    }

    @Test
    fun cliArgs() {
        assertEquals("869.470 125 7 5", SideRadio.DEFAULT.cliArgs())
        assertEquals("869.618 62.5 8 8", SideRadio(869618, 62500, 8, 8).cliArgs())
    }
}
