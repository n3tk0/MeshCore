package io.github.n3tk0.loraota.backup

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import org.json.JSONObject
import java.io.File
import java.security.KeyStore
import java.security.SecureRandom
import java.util.Base64
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.SecretKeyFactory
import javax.crypto.spec.GCMParameterSpec
import javax.crypto.spec.PBEKeySpec
import javax.crypto.spec.SecretKeySpec

class WrongPassword : Exception("wrong password")

/** A private key in a file that leaves the phone: AES-GCM with a key from the user's password (PBKDF2). */
object PasswordBox {
    private const val ITERATIONS = 120_000
    private val b64 = Base64.getEncoder()
    private val unb64 = Base64.getDecoder()

    private fun key(password: String, salt: ByteArray): SecretKeySpec {
        val f = SecretKeyFactory.getInstance("PBKDF2WithHmacSHA256")
        val k = f.generateSecret(PBEKeySpec(password.toCharArray(), salt, ITERATIONS, 256)).encoded
        return SecretKeySpec(k, "AES")
    }

    fun seal(data: ByteArray, password: String): JSONObject {
        val rnd = SecureRandom()
        val salt = ByteArray(16).also { rnd.nextBytes(it) }
        val iv = ByteArray(12).also { rnd.nextBytes(it) }
        val c = Cipher.getInstance("AES/GCM/NoPadding")
        c.init(Cipher.ENCRYPT_MODE, key(password, salt), GCMParameterSpec(128, iv))
        return JSONObject().put("kdf", "pbkdf2-sha256").put("iter", ITERATIONS)
            .put("salt", b64.encodeToString(salt)).put("iv", b64.encodeToString(iv))
            .put("data", b64.encodeToString(c.doFinal(data)))
    }

    fun open(o: JSONObject, password: String): ByteArray {
        val salt = unb64.decode(o.getString("salt"))
        val iv = unb64.decode(o.getString("iv"))
        val f = SecretKeyFactory.getInstance("PBKDF2WithHmacSHA256")
        val k = f.generateSecret(PBEKeySpec(password.toCharArray(), salt, o.optInt("iter", ITERATIONS), 256)).encoded
        val c = Cipher.getInstance("AES/GCM/NoPadding")
        c.init(Cipher.DECRYPT_MODE, SecretKeySpec(k, "AES"), GCMParameterSpec(128, iv))
        return try {
            c.doFinal(unb64.decode(o.getString("data")))
        } catch (e: javax.crypto.AEADBadTagException) {
            throw WrongPassword()
        }
    }

    /** The file to share: the backup, and its private key only under a password (none: left out). */
    fun export(b: Backup, password: String): String {
        val o = b.toJson()
        val k = b.privateKey
        if (k != null && password.isNotEmpty()) o.put("keyPw", seal(k, password))
        return o.toString(2)
    }

    /** True when the file has a private key, so the import asks for its password. */
    fun hasKey(text: String) = JSONObject(text).has("keyPw")

    /** A file of [export]; [password] empty: without its private key. */
    fun import(text: String, password: String): Backup {
        val o = JSONObject(text)
        val b = Backup.fromJson(o)
        val k = if (o.has("keyPw") && password.isNotEmpty()) open(o.getJSONObject("keyPw"), password) else null
        return b.with(k)
    }
}

/**
 * The backups on this phone, one JSON file each in filesDir/backups. A companion's private key is in it
 * encrypted with a key of the Android Keystore (that key never leaves the phone). At most [KEEP] per device:
 * the oldest automatic one goes first.
 */
class BackupStore(ctx: Context) {
    private val dir = File(ctx.filesDir, "backups").apply { mkdirs() }

    private fun keystoreKey(): SecretKey {
        val ks = KeyStore.getInstance(KEYSTORE).apply { load(null) }
        (ks.getKey(ALIAS, null) as? SecretKey)?.let { return it }
        val gen = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, KEYSTORE)
        gen.init(KeyGenParameterSpec.Builder(ALIAS, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
            .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
            .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
            .build())
        return gen.generateKey()
    }

    private fun seal(data: ByteArray): String {
        val c = Cipher.getInstance("AES/GCM/NoPadding")
        c.init(Cipher.ENCRYPT_MODE, keystoreKey())
        return Base64.getEncoder().encodeToString(c.iv + c.doFinal(data))
    }

    /** Null when the Keystore key is gone (e.g. the lock screen was reset): the backup without its key. */
    private fun open(s: String): ByteArray? = runCatching {
        val bytes = Base64.getDecoder().decode(s)
        val c = Cipher.getInstance("AES/GCM/NoPadding")
        c.init(Cipher.DECRYPT_MODE, keystoreKey(), GCMParameterSpec(128, bytes, 0, 12))
        c.doFinal(bytes, 12, bytes.size - 12)
    }.getOrNull()

    @Synchronized
    fun all(): List<Backup> = (dir.listFiles { f -> f.name.endsWith(".json") } ?: emptyArray()).mapNotNull { f ->
        runCatching {
            val o = JSONObject(f.readText())
            Backup.fromJson(o, f.name).with(o.optString("keyKs").takeIf { it.isNotEmpty() }?.let { open(it) })
        }.getOrNull()
    }.sortedByDescending { it.time }

    @Synchronized
    fun save(b: Backup): Backup {
        val name = "${b.kind.name.lowercase()}-${b.deviceKey.take(12)}-${b.time}.json"
        val o = b.toJson()
        b.privateKey?.let { o.put("keyKs", seal(it)) }
        val tmp = File(dir, "$name.tmp")
        tmp.writeText(o.toString())
        if (!tmp.renameTo(File(dir, name))) throw java.io.IOException("could not save the backup")
        trim(b.kind, b.deviceKey)
        return b.with(b.privateKey, name)
    }

    @Synchronized
    fun delete(b: Backup) {
        if (b.file.isNotEmpty()) File(dir, b.file).delete()
    }

    private fun trim(kind: DeviceKind, device: String) {
        val mine = all().filter { it.kind == kind && it.deviceKey == device }
        if (mine.size <= KEEP) return
        val autos = mine.filter { it.auto }.sortedBy { it.time }
        val manual = mine.filter { !it.auto }.sortedBy { it.time }
        for (b in (autos + manual).take(mine.size - KEEP)) delete(b)
    }

    private companion object {
        const val KEYSTORE = "AndroidKeyStore"
        const val ALIAS = "backup-key"
        const val KEEP = 10
    }
}
