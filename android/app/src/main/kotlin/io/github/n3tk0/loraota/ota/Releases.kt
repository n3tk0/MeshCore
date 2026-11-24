package io.github.n3tk0.loraota.ota

import io.github.n3tk0.loraota.flash.BoardCheck
import io.github.n3tk0.loraota.flash.Role
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

    /** False when the first page was not full (or the count is known to be 0 < PER_PAGE): there is no page 2 to search. */
    fun hasOlderPages(firstPageReleases: Int) = firstPageReleases < 0 || firstPageReleases >= PER_PAGE

    private fun pageJson(repo: String, n: Int) = JSONArray(String(get("https://api.github.com/repos/$repo/releases?per_page=$PER_PAGE&page=$n")))

    /** What [listDetailed] found: [manifestFailed] releases had a manifest that did not download ([lastError] says why). */
    class Listing(val files: List<FirmwareFile>, val manifestFailed: Int = 0, val lastError: String = "",
                  /** The tags of the releases whose manifest failed (their entries are from the asset list). */
                  val failedTags: Set<String> = emptySet(),
                  /** How many releases the page had (-1: not known); fewer than a full page: no older page exists. */
                  val releaseCount: Int = -1)

    /** A short reason for a person: the exception's class and its HTTP code / message. */
    internal fun describe(e: Throwable): String =
        if (e is HttpError) "HTTP ${e.code}" else e.javaClass.simpleName + (e.message?.let { ": $it" } ?: "")

    /** Every firmware of the page: all manifests are read (for the UI list). */
    private fun page(repo: String, n: Int): Listing {
        val releases = pageJson(repo, n)
        var failed = 0
        var last = ""
        val tags = LinkedHashSet<String>()
        val files = parseReleases(repo, releases, { get(it) }, onFetchFailed = { e, _ -> failed++; last = describe(e) },
            onFallback = { tags.add(it) })
        return Listing(files, failed, last, tags, releases.length())
    }

    /** A page for the search: only the manifests of the releases that may have [sha] are read. */
    private fun searchPage(repo: String, n: Int, sha: String): Page {
        val releases = pageJson(repo, n)
        var failed = false
        val files = parseReleases(repo, releases, { get(it) }, { mayHave(it, sha) }, { _, covered -> if (!covered) failed = true })
        return Page(releases.length(), files, failed)
    }

    private fun JSONObject.str(key: String): String = if (isNull(key)) "" else optString(key)

    /**
     * Whether a release (its assets) may have the app image with [sha]: an asset's digest is it, or a .bin
     * has no digest (older releases) so nothing excludes it.
     */
    internal fun mayHave(assets: JSONArray, sha: String): Boolean {
        for (a in 0 until assets.length()) {
            val asset = assets.getJSONObject(a)
            val digest = asset.str("digest").takeIf { it.startsWith("sha256:") }?.removePrefix("sha256:")?.lowercase()
            if (digest == null) {
                if (asset.str("name").endsWith(".bin", ignoreCase = true)) return true
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
        val fetched = page(repo, 1)
        if (cache == null) return fetched
        // a release whose manifest failed is shown from its asset list (less than the manifest has): keep what
        // the cache knows of it, and do not clean up (the cached list may still be the fuller one)
        val l = if (fetched.manifestFailed > 0) withCachedFallbacks(fetched, cached(repo, cache)) else fetched
        runCatching {
            writeAtomic(listFile(cache, repo), toJson(l.files).toString().toByteArray())
            if (fetched.manifestFailed == 0) cleanUp(cache)
        }
        return l
    }

    /** [l] with the entries of its failed releases taken from [previous] when that list has the release. */
    internal fun withCachedFallbacks(l: Listing, previous: List<FirmwareFile>): Listing {
        val known = l.failedTags.filter { t -> previous.any { it.release == t } }.toSet()
        if (known.isEmpty()) return l
        val out = ArrayList<FirmwareFile>()
        val done = HashSet<String>()
        for (f in l.files) {
            if (f.release !in known) out.add(f)
            else if (done.add(f.release)) out.addAll(previous.filter { it.release == f.release })
        }
        for (t in known) if (done.add(t)) out.addAll(previous.filter { it.release == t })
        return Listing(out, l.manifestFailed, l.lastError, l.failedTags, l.releaseCount)
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
                               onFetchFailed: (Throwable, Boolean) -> Unit = { _, _ -> },
                               onFallback: (String) -> Unit = {}): List<FirmwareFile> {
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
                if (repo == UPSTREAM_REPO && !rel.optBoolean("draft")) out.addAll(fromAssetNames(rel.str("tag_name"), assets))
                continue
            }
            if (!wanted(assets)) continue   // a search: this release cannot have the firmware
            val published = rel.str("published_at").ifEmpty { rel.str("created_at") }
            val fetched = runCatching { JSONObject(String(fetch(manifest))).getJSONArray("files") }
            val files = fetched.getOrNull()
            if (files == null) {
                // no manifest: the .bin files GitHub lists, checked by the digest it keeps for each
                val (fallback, covered) = fromBinAssets(rel.str("tag_name"), assets, published)
                onFetchFailed(fetched.exceptionOrNull() ?: java.io.IOException("manifest"), covered)
                onFallback(rel.str("tag_name"))
                out.addAll(fallback)
                continue
            }
            for (f in 0 until files.length()) {
                val e = files.getJSONObject(f)
                val file = e.str("file")
                val url = if (file.isEmpty()) "" else urls[file] ?: continue
                val dfu = e.str("dfu")
                val dfuUrl = if (dfu.isEmpty()) "" else urls[dfu] ?: ""
                if (file.isEmpty() && dfuUrl.isEmpty()) continue
                out.add(FirmwareFile(rel.str("tag_name"), e.getString("env"), e.str("version"),
                    file, e.optInt("size"), e.str("sha256").lowercase(), url,
                    if (dfuUrl.isEmpty()) "" else dfu, e.optInt("dfu_size"), e.str("dfu_sha256").lowercase(), dfuUrl,
                    published))
            }
        }
        return out
    }

    private val ROLE_ENV = Regex("""^(.+?_(?:repeater|companion_radio_(?:ble|usb|wifi)|room_server))(?=-|$)""", RegexOption.IGNORE_CASE)

    /**
     * The build env of an asset name without its extension, <env>[-lite]-<version>-<sha7>: the prefix that ends in
     * a role suffix (an env may hold '-': LilyGo_T-Echo_repeater), else the part before "-lite-" / the version.
     */
    internal fun envOf(base: String): String =
        ROLE_ENV.find(base)?.groupValues?.get(1)
            ?: base.substringBefore("-lite-").let { b -> Regex("""-v\d""").find(b)?.let { b.substring(0, it.range.first) } ?: b.substringBefore('-') }

    /**
     * The firmware of a release whose manifest did not download, from its asset list, as the manifest would list
     * it (tools/ota-manifest-entry.sh): a repeater's <env>-....bin with a "sha256:" digest is a LoRa OTA image
     * (the env: [envOf]), with <same name>.zip as its DFU file when there is one, or the .bin itself next to a
     * -merged.bin (ESP32 Wi-Fi OTA). Other roles have no LoRa OTA: only their DFU .zip / ESP32 .bin is listed.
     * The second value: every file that matters had a digest (nothing left unknown).
     */
    internal fun fromBinAssets(tag: String, assets: JSONArray, published: String = ""): Pair<List<FirmwareFile>, Boolean> {
        val byName = HashMap<String, JSONObject>()
        for (a in 0 until assets.length()) assets.getJSONObject(a).let { byName[it.getString("name")] = it }
        fun digest(a: JSONObject?) = a?.str("digest")?.takeIf { it.startsWith("sha256:") }?.removePrefix("sha256:")?.lowercase()
        val bases = java.util.TreeSet<String>()
        for (name in byName.keys) {
            if (name.endsWith(".bin", ignoreCase = true) && !name.endsWith("-merged.bin", ignoreCase = true)) bases.add(name.dropLast(4))
            else if (name.endsWith(".zip", ignoreCase = true)) bases.add(name.dropLast(4))
        }
        val out = ArrayList<FirmwareFile>()
        var covered = true
        for (base in bases) {
            val env = envOf(base)
            if (env.isEmpty()) continue
            val binName = "$base.bin"
            val bin = byName[binName]
            val binSha = digest(bin)
            val binUrl = bin?.str("browser_download_url").orEmpty()
            val binOk = bin != null && binSha != null && binUrl.isNotEmpty()
            val zipName = "$base.zip"
            val zip = byName[zipName]
            val zipSha = digest(zip)
            val zipUrl = zip?.str("browser_download_url").orEmpty()
            val hasZip = zip != null && zipSha != null && zipUrl.isNotEmpty()
            // ESP32: next to a -merged.bin the app .bin is also what Wi-Fi OTA flashes (as the manifest lists it)
            val esp32 = !hasZip && binOk && "$base-merged.bin" in byName
            val dfuName = if (hasZip) zipName else if (esp32) binName else ""
            val dfuSize = if (hasZip) zip!!.optInt("size") else if (esp32) bin!!.optInt("size") else 0
            val dfuSha = if (hasZip) zipSha!! else if (esp32) binSha!! else ""
            val dfuUrl = if (hasZip) zipUrl else if (esp32) binUrl else ""
            if (BoardCheck.role(env) == Role.REPEATER) {
                if (!binOk) { if (bin != null) covered = false; continue }
                out.add(FirmwareFile(tag, env, "", binName, bin!!.optInt("size"), binSha!!, binUrl, dfuName, dfuSize, dfuSha, dfuUrl, published))
            } else {
                // no LoRa OTA (file empty), only what the app flashes
                if (dfuName.isEmpty()) { if (bin != null && !binOk) covered = false; continue }
                out.add(FirmwareFile(tag, env, "", "", 0, "", "", dfuName, dfuSize, dfuSha, dfuUrl, published))
            }
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
            val sha = a.str("digest").takeIf { it.startsWith("sha256:") }?.removePrefix("sha256:")?.lowercase() ?: continue
            out.add(FirmwareFile(tag, env, version, "", 0, "", "",
                name, a.optInt("size"), sha, a.str("browser_download_url")))
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
                keep.add("${it.str("sha256")}.bin")
                keep.add("${it.str("dfu_sha256")}.bin")
            }
        }
        binDir(cache).listFiles()?.forEach { if (it.name !in keep && !it.name.endsWith(".tmp")) it.delete() }
    }

    /** Count and total bytes of cached firmware binaries and release lists (excludes backups and health). */
    fun cacheStats(cache: File): Pair<Int, Long> {
        val bins = File(cache, "firmware").listFiles().orEmpty().filter { it.isFile }
        val jsons = cache.listFiles { f ->
            f.isFile && (f.name.startsWith("releases-") && f.name.endsWith(".json") || f.name == "builds.json")
        }.orEmpty()
        val bytes = bins.sumOf { it.length() } + jsons.sumOf { it.length() }
        return bins.size to bytes
    }

    /** Clears all cached firmware binaries, cached release lists, and downloaded build artifacts. */
    @Synchronized
    fun clearCache(cache: File) {
        File(cache, "firmware").listFiles()?.forEach { it.delete() }
        cache.listFiles { f ->
            f.isFile && (f.name.startsWith("releases-") && f.name.endsWith(".json") || f.name == "builds.json")
        }?.forEach { it.delete() }
    }

    /** The firmware the app's builds made (newest first), kept on the phone (see [addBuild]). */
    fun builds(cache: File): List<FirmwareFile> = runCatching { fromJson(JSONArray(String(buildsFile(cache).readBytes()))) }
        .getOrDefault(emptyList())

    fun isFileCached(f: FirmwareFile, cache: File): Boolean =
        f.hasLoraOta && f.sha256.isNotEmpty() && File(binDir(cache), "${f.sha256}.bin").isFile

    fun isDfuCached(f: FirmwareFile, cache: File): Boolean =
        f.hasDfu && f.dfuSha256.isNotEmpty() && File(binDir(cache), "${f.dfuSha256}.bin").isFile

    fun isAnyCached(f: FirmwareFile, cache: File): Boolean = isFileCached(f, cache) || isDfuCached(f, cache)

    /**
     * All firmware entries (app builds + cached releases across all repos + older cached) that currently
     * have at least one binary saved on disk, deduplicated with builds first and newest releases next.
     */
    fun cachedFirmwares(repo: String, cache: File): List<FirmwareFile> {
        val seen = HashSet<Triple<String, String, String>>()
        val out = ArrayList<FirmwareFile>()
        val repoFiles = cache.listFiles { f -> f.isFile && f.name.startsWith("releases-") && f.name.endsWith(".json") }
            .orEmpty()
            .sortedBy { if (it.name == listFile(cache, repo).name) 0 else 1 }
        val releaseEntries = repoFiles.flatMap { f ->
            runCatching { fromJson(JSONArray(String(f.readBytes()))) }.getOrDefault(emptyList())
        }
        val all = builds(cache) + releaseEntries
        for (f in all) {
            if (!isAnyCached(f, cache)) continue
            val key = Triple(f.env, f.sha256, f.dfuSha256)
            if (seen.add(key)) out.add(f)
        }
        return out
    }

    /** Reads and checksum-verifies an already-cached firmware binary without touching the network. */
    fun readCachedFile(f: FirmwareFile, dfu: Boolean, cache: File): ByteArray? {
        val (size, sha) = if (dfu) f.dfuSize to f.dfuSha256 else f.size to f.sha256
        if (sha.isEmpty()) return null
        val kept = File(binDir(cache), "$sha.bin")
        if (!kept.isFile) return null
        val data = runCatching { kept.readBytes() }.getOrNull() ?: return null
        return if (matches(size, sha, data)) data else null
    }

    /** Removes a single firmware entry's cached binary files (and removes it from builds.json if it was a build). */
    @Synchronized
    fun removeCachedFirmware(cache: File, f: FirmwareFile) {
        if (f.isBuild) {
            val remaining = builds(cache).filterNot {
                it.release == f.release && it.env == f.env && it.sha256 == f.sha256 && it.dfuSha256 == f.dfuSha256
            }
            writeAtomic(buildsFile(cache), toJson(remaining).toString().toByteArray())
        }
        val otherEntries = builds(cache) +
            cache.listFiles { file -> file.isFile && file.name.startsWith("releases-") && file.name.endsWith(".json") }
                .orEmpty()
                .flatMap { file -> runCatching { fromJson(JSONArray(String(file.readBytes()))) }.getOrDefault(emptyList()) }
                .filterNot { it.env == f.env && it.sha256 == f.sha256 && it.dfuSha256 == f.dfuSha256 }
        val stillUsed = otherEntries.filter { isAnyCached(it, cache) }
            .flatMap { listOf(it.sha256, it.dfuSha256) }
            .filter { it.isNotEmpty() }
            .toSet()
        for (sha in listOf(f.sha256, f.dfuSha256)) {
            if (sha.isNotEmpty() && sha !in stillUsed) {
                File(binDir(cache), "$sha.bin").delete()
            }
        }
    }

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
        FirmwareFile(it.getString("release"), it.getString("env"), it.str("version"), it.str("file"),
            it.optInt("size"), it.str("sha256"), it.str("url"),
            it.str("dfu"), it.optInt("dfu_size"), it.str("dfu_sha256"), it.str("dfu_url"),
            it.str("published"))
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

    // a unique temp file in the same directory (two writers of one file never share it), renamed over the target
    @Synchronized
    private fun writeAtomic(dest: File, data: ByteArray) {
        val dir = dest.absoluteFile.parentFile ?: File(".")
        dir.mkdirs()
        val tmp = File.createTempFile(dest.name + ".", ".tmp", dir)
        try {
            tmp.writeBytes(data)
            if (!tmp.renameTo(dest)) tmp.delete()
        } catch (e: Exception) {
            tmp.delete()
            throw e
        }
    }

    private fun toJson(files: List<FirmwareFile>) = JSONArray().apply {
        for (f in files) put(JSONObject().put("release", f.release).put("env", f.env).put("version", f.version)
            .put("file", f.file).put("size", f.size).put("sha256", f.sha256).put("url", f.url)
            .put("dfu", f.dfu).put("dfu_size", f.dfuSize).put("dfu_sha256", f.dfuSha256).put("dfu_url", f.dfuUrl)
            .put("published", f.published))
    }
}

fun hex(b: ByteArray): String = b.joinToString("") { "%02x".format(it) }
