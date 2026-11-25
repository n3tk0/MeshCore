package io.github.n3tk0.loraota.backup

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class BackupCompareTest {
    private fun s(id: String, v: String) = BackupItem(BackupItem.GROUP_SETTING, id, id, v)
    private fun ch(slot: String, name: String, secret: String) =
        BackupItem(BackupItem.GROUP_CHANNEL, slot, name,
            name.toByteArray().joinToString("") { "%02x".format(it) }.padEnd(64, '0') + secret.padEnd(32, '0'))
    private fun ct(key: String, name: String) = BackupItem(BackupItem.GROUP_CONTACT, key, name, "aa")
    private fun backup(t: Long, vararg items: BackupItem) =
        Backup(DeviceKind.REPEATER, "k", "Rep", t, false, "v1", items.toList())

    @Test
    fun settingsAddedRemovedChanged() {
        val a = backup(1, s("tx", "10"), s("af", "1.0"), s("repeat", "on"))
        val b = backup(2, s("tx", "14"), s("af", "1.0"), s("cad", "5"))
        val c = BackupCompare.compare(a, b)
        assertEquals(listOf("cad"), c.settingsAdded.map { it.id })
        assertEquals(listOf("repeat"), c.settingsRemoved.map { it.id })
        assertEquals(1, c.settingsChanged.size)
        assertEquals("10", c.settingsChanged[0].old)
        assertEquals("14", c.settingsChanged[0].new)
        assertFalse(c.isEmpty)
    }

    @Test
    fun secretsAreRedactedEverywhere() {
        val a = backup(1, s("password", "old-secret"), s("guest.password", "g1"), s("prv.key", "AA"), s("pin", "1234"), s("wifi.pwd", "w1"))
        val b = backup(2, s("password", "new-secret"), s("guest.password", "g1"), s("pin", "5678"), s("bridge.secret", "zzz"))
        val c = BackupCompare.compare(a, b)
        val text = (c.settingsAdded.map { it.value } + c.settingsRemoved.map { it.value } +
            c.settingsChanged.flatMap { listOf(it.old, it.new) }).joinToString("|")
        for (leak in listOf("old-secret", "new-secret", "1234", "5678", "zzz", "w1", "AA")) assertFalse(leak, text.contains(leak))
        assertTrue(c.settingsChanged.all { it.secret && it.old == BackupCompare.REDACTED })
        // an equal secret is not a difference
        assertFalse(c.settingsChanged.any { it.id == "guest.password" })
        assertEquals(setOf("password", "pin"), c.settingsChanged.map { it.id }.toSet())
    }

    @Test
    fun channelsAddedRemovedChangedAndTheirSecretStaysHidden() {
        val a = backup(1, ch("0", "Public", "aa"), ch("1", "Old", "bb"), ch("2", "Same", "cc"), ch("3", "Re", "dd"))
        val b = backup(2, ch("0", "Public", "ff"), ch("2", "Same", "cc"), ch("3", "Renamed", "dd"), ch("4", "New", "ee"))
        val c = BackupCompare.compare(a, b)
        assertEquals(listOf("New"), c.channelsAdded.map { it.label })
        assertEquals(listOf("Old"), c.channelsRemoved.map { it.label })
        assertEquals(listOf("#0", "#3"), c.channelsChanged.map { it.label })
        assertTrue(c.channelsChanged[0].secret)        // same name, other secret
        assertFalse(c.channelsChanged[1].secret)       // only renamed
        assertEquals("Re", c.channelsChanged[1].old)
        assertEquals("Renamed", c.channelsChanged[1].new)
        assertTrue(c.channelsAdded.all { it.value == BackupCompare.REDACTED })
    }

    @Test
    fun contactsCountedAndNamed() {
        val a = backup(1, ct("01", "Ann"), ct("02", "Bob"))
        val b = backup(2, ct("02", "Bob"), ct("03", "Cy"), ct("04", "Di"))
        val c = BackupCompare.compare(a, b)
        assertEquals(2, c.contactsOld)
        assertEquals(3, c.contactsNew)
        assertEquals(1, c.contactsDelta)
        assertEquals(listOf("Cy", "Di"), c.contactsAdded)
        assertEquals(listOf("Ann"), c.contactsRemoved)
    }

    @Test
    fun identicalBackupsAreEmptyAndOrderFollowsTime() {
        val a = backup(1, s("tx", "10"), ct("01", "A"))
        val b = backup(2, s("tx", "10"), ct("01", "A"))
        assertTrue(BackupCompare.compare(a, b).isEmpty)
        assertEquals(1L, BackupCompare.inTimeOrder(b, a).first.time)
    }
}
