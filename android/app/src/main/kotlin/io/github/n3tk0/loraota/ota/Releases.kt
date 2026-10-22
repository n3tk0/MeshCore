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
    /** Made by "Build Firmware for Boards" from the app ([release] is build-<run number>), not a release. */
    val isBuild get() = release.startsWith(BUILD_PREFIX)
    /** A light build: some parts were left out (its files have -lite after the board name). */
    val isLite get() = "-lite-" in file || "-lite-" in dfu
    /** For people: "v1.18.0-abc1234", "v1.18.0-abc1234 · Build #12 · lite". */
    val label get() = listOfNotNull(version.ifEmpty { release },
        if (isBuild) "Build #" + release.removePrefix(BUILD_PREFIX) else null, if (isLite) "lite" else null).joinToString(" · ")

    companion object {
        const val BUILD_PREFIX = "build-"
    }
}

object Releases {
    const val DEFAULT_REPO = "n3tk0/MeshCore"
    /** Stock MeshCore: releases with no ota-manifest.json, for a flash only (stock firmware has no LoRa OTA). */
    const val UPSTREAM_REPO = "meshcore-dev/MeshCore"

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
            if (manifest == null) {
                if (repo == UPSTREAM_REPO && !rel.optBoolean("draft")) out.addAll(fromAssetNames(rel.optString("tag_name"), assets))
                continue
            }
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
            cleanUp(cache)
        }
        return out
    }

    /**
     * A release with no manifest (upstream): its flashable files by name, <env>-<version>.zip (nRF52 DFU) or
     * <env>-<version>.bin next to a -merged.bin (ESP32 app image), checked by the SHA-256 GitHub keeps for each.
     */
    internal fun fromAssetNames(tag: String, assets: JSONArray): List<FirmwareFile> {
        val byName = HashMap<String, JSONObject>()
        for (a in 0 until assets.length()) assets.getJSONObject(a).let { byName[it.getString("name")] = it }
        val out = ArrayList<FirmwareFile>()
        for ((name, a) in byName) {
            val m = ASSET_NAME.matchEntire(name) ?: continue
            val (env, version, ext) = m.destructured
            if (ext == "bin" && (name.endsWith("-merged.bin") || "${name.removeSuffix(".bin")}-merged.bin" !in byName)) continue
            val sha = a.optString("digest").takeIf { it.startsWith("sha256:") }?.removePrefix("sha256:")?.lowercase() ?: continue
            out.add(FirmwareFile(tag, env, version, "", 0, "", "",
                name, a.optInt("size"), sha, a.optString("browser_download_url")))
        }
        return out.sortedBy { it.env.lowercase() }
    }

    // Xiao_nrf52_repeater-v1.18.0-abc1234.zip, Ebyte_EoRa-S3_Repeater-v1.18.0-abc1234-merged.bin
    private val ASSET_NAME = Regex("""(.+?)-(v\d[^/]*?)\.(zip|bin)""")

    /** Kept .bin files no list (any repository's releases, the app's builds) has any more are deleted. */
    @Synchronized   // not between a build's files and its list (see addBuild)
    fun cleanUp(cache: File) {
        val keep = HashSet<String>()
        val lists = cache.listFiles { f -> f.name.startsWith("releases-") && f.name.endsWith(".json") }.orEmpty().toList() +
            listOf(buildsFile(cache))
        for (l in lists) {
            val a = runCatching { JSONArray(String(l.readBytes())) }.getOrNull() ?: continue
            for (i in 0 until a.length()) a.getJSONObject(i).let {
                keep.add("${it.optString("sha256")}.bin")
                keep.add("${it.optString("dfu_sha256")}.bin")
            }
        }
        binDir(cache).listFiles()?.forEach { if (it.name !in keep && !it.name.endsWith(".tmp")) it.delete() }
    }

    /** The firmware the app's builds made (newest first), kept on the phone (see [addBuild]). */
    fun builds(cache: File): List<FirmwareFile> = runCatching { fromJson(JSONArray(String(buildsFile(cache).readBytes()))) }
        .getOrDefault(emptyList())

    /**
     * A build's files: [data] by file name, checked against the entries' checksums, kept on the phone
     * under their checksum (so [download] finds them with no URL) and listed by [builds].
     */
    @Synchronized
    fun addBuild(cache: File, entries: List<FirmwareFile>, data: Map<String, ByteArray>) {
        for (f in entries) {
            for ((name, size, sha) in listOf(Triple(f.file, f.size, f.sha256), Triple(f.dfu, f.dfuSize, f.dfuSha256))) {
                if (name.isEmpty()) continue
                val d = data[name] ?: throw java.io.IOException("$name: not in the build's files")
                if (!matches(size, sha, d)) throw java.io.IOException("$name: does not match its checksum")
                writeAtomic(File(binDir(cache), "$sha.bin"), d)
            }
        }
        val shas = entries.map { it.env to it.sha256 + it.dfuSha256 }.toSet()
        val list = entries + builds(cache).filter { (it.env to it.sha256 + it.dfuSha256) !in shas }
        writeAtomic(buildsFile(cache), toJson(list.take(MAX_BUILDS)).toString().toByteArray())
        cleanUp(cache)
    }

    private const val MAX_BUILDS = 20

    private fun buildsFile(cache: File) = File(cache, "builds.json")

    /** The list as it was last fetched, for when there is no internet (empty if never fetched). */
    fun cached(repo: String, cache: File): List<FirmwareFile> = runCatching {
        fromJson(JSONArray(String(listFile(cache, repo).readBytes())))
    }.getOrDefault(emptyList())

    private fun fromJson(a: JSONArray) = (0 until a.length()).map { a.getJSONObject(it) }.map {
        FirmwareFile(it.getString("release"), it.getString("env"), it.getString("version"), it.getString("file"),
            it.getInt("size"), it.getString("sha256"), it.getString("url"),
            it.optString("dfu"), it.optInt("dfu_size"), it.optString("dfu_sha256"), it.optString("dfu_url"))
    }

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
        // a build's file is only on the phone: no URL to fetch it from again
        if (url.isEmpty()) throw java.io.IOException("$name: no longer on the phone, build it again")
        val data = get(url)
        if (!matches(size, sha256, data)) throw java.io.IOException("$name: download does not match its checksum")
        if (kept != null) runCatching { writeAtomic(kept, data) }
        return data
    }

    internal fun matches(size: Int, sha256: String, data: ByteArray) =
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
