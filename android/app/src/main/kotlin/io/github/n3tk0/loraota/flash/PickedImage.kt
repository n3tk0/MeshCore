package io.github.n3tk0.loraota.flash

import io.github.n3tk0.loraota.delta.DetoolsPatch
import io.github.n3tk0.loraota.ota.hex
import org.json.JSONObject
import java.io.ByteArrayInputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.zip.ZipInputStream

/**
 * A firmware file the user picked made into the app image (.bin) the repeater runs: a .uf2 (by its magic, not
 * its name) is read with [Uf2.image], a DFU .zip gives its application .bin, anything else is taken as it is.
 */
object PickedImage {
    fun isUf2(d: ByteArray) = d.size >= 8 && d.size % 512 == 0 &&
        ByteBuffer.wrap(d).order(ByteOrder.LITTLE_ENDIAN).let { it.getInt(0) == 0x0A324655 && it.getInt(4) == 0x9E5D5157.toInt() }

    fun isZip(d: ByteArray) = d.size >= 4 && d[0] == 'P'.code.toByte() && d[1] == 'K'.code.toByte() && d[2].toInt() == 3 && d[3].toInt() == 4

    /** The application image in [data]; throws [IllegalArgumentException] for a .uf2 / .zip that has none. */
    fun app(data: ByteArray): ByteArray = when {
        isUf2(data) -> Uf2.image(data)
        isZip(data) -> fromZip(data)
        else -> data
    }

    private fun fromZip(data: ByteArray): ByteArray {
        val entries = LinkedHashMap<String, ByteArray>()
        ZipInputStream(ByteArrayInputStream(data)).use { z ->
            while (true) {
                val e = z.nextEntry ?: break
                val normalized = e.name.replace('\\', '/')
                val name = normalized.substringAfterLast('/')
                if (!e.isDirectory && !name.startsWith("._") && "__MACOSX/" !in normalized) {
                    entries[name] = z.readBytes()
                }
            }
        }
        val manifest = entries["manifest.json"]
        if (manifest != null) {
            // a DFU package: only the application image (a bootloader / softdevice .zip is not one)
            val named = runCatching { JSONObject(String(manifest)).getJSONObject("manifest").getJSONObject("application").getString("bin_file") }
                .getOrNull() ?: throw IllegalArgumentException("the .zip has no application image (manifest.json lists no application bin_file)")
            return entries[named.replace('\\', '/').substringAfterLast('/')]
                ?: throw IllegalArgumentException("the .zip lacks the application image $named")
        }
        val bins = entries.entries.filter { it.key.endsWith(".bin", ignoreCase = true) && !it.key.endsWith("-merged.bin", ignoreCase = true) }
        // without a manifest there is nothing to say which one is the app: not a guess
        if (bins.size > 1) throw IllegalArgumentException("the .zip has no manifest.json and ${bins.size} .bin files (${bins.joinToString { it.key }}): cannot tell which is the application")
        return bins.firstOrNull()?.value ?: throw IllegalArgumentException("no application .bin in the .zip")
    }

    /**
     * The running image from a picked file: its first [size] bytes (a .uf2's image is padded) when their SHA-256
     * is [sha] (hex); null when the file is not it. With [size] 0 (unknown) the whole app image must match.
     */
    fun running(data: ByteArray, size: Int, sha: String): ByteArray? {
        return runningOf(runCatching { app(data) }.getOrNull() ?: return null, size, sha)
    }

    /** [running] for an app image already taken from the file by [app]. */
    fun runningOf(app: ByteArray, size: Int, sha: String): ByteArray? {
        val image = if (size in 1..app.size) app.copyOf(size) else app
        return image.takeIf { hex(DetoolsPatch.sha256(it)) == sha }
    }
}
