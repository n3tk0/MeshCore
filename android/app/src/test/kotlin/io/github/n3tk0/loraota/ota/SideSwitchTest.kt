package io.github.n3tk0.loraota.ota

import io.github.n3tk0.loraota.screens.MemoryPrefs
import kotlinx.coroutines.runBlocking
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

class SideSwitchTest {
    private val main = SideRadio(869618, 62500, 8, 8)
    private val side = SideRadio.DEFAULT
    private val addr = "AA:BB:CC:DD:EE:FF"

    @Test
    fun savedRadioRoundTripAndRejectsJunk() {
        val s = SavedRadio(main, 1)
        assertEquals(s, SavedRadio.parse(s.format()))
        assertNull(SavedRadio.parse(null))
        assertNull(SavedRadio.parse("869618,62500,8"))
        assertNull(SavedRadio.parse("869618,62500,13,8,0"))   // SF out of range
        assertNull(SavedRadio.parse("x,62500,8,8,0"))
        assertEquals(1, SavedRadio.parse("869618,62500,8,8,7")!!.repeat)
    }

    @Test
    fun stockSwitchSavesFirstAndPutsBack() = runBlocking {
        val store = SavedRadios(MemoryPrefs())
        val sets = mutableListOf<Pair<SideRadio, Int>>()
        var reads = 0
        val sw = StockRadioSwitch(addr, { reads++; SavedRadio(main, 1) }, store) { r, rep -> sets += r to rep }
        sw.enter(side, 2)
        // saved before the switch, client repeat off on the side channel
        assertEquals(SavedRadio(main, 1), store.get(addr))
        assertEquals(listOf(side to 0), sets)
        sw.enter(side, 2)   // a later window: nothing to renew
        assertEquals(1, sets.size)
        sw.leave()
        assertEquals(main to 1, sets.last())
        assertNull(store.get(addr))
        // a second round (after a pause for time on air) reads the normal settings only once
        sw.enter(side, 2)
        sw.leave()
        assertEquals(1, reads)
    }

    @Test
    fun lostConnectionIsRestoredAtNextConnect() = runBlocking {
        val prefs = MemoryPrefs()
        val sw = StockRadioSwitch(addr, { SavedRadio(main, 0) }, SavedRadios(prefs)) { _, _ -> }
        sw.enter(side, 2)
        // leave() fails: the link is gone
        val broken = StockRadioSwitch(addr, { SavedRadio(main, 0) }, SavedRadios(prefs)) { r, _ -> if (r == main) error("gone") }
        broken.enter(side, 2)
        assertFailsWith<IllegalStateException> { broken.leave() }
        // the app restarts and connects again
        val store = SavedRadios(prefs)
        val sets = mutableListOf<SideRadio>()
        assertTrue(StockRadioSwitch.restoreSaved(addr, store) { r, _ -> sets += r })
        assertEquals(listOf(main), sets)
        assertNull(store.get(addr))
        assertFalse(StockRadioSwitch.restoreSaved(addr, store) { r, _ -> sets += r })
        assertFalse(StockRadioSwitch.restoreSaved("11:22:33:44:55:66", store) { r, _ -> sets += r })
    }

    @Test
    fun refusedSwitchIsSideSwitchFailedAndLeaveNeverRunsWithoutEnter() = runBlocking {
        val store = SavedRadios(MemoryPrefs())
        val sets = mutableListOf<SideRadio>()
        val sw = StockRadioSwitch(addr, { SavedRadio(main, 0) }, store) { r, _ -> sets += r; if (r == side) error("companion error 6") }
        assertFailsWith<SideSwitchFailed> { sw.enter(side, 2) }
        sw.leave()   // puts the normal settings back anyway (the reply may have been lost)
        assertEquals(main, sets.last())
        val none = StockRadioSwitch(addr, { error("not read") }, store) { r, _ -> sets += r }
        val before = sets.size
        none.leave()
        assertEquals(before, sets.size)
    }

    @Test
    fun tempRadioUsesTheConsole() = runBlocking {
        val sent = mutableListOf<String>()
        val sw = TempRadioSwitch { sent += it; "OK" }
        sw.enter(side, 3)
        sw.leave()
        assertEquals(listOf("tempradio 869.470,125,7,5,3", "tempradio off"), sent)
        assertFailsWith<SideSwitchFailed> { TempRadioSwitch { "Error" }.enter(side, 3) }
    }
}
