package io.github.n3tk0.loraota.tools

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

class RepeaterValidationTest {

    @Test
    fun validatesRepeaterSettingsAgainstCommonCliBounds() {
        assertNull(RepeaterSettingsValidator.validate("name", "Vitosha-Peak-R1"))
        assertEquals(SettingValidationError.NAME_EMPTY, RepeaterSettingsValidator.validate("name", "   "))
        assertEquals(SettingValidationError.NAME_BAD_CHARS, RepeaterSettingsValidator.validate("name", "Node:1"))
        assertEquals(SettingValidationError.NAME_BAD_CHARS, RepeaterSettingsValidator.validate("name", "Node[A]"))
        assertEquals(SettingValidationError.NAME_TOO_LONG, RepeaterSettingsValidator.validate("name", "a".repeat(32)))

        assertNull(RepeaterSettingsValidator.validate("tx", "22"))
        assertNull(RepeaterSettingsValidator.validate("tx", "-9"))
        assertEquals(SettingValidationError.TX_RANGE, RepeaterSettingsValidator.validate("tx", "31"))
        assertEquals(SettingValidationError.TX_RANGE, RepeaterSettingsValidator.validate("tx", "abc"))

        assertNull(RepeaterSettingsValidator.validate("lat", "42.69770"))
        assertEquals(SettingValidationError.LAT_RANGE, RepeaterSettingsValidator.validate("lat", "95.0"))
        assertNull(RepeaterSettingsValidator.validate("lon", "23.32190"))
        assertEquals(SettingValidationError.LON_RANGE, RepeaterSettingsValidator.validate("lon", "-181.0"))

        assertNull(RepeaterSettingsValidator.validate("advert.interval", "0"))
        assertNull(RepeaterSettingsValidator.validate("advert.interval", "120"))
        assertEquals(SettingValidationError.ADVERT_RANGE, RepeaterSettingsValidator.validate("advert.interval", "30"))
        assertEquals(SettingValidationError.ADVERT_RANGE, RepeaterSettingsValidator.validate("advert.interval", "300"))

        assertNull(RepeaterSettingsValidator.validate("flood.advert.interval", "0"))
        assertNull(RepeaterSettingsValidator.validate("flood.advert.interval", "12"))
        assertEquals(SettingValidationError.FLOOD_ADVERT_RANGE, RepeaterSettingsValidator.validate("flood.advert.interval", "2"))
        assertEquals(SettingValidationError.FLOOD_ADVERT_RANGE, RepeaterSettingsValidator.validate("flood.advert.interval", "200"))

        assertNull(RepeaterSettingsValidator.validate("af", "1.5"))
        assertEquals(SettingValidationError.AF_RANGE, RepeaterSettingsValidator.validate("af", "12"))
    }

    @Test
    fun buildsValidTempRadioCommandAndRejectsInvalidParams() {
        val cmd = RepeaterSettingsValidator.buildTempRadioCommand("869,525", "250", "7", "5", "5")
        assertNotNull(cmd)
        assertEquals("tempradio 869.525 250.0 7 5 5", cmd)

        assertNull(RepeaterSettingsValidator.buildTempRadioCommand("50", "250", "7", "5", "5"))
        assertNull(RepeaterSettingsValidator.buildTempRadioCommand("869.5", "250", "13", "5", "5"))
        assertNull(RepeaterSettingsValidator.buildTempRadioCommand("869.5", "250", "7", "9", "5"))
        assertNull(RepeaterSettingsValidator.buildTempRadioCommand("869.5", "250", "7", "5", "0"))
        assertNull(RepeaterSettingsValidator.buildTempRadioCommand("869.5", "250", "7", "5", "61"))
    }

    @Test
    fun fleetFilterLogicMatchesDirectIssuesAndPinned() {
        val okLine = "ok radio=ok i2c=ok batt=4000mV/80% ps=normal"
        val warnLine = "WARN i2c radio=ok i2c=off:1 batt=3700mV/40% ps=normal"

        assertTrue(FleetFilterLogic.matches(FleetFilter.DIRECT, hops = 0, isPinned = false, latestHealthLine = okLine))
        assertFalse(FleetFilterLogic.matches(FleetFilter.DIRECT, hops = 1, isPinned = false, latestHealthLine = okLine))

        assertTrue(FleetFilterLogic.matches(FleetFilter.ISSUES, hops = 1, isPinned = false, latestHealthLine = warnLine))
        assertFalse(FleetFilterLogic.matches(FleetFilter.ISSUES, hops = 0, isPinned = false, latestHealthLine = okLine))
        assertFalse(FleetFilterLogic.matches(FleetFilter.ISSUES, hops = 0, isPinned = false, latestHealthLine = null))

        assertTrue(FleetFilterLogic.matches(FleetFilter.PINNED, hops = 2, isPinned = true, latestHealthLine = null))
        assertFalse(FleetFilterLogic.matches(FleetFilter.PINNED, hops = 0, isPinned = false, latestHealthLine = null))
    }

    @Test
    fun sweepReportFormatterProducesShareableSummary() {
        val text = SweepReportFormatter.format(
            listOf(
                SweepReportRow("Vitosha-R1", "OK", "4.02 V · 81%", "All good"),
                SweepReportRow("Plana-R2", "WARN", "3.74 V · 42%", "i2c"),
            ),
            "23.11 11:30",
        )
        assertTrue("MeshCore Fleet Health Report (23.11 11:30)" in text)
        assertTrue("• Vitosha-R1 [OK] · 4.02 V · 81% — All good" in text)
        assertTrue("• Plana-R2 [WARN] · 3.74 V · 42% — i2c" in text)
    }
}
