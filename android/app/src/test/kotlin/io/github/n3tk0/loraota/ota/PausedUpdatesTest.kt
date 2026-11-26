package io.github.n3tk0.loraota.ota

import io.github.n3tk0.loraota.screens.MemoryPrefs
import kotlin.test.Test
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class PausedUpdatesTest {
    @Test
    fun markedPerRepeaterUntilCleared() {
        val prefs = MemoryPrefs()
        val p = PausedUpdates(prefs)
        assertFalse(p.has("aa"))
        p.mark("aa")
        p.mark("bb")
        assertTrue(p.has("aa"))
        // another instance on the same preferences (the app restarted) still knows
        assertTrue(PausedUpdates(prefs).has("bb"))
        p.clear("aa")
        assertFalse(p.has("aa"))
        assertTrue(p.has("bb"))
        p.mark("")
        assertFalse(p.has(""))
    }
}
