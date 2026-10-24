package io.github.n3tk0.loraota.ota

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class RepeaterTelemetryTest {
    private fun b(vararg v: Int) = ByteArray(v.size) { v[it].toByte() }

    // ch 1: 0.45 V, 24.5 C; ch 2 (INA219): 3.92 V, 0.035 A, 0 W
    private val stock = b(1, 116, 0x00, 0x2D, 1, 103, 0x00, 0xF5, 2, 116, 0x01, 0x88, 2, 117, 0x00, 0x23, 2, 128, 0, 0)

    @Test
    fun stockRepeaterHasNoDirection() {
        val bat = RepeaterTelemetry.parse(stock).battery()!!
        assertEquals(3.92, bat.volts, 0.001)
        assertEquals(0.035, bat.amps, 0.0001)
        assertFalse(bat.signed)
    }

    @Test
    fun ourFirmwareSaysCharging() {
        // + analog input -0.12 (signed, 0.01): charging at 120 mA
        val bat = RepeaterTelemetry.parse(stock + b(2, 2, 0xFF, 0xF4)).battery()!!
        assertTrue(bat.signed)
        assertEquals(-0.12, bat.amps, 0.0001)
    }

    @Test
    fun noSensorNoBattery() {
        assertNull(RepeaterTelemetry.parse(b(1, 116, 0x01, 0x90, 1, 103, 0x00, 0xF5)).battery())
    }
}
