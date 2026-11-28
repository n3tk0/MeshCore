package io.github.n3tk0.loraota.ota

import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue
import kotlin.test.Test

class BootloaderInfoTest {
    @Test
    fun ours() {
        val b = BootloaderInfo.parse(
            "ours ver=0.9.2-OTAFIX2.3-BP1.4-mcota1 board=nRF52840-SeeedXiaoSense-v1 date=2026-10-22 sd=S140/6.1.1 feat=staged,rollback"
        )!!
        assertEquals(BootloaderInfo.Kind.OURS, b.kind)
        assertEquals("0.9.2-OTAFIX2.3-BP1.4-mcota1", b.version)
        assertEquals("nRF52840-SeeedXiaoSense-v1", b.board)
        assertEquals("2026-10-22", b.date)
        assertEquals("S140 6.1.1", b.softDevice)
        assertTrue(b.staged)
        assertTrue(b.rollback)
    }

    @Test
    fun stockAndOld() {
        val b = BootloaderInfo.parse("> otafix ver=0.9.2-OTAFIX2.3-BP1.4 board=nRF52840-ProMicro date=2025-03-01")!!
        assertEquals(BootloaderInfo.Kind.OTAFIX, b.kind)
        assertFalse(b.staged)
        assertNull(b.softDevice)
        assertEquals(BootloaderInfo.Kind.NOT_NRF52, BootloaderInfo.parse("n/a (no UF2 bootloader)")!!.kind)
        assertNull(BootloaderInfo.parse("Unknown command"))
        assertNull(BootloaderInfo.parse(""))
    }
}
