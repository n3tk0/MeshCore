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

    @Test
    fun pbkdf2MatchesJcaAndTheKnownVector() {
        // RFC 7914 section 11: P="passwd", S="salt", c=1, dkLen=64
        val v = PasswordBox.pbkdf2Sha256("passwd", "salt".toByteArray(), 1, 64)
        assertEquals("55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc" +
            "49ca9cccf179b645991664b39d77ef317c71b845b1e30bd509112041d3a19783",
            v.joinToString("") { "%02x".format(it) })
        // the same bytes as the JCA one (Android 8+, desktop Java), so old backups still open
        val salt = ByteArray(16) { (it * 7).toByte() }
        for (pw in listOf("secret", "парола", "a")) {
            val jca = javax.crypto.SecretKeyFactory.getInstance("PBKDF2WithHmacSHA256")
                .generateSecret(javax.crypto.spec.PBEKeySpec(pw.toCharArray(), salt, 1000, 256)).encoded
            assertArrayEquals(jca, PasswordBox.pbkdf2Sha256(pw, salt, 1000, 32))
        }
    }

    @Test
    fun aBadIterationCountIsABadFileNotAWrongPassword() {
        val withPw = PasswordBox.export(backup, "secret")
        for (iter in listOf(0, -5, 2_000_000_000)) {
            val o = JSONObject(withPw)
            o.getJSONObject("enc").put("iter", iter)
            try {
                PasswordBox.import(o.toString(), "secret")
                throw AssertionError("iter $iter accepted")
            } catch (e: IllegalArgumentException) {
                // the import screen shows 'not a backup file' for this, not 'wrong password'
            }
        }
    }

    @Test
    fun anUnknownKdfIsABadFileAndTheCapIs2M() {
        val o = JSONObject(PasswordBox.export(backup, "secret"))
        o.getJSONObject("enc").put("kdf", "scrypt")
        try {
            PasswordBox.import(o.toString(), "secret")
            throw AssertionError("scrypt accepted")
        } catch (e: IllegalArgumentException) {
        }
        val o2 = JSONObject(PasswordBox.export(backup, "secret"))
        o2.getJSONObject("enc").put("iter", 2_000_001)
        try {
            PasswordBox.import(o2.toString(), "secret")
            throw AssertionError("iter above the cap accepted")
        } catch (e: IllegalArgumentException) {
        }
    }
}
