package io.github.n3tk0.loraota.ota

import org.json.JSONArray
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL

/** A repeater .bin listed in a release's ota-manifest.json (see .github/workflows/build-xiao-repeaters.yml). */
data class FirmwareFile(val release: String, val env: String, val version: String, val file: String,
                        val size: Int, val sha256: String, val url: String)

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

    /** Every firmware .bin of the repository's recent releases, newest release first. */
    fun list(repo: String = DEFAULT_REPO): List<FirmwareFile> {
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
                val url = urls[e.getString("file")] ?: continue
                out.add(FirmwareFile(rel.optString("tag_name"), e.getString("env"), e.optString("version"),
                    e.getString("file"), e.getInt("size"), e.getString("sha256").lowercase(), url))
            }
        }
        return out
    }

    fun download(f: FirmwareFile): ByteArray {
        val data = get(f.url)
        if (data.size != f.size || hex(io.github.n3tk0.loraota.delta.DetoolsPatch.sha256(data)) != f.sha256) {
            throw java.io.IOException("${f.file}: download does not match its checksum")
        }
        return data
    }
}

fun hex(b: ByteArray): String = b.joinToString("") { "%02x".format(it) }
