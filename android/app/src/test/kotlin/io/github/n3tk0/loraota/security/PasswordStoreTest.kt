package io.github.n3tk0.loraota.security

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

class PasswordStoreTest {

    private class MemoryStore : PasswordStore.KeyValueStore {
        val map = LinkedHashMap<String, String>()
        override fun keys(): Set<String> = map.keys.toSet()
        override fun allStrings(): Map<String, String> = map.toMap()
        override fun getString(key: String): String? = map[key]
        override fun putString(key: String, value: String) { map[key] = value }
        override fun remove(key: String) { map.remove(key) }
    }

    private class FakeCipher(var failEncrypt: Boolean = false) : PasswordCipher {
        override fun encrypt(plain: String): String {
            if (failEncrypt) throw RuntimeException("keystore unavailable")
            return "ENC:$plain"
        }
        override fun decrypt(encoded: String): DecryptResult =
            if (encoded.startsWith("ENC:")) DecryptResult.Ok(encoded.removePrefix("ENC:"))
            else DecryptResult.PermanentFailure
    }

    @Test
    fun migratesLegacyPlaintextPasswordsIntoEncryptedStore() {
        val enc = MemoryStore()
        val legacy = MemoryStore().apply {
            putString("node1", "admin123")
            putString("node2", "secret456")
        }
        val store = PasswordStore.createForTest(enc, legacy, FakeCipher())

        assertEquals("admin123", store.get("node1"))
        assertEquals("secret456", store.get("node2"))
        assertEquals(setOf("node1", "node2"), store.keys())
        // Legacy plaintext must be cleared once encrypted
        assertTrue(legacy.map.isEmpty())
        assertEquals("ENC:admin123", enc.getString("node1"))
        assertEquals("ENC:secret456", enc.getString("node2"))
    }

    @Test
    fun keepsLegacyPlaintextIfKeystoreFailsDuringMigration() {
        val enc = MemoryStore()
        val legacy = MemoryStore().apply { putString("node1", "keepme") }
        val cipher = FakeCipher(failEncrypt = true)
        val store = PasswordStore.createForTest(enc, legacy, cipher)

        // Password must still be readable from legacy store and not lost
        assertEquals("keepme", store.get("node1"))
        assertEquals("keepme", legacy.getString("node1"))
        assertTrue(enc.map.isEmpty())
    }

    @Test
    fun setFallbackKeepsPasswordInMemoryOnlyAndNeverWritesPlaintext() {
        val enc = MemoryStore()
        val legacy = MemoryStore()
        val cipher = FakeCipher(failEncrypt = false)
        val store = PasswordStore.createForTest(enc, legacy, cipher)

        assertTrue(store.set("r1", "oldpw"))
        assertEquals("oldpw", store.get("r1"))

        // Keystore failure while updating: old ciphertext dropped, new password only in memory
        cipher.failEncrypt = true
        assertFalse(store.set("r1", "newpw"))
        assertEquals("newpw", store.get("r1"))
        assertNull(enc.getString("r1"))
        assertTrue(legacy.map.isEmpty())
        assertTrue("r1" in store.keys())

        // Not persisted: a restarted store does not know it
        val restarted = PasswordStore.createForTest(enc, legacy, cipher)
        assertNull(restarted.get("r1"))
    }

    @Test
    fun setAndRemoveWorkAcrossBothStores() {
        val enc = MemoryStore()
        val legacy = MemoryStore()
        val store = PasswordStore.createForTest(enc, legacy, FakeCipher())

        store.set("r1", "pw1")
        assertEquals("pw1", store.get("r1"))
        assertEquals("ENC:pw1", enc.getString("r1"))
        assertFalse(legacy.map.containsKey("r1"))

        store.remove("r1")
        assertNull(store.get("r1"))
        assertTrue(store.keys().isEmpty())
    }
}
