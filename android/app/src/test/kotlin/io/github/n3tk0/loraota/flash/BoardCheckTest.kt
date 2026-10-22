package io.github.n3tk0.loraota.flash

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

class BoardCheckTest {
    @Test
    fun dfuNamesMatchTheirBuilds() {
        assertEquals(true, BoardCheck.matches("Xiao_nrf52_repeater", "XIAO_NRF52_OTA"))
        assertEquals(true, BoardCheck.matches("RAK_4631_repeater", "RAK4631_OTA"))
        assertEquals(true, BoardCheck.matches("ProMicro_repeater", "ProMicro_OTA"))
        assertEquals(true, BoardCheck.matches("RAK_WisMesh_Tag_repeater", "WISMESHTAG_OTA"))
        assertEquals(false, BoardCheck.matches("RAK_4631_repeater", "XIAO_NRF52_OTA"))
    }

    @Test
    fun manufacturerNamesMatchTheirBuilds() {
        assertEquals(true, BoardCheck.matches("Faketec_v5_companion_radio_ble", "Faketec DIY"))
        assertEquals(true, BoardCheck.matches("RAK_WisMesh_Tag_companion_radio_ble", "RAK WisMesh Tag"))
        assertEquals(false, BoardCheck.matches("Faketec_v5_companion_radio_ble", "RAK WisMesh Tag"))
        assertEquals(false, BoardCheck.matches("RAK_4631_companion_radio_ble", "RAK WisMesh Tag"))
        assertEquals(true, BoardCheck.matches("Xiao_nrf52_repeater", "Seeed Xiao-nrf52"))
        assertEquals(false, BoardCheck.matches("ThinkNode_M6_repeater", "ThinkNode M1"))
        assertEquals(true, BoardCheck.matches("ThinkNode_M1_repeater", "THINKNODE_M1_OTA"))
        assertEquals(true, BoardCheck.matches("ThinkNode_M1_repeater", "Elecrow ThinkNode-M1"))
        // the chip is checked on its own (file type, platform): here only the board counts
        assertEquals(true, BoardCheck.matches("Xiao_nrf52_repeater", "XIAO_NRF52840_OTA"))
        assertEquals(false, BoardCheck.matches("Heltec_t114_repeater", "Heltec T1"))
        assertEquals(true, BoardCheck.matches("Heltec_t114_repeater", "T114_OTA"))
        assertEquals(true, BoardCheck.matches("waveshare_rp2040_lora_repeater", "Waveshare RP2040-LoRa"))
        assertNull(BoardCheck.matches("Faketec_v5_companion_radio_ble", "AdaDFU"))
        assertNull(BoardCheck.matches("", "Faketec DIY"))
    }

    @Test
    fun roles() {
        assertEquals(Role.REPEATER, BoardCheck.role("Xiao_C3_repeater"))
        assertEquals(Role.ROOM_SERVER, BoardCheck.role("RAK_4631_room_server"))
        assertTrue(BoardCheck.fitsCompanion("Faketec_v5_companion_radio_ble"))
        assertFalse(BoardCheck.fitsCompanion("Faketec_v5_companion_radio_usb"))
        assertFalse(BoardCheck.fitsCompanion("Xiao_nrf52_repeater"))
    }
}
