package io.github.n3tk0.loraota.backup

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import org.json.JSONObject
import java.io.File
import java.security.KeyStore
import java.security.SecureRandom
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.Mac
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec
import javax.crypto.spec.SecretKeySpec

class WrongPassword : Exception("wrong password")

private const val B64_ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"

internal fun b64Encode(data: ByteArray): String {
    val sb = StringBuilder((data.size + 2) / 3 * 4)
    var i = 0
    while (i < data.size) {
        val b0 = data[i++].toInt() and 0xff
        val b1 = if (i < data.size) data[i++].toInt() and 0xff else -1
        val b2 = if (i < data.size) data[i++].toInt() and 0xff else -1
        sb.append(B64_ALPHABET[b0 ushr 2])
        sb.append(B64_ALPHABET[((b0 and 0x03) shl 4) or (if (b1 >= 0) b1 ushr 4 else 0)])
        sb.append(if (b1 >= 0) B64_ALPHABET[((b1 and 0x0f) shl 2) or (if (b2 >= 0) b2 ushr 6 else 0)] else '=')
        sb.append(if (b2 >= 0) B64_ALPHABET[b2 and 0x3f] else '=')
    }
    return sb.toString()
}

internal fun b64Decode(s: String): ByteArray {
    val clean = s.filterNot { it.isWhitespace() }
    require(clean.length % 4 == 0) { "bad base64 length" }
    if (clean.isEmpty()) return ByteArray(0)
    val pad = (if (clean.endsWith("==")) 2 else if (clean.endsWith("=")) 1 else 0)
    val out = ByteArray(clean.length / 4 * 3 - pad)
    var outIdx = 0
    var i = 0
    while (i < clean.length) {
        val c0 = B64_ALPHABET.indexOf(clean[i++])
        val c1 = B64_ALPHABET.indexOf(clean[i++])
        val ch2 = clean[i++]
        val ch3 = clean[i++]
        val c2 = if (ch2 == '=') 0 else B64_ALPHABET.indexOf(ch2)
        val c3 = if (ch3 == '=') 0 else B64_ALPHABET.indexOf(ch3)
        require(c0 >= 0 && c1 >= 0 && c2 >= 0 && c3 >= 0) { "bad base64 char" }
        val triple = (c0 shl 18) or (c1 shl 12) or (c2 shl 6) or c3
        out[outIdx++] = (triple ushr 16).toByte()
        if (ch2 != '=' && outIdx < out.size) out[outIdx++] = (triple ushr 8).toByte()
        if (ch3 != '=' && outIdx < out.size) out[outIdx++] = triple.toByte()
    }
    return out
}

/**
 * A backup in a file that leaves the phone. With a password the whole backup is in it, encrypted (AES-GCM,
 * key from the password with PBKDF2); without one only what is no secret: no private key, no channels
 * (their keys), no passwords.
 */
object PasswordBox {
    private const val ITERATIONS = 120_000
    private const val MAX_ITERATIONS = 2_000_000

    private fun key(password: String, salt: ByteArray, iterations: Int = ITERATIONS) =
        SecretKeySpec(pbkdf2Sha256(password, salt, iterations, 32), "AES")

    /**
     * PBKDF2-HMAC-SHA256 (RFC 8018), password as UTF-8: the same bytes as the JCA "PBKDF2WithHmacSHA256",
     * which Android has only from 8.0 (API 26); HmacSHA256 is on every version, so Android 7 can open and
     * make the same files.
     */
    fun pbkdf2Sha256(password: String, salt: ByteArray, iterations: Int, length: Int): ByteArray {
        // a broken or made-up file: refused (a bad-file error, not 'wrong password'), not hours of work
        require(iterations in 1..MAX_ITERATIONS) { "bad iteration count $iterations" }
        require(length in 1..1024) { "bad key length $length" }
        val mac = Mac.getInstance("HmacSHA256")
        mac.init(SecretKeySpec(password.toByteArray(Charsets.UTF_8).let { if (it.isEmpty()) ByteArray(1) else it }, "HmacSHA256"))
        val out = ByteArray(length)
        val u = ByteArray(32)
        val t = ByteArray(32)
        var block = 1
        var pos = 0
        while (pos < length) {
            mac.update(salt)
            mac.update(byteArrayOf((block ushr 24).toByte(), (block ushr 16).toByte(), (block ushr 8).toByte(), block.toByte()))
            mac.doFinal(u, 0)
            u.copyInto(t)
            repeat(iterations - 1) {
                mac.update(u)
                mac.doFinal(u, 0)
                for (i in 0 until 32) t[i] = (t[i].toInt() xor u[i].toInt()).toByte()
            }
            val n = minOf(32, length - pos)
            t.copyInto(out, pos, 0, n)
            pos += n
            block++
        }
        return out
    }

    fun seal(data: ByteArray, password: String): JSONObject {
        val rnd = SecureRandom()
        val salt = ByteArray(16).also { rnd.nextBytes(it) }
        val iv = ByteArray(12).also { rnd.nextBytes(it) }
        val c = Cipher.getInstance("AES/GCM/NoPadding")
        c.init(Cipher.ENCRYPT_MODE, key(password, salt), GCMParameterSpec(128, iv))
        return JSONObject().put("kdf", "pbkdf2-sha256").put("iter", ITERATIONS)
            .put("salt", b64Encode(salt)).put("iv", b64Encode(iv))
            .put("data", b64Encode(c.doFinal(data)))
    }

    fun open(o: JSONObject, password: String): ByteArray {
        require(o.optString("kdf", "pbkdf2-sha256") == "pbkdf2-sha256") { "unknown kdf ${o.optString("kdf")}" }
        val salt = b64Decode(o.getString("salt"))
        val iv = b64Decode(o.getString("iv"))
        val c = Cipher.getInstance("AES/GCM/NoPadding")
        c.init(Cipher.DECRYPT_MODE, key(password, salt, o.optInt("iter", ITERATIONS)), GCMParameterSpec(128, iv))
        return try {
            c.doFinal(b64Decode(o.getString("data")))
        } catch (e: javax.crypto.AEADBadTagException) {
            throw WrongPassword()
        }
    }

    /** Settings that are secrets: only in a file under a password. */
    val SECRET_SETTINGS = setOf("guest.password", "password")

    fun export(b: Backup, password: String): String {
        if (password.isEmpty()) {
            val open = Backup(b.kind, b.deviceKey, b.deviceName, b.time, b.auto, b.firmware,
                b.items.filter { it.group != BackupItem.GROUP_CHANNEL && it.id !in SECRET_SETTINGS })
            return open.toJson().toString(2)
        }
        val inner = b.toJson()
        b.privateKey?.let { inner.put("key", b64Encode(it)) }
        // outside only what the import needs to say what it is
        return JSONObject().put("format", Backup.FORMAT).put("kind", b.kind.name.lowercase()).put("name", b.deviceName)
            .put("time", b.time).put("enc", seal(inner.toString().toByteArray(), password)).toString(2)
    }

    /** True when the file is encrypted: the import needs its password. */
    fun needsPassword(text: String) = JSONObject(text).has("enc")

    /** A file of [export]; an encrypted one only with its [password] ([WrongPassword] otherwise). */
    fun import(text: String, password: String): Backup {
        val o = JSONObject(text)
        if (!o.has("enc")) return Backup.fromJson(o)
        if (password.isEmpty()) throw WrongPassword()
        val inner = JSONObject(String(open(o.getJSONObject("enc"), password)))
        val k = inner.optString("key").takeIf { it.isNotEmpty() }?.let { b64Decode(it) }
        return Backup.fromJson(inner).with(k)
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
        return b64Encode(c.iv + c.doFinal(data))
    }

    /** Null when the Keystore key is gone (e.g. the lock screen was reset): the backup without its key. */
    private fun open(s: String): ByteArray? = runCatching {
        val bytes = b64Decode(s)
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
        val safeKey = b.deviceKey.filter { it.isLetterOrDigit() || it == '_' || it == '-' }.take(12).ifEmpty { "dev" }
        val name = "${b.kind.name.lowercase()}-$safeKey-${b.time}.json"
        val o = b.toJson()
        b.privateKey?.let { o.put("keyKs", seal(it)) }
        val tmp = File(dir, "$name.tmp")
        tmp.writeText(o.toString())
        if (!tmp.renameTo(File(dir, name))) throw java.io.IOException("could not save the backup")
        trim(b.kind, b.deviceKey, keepFile = name)
        return b.with(b.privateKey, name)
    }

    @Synchronized
    fun delete(b: Backup) {
        if (b.file.isNotEmpty()) File(dir, b.file).delete()
    }

    private fun trim(kind: DeviceKind, device: String, keepFile: String = "") {
        val mine = all().filter { it.kind == kind && it.deviceKey == device }
        if (mine.size <= KEEP) return
        val candidates = mine.filter { it.file != keepFile }
        val autos = candidates.filter { it.auto }.sortedBy { it.time }
        val manual = candidates.filter { !it.auto }.sortedBy { it.time }
        for (b in (autos + manual).take(mine.size - KEEP)) delete(b)
    }

    private companion object {
        const val KEYSTORE = "AndroidKeyStore"
        const val ALIAS = "backup-key"
        const val KEEP = 10
    }
}
