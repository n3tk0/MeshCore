package io.github.n3tk0.loraota.security

import android.content.Context
import android.content.SharedPreferences
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import java.security.KeyStore
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

/** Outcome of decrypting a stored password: distinguishes permanent key loss from transient Keystore errors. */
sealed class DecryptResult {
    data class Ok(val plain: String) : DecryptResult()
    data object PermanentFailure : DecryptResult()
    data object TransientFailure : DecryptResult()
}

/** Abstraction for encrypting/decrypting password strings so migration logic is JVM-testable. */
interface PasswordCipher {
    fun encrypt(plain: String): String
    fun decrypt(encoded: String): DecryptResult
}

/**
 * Repeater admin passwords encrypted at rest with an Android Keystore AES/GCM key.
 * Transparently migrates any legacy plaintext entries from SharedPreferences("passwords")
 * into SharedPreferences("passwords_enc") on startup without losing existing saved passwords.
 */
class PasswordStore private constructor(
    private val encryptedPrefs: KeyValueStore,
    private val legacyPrefs: KeyValueStore?,
    private val cipher: PasswordCipher,
) {
    constructor(ctx: Context) : this(
        encryptedPrefs = SharedPrefsStore(ctx.getSharedPreferences(ENC_PREFS, Context.MODE_PRIVATE)),
        legacyPrefs = SharedPrefsStore(ctx.getSharedPreferences(LEGACY_PREFS, Context.MODE_PRIVATE)),
        cipher = AndroidKeystoreCipher(ALIAS),
    )

    init {
        migrateLegacy()
    }

    /** Migrates legacy plaintext passwords to encrypted storage; only removes legacy key once encrypted save succeeds. */
    private fun migrateLegacy() {
        val legacy = legacyPrefs ?: return
        val entries = legacy.allStrings()
        if (entries.isEmpty()) return
        for ((k, v) in entries) {
            val saved = runCatching {
                if (encryptedPrefs.getString(k) == null) {
                    // commit() so the encrypted entry is on disk before the legacy one is removed
                    encryptedPrefs.putStringSync(k, cipher.encrypt(v))
                } else true
            }.getOrDefault(false)
            if (saved) legacy.remove(k)
        }
    }

    /** Passwords that could not be encrypted: kept only for this process, never written to disk in plaintext. */
    private val sessionOnly = HashMap<String, String>()

    @Synchronized
    fun keys(): Set<String> {
        val all = LinkedHashSet<String>()
        all.addAll(sessionOnly.keys)
        all.addAll(encryptedPrefs.keys())
        legacyPrefs?.keys()?.let { all.addAll(it) }
        return all
    }

    @Synchronized
    fun get(key: String): String? {
        sessionOnly[key]?.let { return it }
        encryptedPrefs.getString(key)?.let { enc ->
            when (val res = cipher.decrypt(enc)) {
                is DecryptResult.Ok -> return res.plain
                DecryptResult.PermanentFailure -> encryptedPrefs.remove(key)
                DecryptResult.TransientFailure -> {}
            }
        }
        // Fallback if an entry could not be migrated yet
        return legacyPrefs?.getString(key)
    }

    /**
     * Stores [password] encrypted. Returns false when Keystore encryption failed: the password is then
     * kept in memory only (until the app closes) and nothing is written to disk in plaintext.
     */
    @Synchronized
    fun set(key: String, password: String): Boolean {
        val ok = runCatching {
            encryptedPrefs.putString(key, cipher.encrypt(password))
            legacyPrefs?.remove(key)
            true
        }.getOrDefault(false)
        if (ok) {
            sessionOnly.remove(key)
        } else {
            // drop any stale encrypted/legacy entry so the old password is not returned instead of the new one
            encryptedPrefs.remove(key)
            legacyPrefs?.remove(key)
            sessionOnly[key] = password
        }
        return ok
    }

    @Synchronized
    fun remove(key: String) {
        sessionOnly.remove(key)
        encryptedPrefs.remove(key)
        legacyPrefs?.remove(key)
    }

    /** Minimal key-value store abstraction for unit testing without Android framework stubs. */
    interface KeyValueStore {
        fun keys(): Set<String>
        fun allStrings(): Map<String, String>
        fun getString(key: String): String?
        fun putString(key: String, value: String)
        /** Writes synchronously; false when it could not be stored. */
        fun putStringSync(key: String, value: String): Boolean { putString(key, value); return true }
        fun remove(key: String)
    }

    private class SharedPrefsStore(private val prefs: SharedPreferences) : KeyValueStore {
        override fun keys(): Set<String> = prefs.all.keys
        override fun allStrings(): Map<String, String> =
            prefs.all.mapNotNull { (k, v) -> (v as? String)?.let { k to it } }.toMap()
        override fun getString(key: String): String? = prefs.getString(key, null)
        override fun putString(key: String, value: String) { prefs.edit().putString(key, value).apply() }
        override fun putStringSync(key: String, value: String): Boolean = prefs.edit().putString(key, value).commit()
        override fun remove(key: String) { prefs.edit().remove(key).apply() }
    }

    private class AndroidKeystoreCipher(private val alias: String) : PasswordCipher {
        @Synchronized
        private fun key(): SecretKey {
            val ks = KeyStore.getInstance(KEYSTORE).apply { load(null) }
            (ks.getKey(alias, null) as? SecretKey)?.let { return it }
            val gen = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, KEYSTORE)
            gen.init(
                KeyGenParameterSpec.Builder(alias, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
                    .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                    .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                    .build()
            )
            return gen.generateKey()
        }

        override fun encrypt(plain: String): String {
            val c = Cipher.getInstance(TRANSFORM)
            c.init(Cipher.ENCRYPT_MODE, key())
            val iv = c.iv
            check(iv.size == IV)
            return Base64.encodeToString(iv + c.doFinal(plain.toByteArray(Charsets.UTF_8)), Base64.NO_WRAP)
        }

        override fun decrypt(encoded: String): DecryptResult = try {
            val bytes = Base64.decode(encoded, Base64.NO_WRAP)
            val c = Cipher.getInstance(TRANSFORM)
            c.init(Cipher.DECRYPT_MODE, key(), GCMParameterSpec(128, bytes, 0, IV))
            DecryptResult.Ok(String(c.doFinal(bytes, IV, bytes.size - IV), Charsets.UTF_8))
        } catch (it: Throwable) {
            val permanent =
                it is android.security.keystore.KeyPermanentlyInvalidatedException ||
                it is javax.crypto.AEADBadTagException ||
                it is java.security.UnrecoverableKeyException ||
                it is IllegalArgumentException
            if (permanent) DecryptResult.PermanentFailure else DecryptResult.TransientFailure
        }
    }

    companion object {
        private const val KEYSTORE = "AndroidKeyStore"
        private const val ALIAS = "repeater-passwords"
        private const val ENC_PREFS = "passwords_enc"
        private const val LEGACY_PREFS = "passwords"
        private const val TRANSFORM = "AES/GCM/NoPadding"
        private const val IV = 12

        internal fun createForTest(
            encryptedPrefs: KeyValueStore,
            legacyPrefs: KeyValueStore?,
            cipher: PasswordCipher,
        ) = PasswordStore(encryptedPrefs, legacyPrefs, cipher)
    }
}
