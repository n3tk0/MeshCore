package io.github.n3tk0.loraota.ota

import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.net.HttpURLConnection
import java.net.URL

/**
 * A board's firmware listed in a release's ota-manifest.json (see tools/ota-manifest-entry.sh): [file] is the
 * app image .bin for LoRa OTA (repeaters only, else empty), [dfu] what the app flashes over Bluetooth
 * (nRF52 .zip) or Wi-Fi (ESP32 .bin), empty in older manifests.
 */
data class FirmwareFile(val release: String, val env: String, val version: String, val file: String,
                        val size: Int, val sha256: String, val url: String,
                        val dfu: String = "", val dfuSize: Int = 0, val dfuSha256: String = "", val dfuUrl: String = "") {
    val hasLoraOta get() = file.isNotEmpty()
    val hasDfu get() = dfu.isNotEmpty()
    val dfuIsZip get() = dfu.endsWith(".zip", ignoreCase = true)
}

object Releases {
    const val DEFAULT_REPO = "n3tk0/MeshCore"

    private fun get(url: String): ByteArray {
        val c = URL(url).openConnection() as HttpURLConnection
        c.connectTimeout = 15_000
        c.readTimeout = 30_000
        c.setRequestProperty("Accept", "application/vnd.github+json, application/octet-stream")
        c.setRequestProperty("User-Agent", "LoraOta")
        try {
            if (c.responseCode !in 200..299) throw java.io.IOException("HTTP ${c.responseCode} for $url")
            return c.inputStream.use { it.readBytes() }
        } finally {
            c.disconnect()
        }
    }

    /**
     * Every firmware of the repository's recent releases, newest release first. With a cache folder
     * the list is kept there (for use with no internet, see [cached]) and kept .bin files no release
     * lists any more are deleted.
     */
    fun list(repo: String = DEFAULT_REPO, cache: File? = null): List<FirmwareFile> {
        val out = ArrayList<FirmwareFile>()
        val releases = JSONArray(String(get("https://api.github.com/repos/$repo/releases?per_page=20")))
        for (i in 0 until releases.length()) {
            val rel = releases.getJSONObject(i)
            val assets = rel.getJSONArray("assets")
            val urls = HashMap<String, String>()
            var manifest: String? = null
            for (a in 0 until assets.length()) {
                val asset = assets.getJSONObject(a)
                urls[asset.getString("name")] = asset.getString("browser_download_url")
                if (asset.getString("name") == "ota-manifest.json") manifest = asset.getString("browser_download_url")
            }
            if (manifest == null) continue
            val files = runCatching { JSONObject(String(get(manifest))).getJSONArray("files") }.getOrNull() ?: continue
            for (f in 0 until files.length()) {
                val e = files.getJSONObject(f)
                val file = e.optString("file")
                val url = if (file.isEmpty()) "" else urls[file] ?: continue
                val dfu = e.optString("dfu")
                val dfuUrl = if (dfu.isEmpty()) "" else urls[dfu] ?: ""
                if (file.isEmpty() && dfuUrl.isEmpty()) continue
                out.add(FirmwareFile(rel.optString("tag_name"), e.getString("env"), e.optString("version"),
                    file, e.optInt("size"), e.optString("sha256").lowercase(), url,
                    if (dfuUrl.isEmpty()) "" else dfu, e.optInt("dfu_size"), e.optString("dfu_sha256").lowercase(), dfuUrl))
            }
        }
        if (cache != null) runCatching {
            writeAtomic(listFile(cache, repo), toJson(out).toString().toByteArray())
            val keep = out.flatMap { listOf("${it.sha256}.bin", "${it.dfuSha256}.bin") }.toSet()
            binDir(cache).listFiles()?.forEach { if (it.name !in keep) it.delete() }
        }
        return out
    }

    /** The list as it was last fetched, for when there is no internet (empty if never fetched). */
    fun cached(repo: String, cache: File): List<FirmwareFile> = runCatching {
        val a = JSONArray(String(listFile(cache, repo).readBytes()))
        (0 until a.length()).map { a.getJSONObject(it) }.map {
            FirmwareFile(it.getString("release"), it.getString("env"), it.getString("version"), it.getString("file"),
                it.getInt("size"), it.getString("sha256"), it.getString("url"),
                it.optString("dfu"), it.optInt("dfu_size"), it.optString("dfu_sha256"), it.optString("dfu_url"))
        }
    }.getOrDefault(emptyList())

    /** The .bin, from the phone when it was downloaded before; checked against its checksum either way. */
    fun download(f: FirmwareFile, cache: File? = null): ByteArray = download(f.file, f.size, f.sha256, f.url, cache)

    /** The file the app flashes (DFU .zip or ESP32 .bin), like [download]. */
    fun downloadDfu(f: FirmwareFile, cache: File? = null): ByteArray = download(f.dfu, f.dfuSize, f.dfuSha256, f.dfuUrl, cache)

    private fun download(name: String, size: Int, sha256: String, url: String, cache: File?): ByteArray {
        val kept = cache?.let { File(binDir(it), "$sha256.bin") }
        if (kept != null && kept.exists()) {
            val data = kept.readBytes()
            if (matches(size, sha256, data)) return data
            kept.delete()
        }
        val data = get(url)
        if (!matches(size, sha256, data)) throw java.io.IOException("$name: download does not match its checksum")
        if (kept != null) runCatching { writeAtomic(kept, data) }
        return data
    }

    private fun matches(size: Int, sha256: String, data: ByteArray) =
        data.size == size && hex(io.github.n3tk0.loraota.delta.DetoolsPatch.sha256(data)) == sha256

    private fun listFile(cache: File, repo: String) = File(cache, "releases-" + repo.replace(Regex("[^A-Za-z0-9._-]"), "_") + ".json")

    private fun binDir(cache: File) = File(cache, "firmware").also { it.mkdirs() }

    private fun writeAtomic(dest: File, data: ByteArray) {
        val tmp = File(dest.path + ".tmp")
        tmp.writeBytes(data)
        if (!tmp.renameTo(dest)) tmp.delete()
    }

    private fun toJson(files: List<FirmwareFile>) = JSONArray().apply {
        for (f in files) put(JSONObject().put("release", f.release).put("env", f.env).put("version", f.version)
            .put("file", f.file).put("size", f.size).put("sha256", f.sha256).put("url", f.url)
            .put("dfu", f.dfu).put("dfu_size", f.dfuSize).put("dfu_sha256", f.dfuSha256).put("dfu_url", f.dfuUrl))
    }
}

fun hex(b: ByteArray): String = b.joinToString("") { "%02x".format(it) }
