package io.github.n3tk0.loraota.backup

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

class SettingsCacheTest {
    private fun s(id: String, v: String) = BackupItem(BackupItem.GROUP_SETTING, id, id, v)

    private var now = 1_000_000L
    private val cache = SettingsCache { now }
    private val base = listOf(s("name", "R1"), s("tx", "14"), s("radio", "869.525,250,11,5"))

    @Test
    fun freshForThirtyMinutes() {
        cache.put("aa", base, withOta = false)
        assertNotNull(cache.fresh("aa"))
        now += 29 * 60_000L + 59_000
        val e = assertNotNull(cache.fresh("aa"))
        assertEquals(29, cache.ageMinutes(e))
        now += 1_000
        assertNull(cache.fresh("aa"))
        assertNull(cache.fresh("bb"))
    }

    @Test
    fun aClockThatWentBackIsNotTrusted() {
        cache.put("aa", base, withOta = false)
        now -= 5 * 60_000L
        assertNull(cache.fresh("aa"))
    }

    @Test
    fun thePasswordAndOtherGroupsAreNeverKept() {
        cache.put(
            "aa",
            base + s(RepeaterBackup.PASSWORD, "secret") + BackupItem(BackupItem.GROUP_CHANNEL, "0", "ch", "ff"),
            withOta = false,
        )
        val e = assertNotNull(cache.fresh("aa"))
        assertEquals(base.map { it.id }, e.items.map { it.id })
        cache.update("aa", mapOf(RepeaterBackup.PASSWORD to "new"))
        assertFalse(assertNotNull(cache.fresh("aa")).values.containsKey(RepeaterBackup.PASSWORD))
    }

    @Test
    fun writtenKeysReplaceAndAddAndKeepTheReadTime() {
        cache.put("aa", base, withOta = true)
        now += 10 * 60_000L
        cache.update("aa", mapOf("tx" to "10", "af" to "1.0"))
        val e = assertNotNull(cache.fresh("aa"))
        assertEquals("10", e.values["tx"])
        assertEquals("1.0", e.values["af"])
        assertEquals("R1", e.values["name"])
        assertTrue(e.withOta)
        // the rest is as old as it was: the write does not make the whole read younger
        assertEquals(10, cache.ageMinutes(e))
        now += 20 * 60_000L + 1
        assertNull(cache.fresh("aa"))
    }

    @Test
    fun updateOfARepeaterNeverReadDoesNothing() {
        cache.update("zz", mapOf("tx" to "10"))
        assertFalse(cache.has("zz"))
    }

    @Test
    fun invalidateForgetsOnlyThatRepeater() {
        cache.put("aa", base, withOta = false)
        cache.put("bb", base, withOta = false)
        cache.invalidate("aa")
        assertNull(cache.fresh("aa"))
        assertNotNull(cache.fresh("bb"))
        cache.clear()
        assertNull(cache.fresh("bb"))
    }

    @Test
    fun aNewReadReplacesTheOldOneAndItsAge() {
        cache.put("aa", base, withOta = false)
        now += 20 * 60_000L
        cache.put("aa", listOf(s("name", "R2")), withOta = true)
        val e = assertNotNull(cache.fresh("aa"))
        assertEquals(0, cache.ageMinutes(e))
        assertEquals(mapOf("name" to "R2"), e.values)
    }

    @Test
    fun noKeyNoEntry() {
        cache.put("", base, withOta = false)
        assertNull(cache.fresh(""))
    }

    @Test
    fun remembersWhetherTheFilterWasRead() {
        cache.put("aa", base, withOta = false, withFilter = true)
        cache.put("bb", base, withOta = false, withFilter = false)
        assertTrue(assertNotNull(cache.fresh("aa")).withFilter)
        assertFalse(assertNotNull(cache.fresh("bb")).withFilter)
        cache.update("aa", mapOf("tx" to "10"))
        assertTrue(assertNotNull(cache.fresh("aa")).withFilter)
    }

    @Test
    fun keepsSixteenRepeaters() {
        for (i in 1..17) cache.put("k$i", base, withOta = false)
        assertFalse(cache.has("k1"))
        for (i in 2..17) assertTrue(cache.has("k$i"))
    }
}
