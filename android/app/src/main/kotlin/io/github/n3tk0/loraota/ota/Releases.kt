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
                        val dfu: String = "", val dfuSize: Int = 0, val dfuSha256: String = "", val dfuUrl: String = "",
                        /** When the release was published (GitHub, ISO 8601) or the app's build was made; empty if unknown. */
                        val published: String = "") {
    /** [published] as epoch milliseconds, 0 when unknown. */
    val publishedMillis get() = runCatching { java.time.Instant.parse(published).toEpochMilli() }.getOrDefault(0L)
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

    /** The short commit: the trailing -<sha7> of the file name (Board-lite-branch-934d2da.bin), else of the version; "" if none. */
    val commit: String get() = listOf(file, dfu).firstNotNullOfOrNull { COMMIT_FILE.find(it)?.groupValues?.get(1) }
        ?: COMMIT_VERSION.find(version)?.groupValues?.get(1) ?: ""

    /** [published] like "19.11 22:29" in [zone] (the phone's), "" when unknown. */
    fun dateLabel(zone: java.time.ZoneId = java.time.ZoneId.systemDefault()): String =
        if (publishedMillis == 0L) "" else java.time.format.DateTimeFormatter.ofPattern("dd.MM HH:mm")
            .format(java.time.Instant.ofEpochMilli(publishedMillis).atZone(zone))

    /** For a list row, so builds of one branch differ: "boards-17 · 934d2da · 19.11 22:29 · lite". */
    fun rowLabel(zone: java.time.ZoneId = java.time.ZoneId.systemDefault()): String = listOfNotNull(
        if (isBuild) "Build #" + release.removePrefix(BUILD_PREFIX) else release.ifEmpty { version },
        commit.ifEmpty { null }, dateLabel(zone).ifEmpty { null }, if (isLite) "lite" else null).joinToString(" · ")

    companion object {
        const val BUILD_PREFIX = "build-"
        private val COMMIT_FILE = Regex("""-([0-9a-f]{7})\.[A-Za-z0-9]+$""")
        private val COMMIT_VERSION = Regex("""-([0-9a-f]{7})$""")
    }
}

/** GitHub answered with an error status. */
class HttpError(val code: Int, url: String) : java.io.IOException("HTTP $code for $url") {
    /** 403 / 429 from the API: the limit of requests with no token (60 an hour for a network). */
    val rateLimited get() = code == 403 || code == 429
}

object Releases {
    /** A list loaded longer ago than this is fetched again when its screen opens. */
    const val STALE_MS = 5 * 60 * 1000L

    /** [loadedAt] 0 = never loaded. */
    fun isStale(loadedAt: Long, now: Long = System.currentTimeMillis()): Boolean = loadedAt == 0L || now - loadedAt > STALE_MS
    const val DEFAULT_REPO = "n3tk0/MeshCore"
    /** Stock MeshCore: releases with no ota-manifest.json, for a flash only (stock firmware has no LoRa OTA). */
    const val UPSTREAM_REPO = "meshcore-dev/MeshCore"

    /** The GitHub token (the user's or the group's) for the API calls: 5000 requests an hour instead of 60 a network. */
    @Volatile var apiToken: String? = null

    /** The Authorization header for [url]: only api.github.com, never a file host (a download redirects away from it). */
    internal fun authFor(url: String, token: String? = apiToken): String? =
        if (!token.isNullOrEmpty() && URL(url).let { it.protocol == "https" && it.host == "api.github.com" }) "Bearer $token" else null

    private fun get(url: String): ByteArray {
        var target = url
        // redirects by hand (a download goes from github.com to another host): each hop gets the Authorization
        // header only if it is api.github.com itself, and only https is followed
        for (hop in 0..MAX_REDIRECTS) {
            val c = URL(target).openConnection() as HttpURLConnection
            authFor(target)?.let { c.setRequestProperty("Authorization", it) }
            c.instanceFollowRedirects = false
            c.connectTimeout = 15_000
            c.readTimeout = 30_000
            c.setRequestProperty("Accept", "application/vnd.github+json, application/octet-stream")
            c.setRequestProperty("User-Agent", "LoraOta")
            try {
                val code = c.responseCode
                if (code in 300..399 && code != 304) {
                    val loc = c.getHeaderField("Location") ?: throw HttpError(code, target)
                    val next = URL(URL(target), loc)
                    if (next.protocol != "https") throw java.io.IOException("redirect to ${next.protocol}")
                    target = next.toString()
                    continue
                }
                if (code !in 200..299) throw HttpError(code, target)
                return c.inputStream.use { it.readBytes() }
            } finally {
                c.disconnect()
            }
        }
        throw java.io.IOException("too many redirects for $url")
    }

    private const val MAX_REDIRECTS = 5

    private const val PER_PAGE = 50
    /** How many pages of releases [findOlder] looks through at most (the first one is [list]'s). */
    const val MAX_PAGES = 10
    /** The page cap of an explicit "search again". */
    const val MAX_PAGES_RETRY = 30

    /**
     * One page of GitHub's release list: how many releases it had and the firmware they list. [incomplete]:
     * a release that had to be checked could not be (its manifest did not download).
     */
    internal class Page(val releases: Int, val files: List<FirmwareFile>, val incomplete: Boolean = false)

    /** The outcome of the search for the running firmware in the older releases. */
    sealed class SearchResult {
        class Found(val file: FirmwareFile) : SearchResult()
        /** Every page was looked through (to the last one) and nothing has it. */
        object NotFound : SearchResult()
        /** The page cap was reached or a fetch failed: it may still be in a release. */
        object Incomplete : SearchResult()
    }

    private fun pageJson(repo: String, n: Int) = JSONArray(String(get("https://api.github.com/repos/$repo/releases?per_page=$PER_PAGE&page=$n")))

    /** What [listDetailed] found: [manifestFailed] releases had a manifest that did not download ([lastError] says why). */
    class Listing(val files: List<FirmwareFile>, val manifestFailed: Int = 0, val lastError: String = "")

    /** A short reason for a person: the exception's class and its HTTP code / message. */
    internal fun describe(e: Throwable): String =
        if (e is HttpError) "HTTP ${e.code}" else e.javaClass.simpleName + (e.message?.let { ": $it" } ?: "")

    /** Every firmware of the page: all manifests are read (for the UI list). */
    private fun page(repo: String, n: Int): Listing {
        val releases = pageJson(repo, n)
        var failed = 0
        var last = ""
        val files = parseReleases(repo, releases, { get(it) }, onFetchFailed = { e, _ -> failed++; last = describe(e) })
        return Listing(files, failed, last)
    }

    /** A page for the search: only the manifests of the releases that may have [sha] are read. */
    private fun searchPage(repo: String, n: Int, sha: String): Page {
        val releases = pageJson(repo, n)
        var failed = false
        val files = parseReleases(repo, releases, { get(it) }, { mayHave(it, sha) }, { _, covered -> if (!covered) failed = true })
        return Page(releases.length(), files, failed)
    }

    /**
     * Whether a release (its assets) may have the app image with [sha]: an asset's digest is it, or a .bin
     * has no digest (older releases) so nothing excludes it.
     */
    internal fun mayHave(assets: JSONArray, sha: String): Boolean {
        for (a in 0 until assets.length()) {
            val asset = assets.getJSONObject(a)
            val digest = asset.optString("digest").takeIf { it.startsWith("sha256:") }?.removePrefix("sha256:")?.lowercase()
            if (digest == null) {
                if (asset.optString("name").endsWith(".bin", ignoreCase = true)) return true
            } else if (digest == sha) {
                return true
            }
        }
        return false
    }

    /**
     * Every firmware of the repository's recent releases (the first 50), newest release first. With a cache
     * folder the list is kept there (for use with no internet, see [cached]) and kept .bin files no release
     * lists any more are deleted. Older releases: [findOlder].
     */
    fun list(repo: String = DEFAULT_REPO, cache: File? = null): List<FirmwareFile> = listDetailed(repo, cache).files

    /** [list] and how many releases had to be shown from GitHub's file list because their manifest did not download. */
    fun listDetailed(repo: String = DEFAULT_REPO, cache: File? = null): Listing {
        val l = page(repo, 1)
        if (cache != null) runCatching {
            writeAtomic(listFile(cache, repo), toJson(l.files).toString().toByteArray())
            cleanUp(cache)
        }
        return l
    }

    /**
     * The firmware whose app image has [sha] (the SHA-256 the repeater tells), from the releases after the
     * first 50: kept from an earlier search, else pages 2.. of the release list until it is found, the last
     * page is read, or [maxPages] pages were read ([SearchResult]). A find is kept in [cache] (for no internet
     * and for [list], which only knows the first page). Throws when GitHub cannot be reached at the start.
     * [onPage] is told each page number before it is read.
     */
    fun findOlder(repo: String, sha: String, cache: File? = null, maxPages: Int = MAX_PAGES,
                  onPage: (Int) -> Unit = {}): SearchResult {
        if (cache != null) olderCached(repo, cache).firstOrNull { it.sha256 == sha }?.let { return SearchResult.Found(it) }
        val r = searchPages(sha, maxPages) { onPage(it); searchPage(repo, it, sha) }
        if (cache != null && r is SearchResult.Found) runCatching { addOlder(repo, cache, r.file) }
        return r
    }

    /** The page search of [findOlder], with the page fetch given (so it needs no network to test). */
    internal fun searchPages(sha: String, maxPages: Int = MAX_PAGES, fetchPage: (Int) -> Page): SearchResult {
        var incomplete = false
        for (n in 2..maxPages) {
            val p = try {
                fetchPage(n)
            } catch (x: Exception) {
                if (n == 2) throw x   // nothing read at all: no internet proves nothing
                return SearchResult.Incomplete
            }
            p.files.firstOrNull { it.sha256.isNotEmpty() && it.sha256 == sha }?.let { return SearchResult.Found(it) }
            if (p.incomplete) incomplete = true
            // empty, or the last (short) page
            if (p.releases < PER_PAGE) return if (incomplete) SearchResult.Incomplete else SearchResult.NotFound
        }
        return SearchResult.Incomplete   // the cap
    }

    private const val MAX_OLDER = 20

    private fun olderFile(cache: File, repo: String) = File(cache, "releases-older-" + repo.replace(Regex("[^A-Za-z0-9._-]"), "_") + ".json")

    /** The firmware [findOlder] found before, newest find first. */
    fun olderCached(repo: String, cache: File): List<FirmwareFile> = runCatching {
        fromJson(JSONArray(String(olderFile(cache, repo).readBytes())))
    }.getOrDefault(emptyList())

    @Synchronized
    private fun addOlder(repo: String, cache: File, f: FirmwareFile) {
        val list = listOf(f) + olderCached(repo, cache).filter { !(it.env == f.env && it.sha256 == f.sha256) }
        writeAtomic(olderFile(cache, repo), toJson(list.take(MAX_OLDER)).toString().toByteArray())
    }

    /** The newest of [candidates] (the same board): by publish time, else the first one (the lists are newest first). */
    fun recommended(candidates: List<FirmwareFile>): FirmwareFile? = candidates.maxByOrNull { it.publishedMillis }

    /** The firmware of a page of releases; [fetch] gets a URL (the manifest). */
    internal fun parseReleases(repo: String, releases: JSONArray, fetch: (String) -> ByteArray,
                               wanted: (JSONArray) -> Boolean = { true },
                               onFetchFailed: (Throwable, Boolean) -> Unit = { _, _ -> }): List<FirmwareFile> {
        val out = ArrayList<FirmwareFile>()
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
            if (!wanted(assets)) continue   // a search: this release cannot have the firmware
            val published = rel.optString("published_at").ifEmpty { rel.optString("created_at") }
            val fetched = runCatching { JSONObject(String(fetch(manifest))).getJSONArray("files") }
            val files = fetched.getOrNull()
            if (files == null) {
                // no manifest: the .bin files GitHub lists, checked by the digest it keeps for each
                val (fallback, covered) = fromBinAssets(rel.optString("tag_name"), assets, published)
                onFetchFailed(fetched.exceptionOrNull() ?: java.io.IOException("manifest"), covered)
                out.addAll(fallback)
                continue
            }
            for (f in 0 until files.length()) {
                val e = files.getJSONObject(f)
                val file = e.optString("file")
                val url = if (file.isEmpty()) "" else urls[file] ?: continue
                val dfu = e.optString("dfu")
                val dfuUrl = if (dfu.isEmpty()) "" else urls[dfu] ?: ""
                if (file.isEmpty() && dfuUrl.isEmpty()) continue
                out.add(FirmwareFile(rel.optString("tag_name"), e.getString("env"), e.optString("version"),
                    file, e.optInt("size"), e.optString("sha256").lowercase(), url,
                    if (dfuUrl.isEmpty()) "" else dfu, e.optInt("dfu_size"), e.optString("dfu_sha256").lowercase(), dfuUrl,
                    published))
            }
        }
        return out
    }

    /**
     * The firmware of a release whose manifest did not download, from its asset list: each <env>-....bin with a
     * "sha256:" digest is a LoRa OTA image (env = the name up to the first '-'), with <same name>.zip as its
     * DFU file when there is one. The second value: every .bin had a digest (nothing left unknown).
     */
    internal fun fromBinAssets(tag: String, assets: JSONArray, published: String = ""): Pair<List<FirmwareFile>, Boolean> {
        val byName = HashMap<String, JSONObject>()
        for (a in 0 until assets.length()) assets.getJSONObject(a).let { byName[it.getString("name")] = it }
        fun digest(a: JSONObject?) = a?.optString("digest")?.takeIf { it.startsWith("sha256:") }?.removePrefix("sha256:")?.lowercase()
        val out = ArrayList<FirmwareFile>()
        var covered = true
        for ((name, a) in byName.entries.sortedBy { it.key }) {
            if (!name.endsWith(".bin", ignoreCase = true) || name.endsWith("-merged.bin", ignoreCase = true)) continue
            val sha = digest(a)
            val url = a.optString("browser_download_url")
            if (sha == null || url.isEmpty()) { covered = false; continue }
            val env = name.dropLast(4).substringBefore('-')
            if (env.isEmpty()) continue
            val zipName = name.dropLast(4) + ".zip"
            val zip = byName[zipName]
            val zipSha = digest(zip)
            val zipUrl = zip?.optString("browser_download_url").orEmpty()
            val hasZip = zip != null && zipSha != null && zipUrl.isNotEmpty()
            out.add(FirmwareFile(tag, env, "", name, a.optInt("size"), sha, url,
                if (hasZip) zipName else "", if (hasZip) zip!!.optInt("size") else 0, if (hasZip) zipSha!! else "",
                if (hasZip) zipUrl else "", published))
        }
        return out to covered
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
        val now = java.time.Instant.now().toString()
        val stamped = entries.map { if (it.published.isEmpty()) it.copy(published = now) else it }   // newest build = recommended
        val shas = stamped.map { it.env to it.sha256 + it.dfuSha256 }.toSet()
        val list = stamped + builds(cache).filter { (it.env to it.sha256 + it.dfuSha256) !in shas }
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
            it.optString("dfu"), it.optInt("dfu_size"), it.optString("dfu_sha256"), it.optString("dfu_url"),
            it.optString("published"))
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
            .put("dfu", f.dfu).put("dfu_size", f.dfuSize).put("dfu_sha256", f.dfuSha256).put("dfu_url", f.dfuUrl)
            .put("published", f.published))
    }
}

fun hex(b: ByteArray): String = b.joinToString("") { "%02x".format(it) }
