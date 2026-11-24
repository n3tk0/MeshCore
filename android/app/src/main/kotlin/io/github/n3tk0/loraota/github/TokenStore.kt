package io.github.n3tk0.loraota.github

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import java.security.KeyStore
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

/**
 * The user's GitHub token, only on this phone and encrypted with a key of the Android Keystore
 * (the key never leaves it; the app has no backup, so the token does not either).
 */
class TokenStore(ctx: Context) {
    private val prefs = ctx.getSharedPreferences("github", Context.MODE_PRIVATE)

    @Synchronized
    private fun key(): SecretKey {
        val ks = KeyStore.getInstance(KEYSTORE).apply { load(null) }
        (ks.getKey(ALIAS, null) as? SecretKey)?.let { return it }
        val gen = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, KEYSTORE)
        gen.init(KeyGenParameterSpec.Builder(ALIAS, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
            .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
            .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
            .build())
        return gen.generateKey()
    }

    private val builtInGroup: String? by lazy {
        val a = unhex(io.github.n3tk0.loraota.BuildConfig.GROUP_TOKEN_A)
        val b = unhex(io.github.n3tk0.loraota.BuildConfig.GROUP_TOKEN_B)
        if (a.isEmpty() || a.size != b.size) null else String(ByteArray(a.size) { (a[it].toInt() xor b[it].toInt()).toByte() })
    }

    init {
        // Persist the built-in group token encrypted in Keystore so updating from a Group APK
        // to a Public APK (same signing key) preserves the group token automatically,
        // unless the user explicitly opted out.
        if (builtInGroup != null && !prefs.getBoolean(KEY_GROUP_OPT_OUT, false)) {
            runCatching { saveEncrypted(KEY_GROUP, builtInGroup!!) }
        }
    }

    /** The user entered one (it can be forgotten; the built-in group's cannot). */
    val hasOwn get() = prefs.contains(KEY)

    /** True when this APK binary itself contains the built-in group token. */
    val hasBuiltInGroup get() = builtInGroup != null

    /** True when a group token was saved from an earlier Group APK install. */
    val hasSavedGroup get() = prefs.contains(KEY_GROUP)

    /** True when either a built-in or preserved group token is available. */
    val hasGroup get() = group != null

    val has get() = hasOwn || group != null

    /** Only the group's token (built-in or preserved across update). */
    val usesGroup get() = !hasOwn && group != null

    /** The user's own, else the group's; null: none (or the key is gone, e.g. the lock screen was reset). */
    fun get(): String? = own() ?: group

    /** Ensures the built-in group token is flushed encrypted to disk before installing an update. */
    fun persistGroupToken(): Boolean {
        prefs.edit().remove(KEY_GROUP_OPT_OUT).commit()
        val g = builtInGroup ?: return savedGroup() != null
        return runCatching { saveEncrypted(KEY_GROUP, g, sync = true); true }.getOrDefault(false)
    }

    /** Removes a preserved group token and synchronously records opt-out before APK install. */
    fun clearSavedGroup() {
        prefs.edit().remove(KEY_GROUP).putBoolean(KEY_GROUP_OPT_OUT, true).commit()
    }

    private fun own(): String? = readEncrypted(KEY)

    private fun savedGroup(): String? = readEncrypted(KEY_GROUP)

    private val group: String? get() = builtInGroup ?: savedGroup()

    private fun readEncrypted(prefKey: String): String? {
        val stored = prefs.getString(prefKey, null) ?: return null
        return runCatching {
            val bytes = Base64.decode(stored, Base64.NO_WRAP)
            val c = Cipher.getInstance(TRANSFORM)
            c.init(Cipher.DECRYPT_MODE, key(), GCMParameterSpec(128, bytes, 0, IV))
            String(c.doFinal(bytes, IV, bytes.size - IV))
        }.onFailure {
            // only a key that is surely gone or another one (lock screen reset, app data restored):
            // a passing Keystore error must not take the user's token
            if (it is android.security.keystore.KeyPermanentlyInvalidatedException ||
                it is javax.crypto.AEADBadTagException || it is java.security.UnrecoverableKeyException ||
                it is IllegalArgumentException) {
                prefs.edit().remove(prefKey).apply()
            }
        }.getOrNull()
    }

    private fun saveEncrypted(prefKey: String, token: String, sync: Boolean = false) {
        val c = Cipher.getInstance(TRANSFORM)
        c.init(Cipher.ENCRYPT_MODE, key())
        val iv = c.iv
        check(iv.size == IV)
        val editor = prefs.edit().putString(prefKey, Base64.encodeToString(iv + c.doFinal(token.toByteArray()), Base64.NO_WRAP))
        if (sync) editor.commit() else editor.apply()
    }

    fun set(token: String) {
        saveEncrypted(KEY, token)
    }

    fun clear() {
        prefs.edit().remove(KEY).apply()
    }

    private fun unhex(s: String) = ByteArray(s.length / 2) { s.substring(it * 2, it * 2 + 2).toInt(16).toByte() }

    private companion object {
        const val KEYSTORE = "AndroidKeyStore"
        const val ALIAS = "github-token"
        const val KEY = "token"
        const val KEY_GROUP = "group_token"
        const val KEY_GROUP_OPT_OUT = "group_token_opt_out"
        const val TRANSFORM = "AES/GCM/NoPadding"
        const val IV = 12
    }
}
