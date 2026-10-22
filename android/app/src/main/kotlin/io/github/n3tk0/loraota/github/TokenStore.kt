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

    /** The user entered one (it can be forgotten; the group's cannot). */
    val hasOwn get() = prefs.contains(KEY)

    val has get() = hasOwn || group != null

    /** Only the group's token, built into the group APK (see build.gradle.kts). */
    val usesGroup get() = !hasOwn && group != null

    /** The user's own, else the group's; null: none (or the key is gone, e.g. the lock screen was reset). */
    fun get(): String? = own() ?: group

    private fun own(): String? {
        val stored = prefs.getString(KEY, null) ?: return null
        return runCatching {
            val bytes = Base64.decode(stored, Base64.NO_WRAP)
            val c = Cipher.getInstance(TRANSFORM)
            c.init(Cipher.DECRYPT_MODE, key(), GCMParameterSpec(128, bytes, 0, IV))
            String(c.doFinal(bytes, IV, bytes.size - IV))
        }.getOrNull()
    }

    fun set(token: String) {
        val c = Cipher.getInstance(TRANSFORM)
        c.init(Cipher.ENCRYPT_MODE, key())
        val iv = c.iv
        check(iv.size == IV)
        prefs.edit().putString(KEY, Base64.encodeToString(iv + c.doFinal(token.toByteArray()), Base64.NO_WRAP)).apply()
    }

    fun clear() {
        prefs.edit().remove(KEY).apply()
    }

    private val group: String? by lazy {
        val a = unhex(io.github.n3tk0.loraota.BuildConfig.GROUP_TOKEN_A)
        val b = unhex(io.github.n3tk0.loraota.BuildConfig.GROUP_TOKEN_B)
        if (a.isEmpty() || a.size != b.size) null else String(ByteArray(a.size) { (a[it].toInt() xor b[it].toInt()).toByte() })
    }

    private fun unhex(s: String) = ByteArray(s.length / 2) { s.substring(it * 2, it * 2 + 2).toInt(16).toByte() }

    private companion object {
        const val KEYSTORE = "AndroidKeyStore"
        const val ALIAS = "github-token"
        const val KEY = "token"
        const val TRANSFORM = "AES/GCM/NoPadding"
        const val IV = 12
    }
}
