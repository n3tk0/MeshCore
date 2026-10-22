package io.github.n3tk0.loraota.backup

import org.json.JSONObject
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class BackupTest {
    private fun s(id: String, v: String) = BackupItem(BackupItem.GROUP_SETTING, id, id, v)
    private fun contact(key: String, v: String) = BackupItem(BackupItem.GROUP_CONTACT, key, "c$key", v)

    private val key = ByteArray(64) { it.toByte() }
    private val backup = Backup(DeviceKind.COMPANION, "ab".repeat(32), "Petko", 1_760_000_000_000, true, "Faketec · v1.18.0",
        listOf(s("name", "Petko, the 1st"), s("radio", "869618,62500,8,8,1"), s("guest.password", "g"), contact("01", "aa"),
            BackupItem(BackupItem.GROUP_CHANNEL, "0", "Public", "00".repeat(48))), key)

    @Test
    fun jsonRoundTripKeepsEverythingButTheKey() {
        val b = Backup.fromJson(JSONObject(backup.toJson().toString()))
        assertEquals(backup.items, b.items)
        assertEquals(DeviceKind.COMPANION, b.kind)
        assertEquals("Petko", b.deviceName)
        assertTrue(b.auto)
        assertNull(b.privateKey)
        assertFalse(backup.toJson().toString().contains("keyPw"))
    }

    @Test
    fun withAPasswordAllIsEncryptedWithoutOneNoSecrets() {
        val withPw = PasswordBox.export(backup, "secret")
        assertTrue(PasswordBox.needsPassword(withPw))
        assertFalse(withPw.contains("Public"))   // nothing readable inside
        val back = PasswordBox.import(withPw, "secret")
        assertArrayEquals(key, back.privateKey)
        assertEquals(backup.items, back.items)
        for (pw in listOf("wrong", "")) {
            try {
                PasswordBox.import(withPw, pw)
                throw AssertionError("a wrong password must fail")
            } catch (e: WrongPassword) {
            }
        }
        val noPw = PasswordBox.export(backup.with(key), "")
        assertFalse(PasswordBox.needsPassword(noPw))
        val open = PasswordBox.import(noPw, "")
        assertNull(open.privateKey)
        // no channels (their keys) and no passwords
        assertEquals(listOf("setting/name", "setting/radio", "contact/01"), open.items.map { it.ref })
    }

    @Test(expected = IllegalArgumentException::class)
    fun anotherFormatIsRefused() {
        Backup.fromJson(JSONObject(backup.toJson().put("format", 99).toString()))
    }

    @Test
    fun diffShowsChangedAndMissingButNotExtrasOrKnownContacts() {
        val now = listOf(s("name", "Other"), s("radio", "869618,62500,8,8,1"), contact("01", "bb"), contact("02", "cc"),
            s("tx", "22"))
        val rows = BackupDiff.diff(backup.items, now)
        // the name changed, the channel is gone; the contact is there (its frame changes all the time); tx is not in the backup
        assertEquals(listOf("setting/name", "setting/guest.password", "channel/0"), rows.map { it.item.ref })
        assertEquals("Other", rows[0].current)
        assertNull(rows[2].current)
        assertEquals(listOf("contact/01"), BackupDiff.diff(backup.items, emptyList()).filter { it.item.group == BackupItem.GROUP_CONTACT }.map { it.item.ref })
    }
}
