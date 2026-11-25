package io.github.n3tk0.loraota.github

import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.pm.PackageInstaller
import android.os.Build
import io.github.n3tk0.loraota.ota.HttpError
import io.github.n3tk0.loraota.ota.Releases
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import java.security.MessageDigest
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.asSharedFlow

/** A LoRa OTA Android app release published on GitHub (tags starting with `lora-ota-app-`). */
data class AppRelease(
    val tag: String,
    val version: String,
    val name: String,
    val notes: String,
    val apkName: String,
    val apkUrl: String,
    val apkSize: Long,
    val htmlUrl: String,
    val publishedAt: String,
    val apkSha256: String = "",
)

/**
 * Checks GitHub Releases for newer LoRa OTA Android APK builds and downloads them for installation.
 */
object AppUpdater {
    const val TAG_PREFIX = "lora-ota-app-"
    const val AUTO_CHECK_INTERVAL_MS = 24 * 60 * 60 * 1000L
    private const val MAX_REDIRECTS = 5

    /** Strips tag prefixes like `lora-ota-app-v` or `v` to get a clean version string (e.g. `1.2.3`). */
    fun cleanVersion(raw: String): String {
        var s = raw.trim()
        if (s.startsWith(TAG_PREFIX, ignoreCase = true)) s = s.substring(TAG_PREFIX.length)
        if (s.startsWith("v", ignoreCase = true) && s.length > 1 && s[1].isDigit()) s = s.substring(1)
        return s
    }

    /**
     * Compares two version strings (`major.minor.patch` with optional `-build.N` or SemVer pre-release suffix).
     * Returns > 0 if [a] is newer than [b], < 0 if older, 0 if equal.
     * Release `0.2.0` is considered newer than pre-release `0.2.0-build.5`.
     */
    fun compareVersions(a: String, b: String): Int {
        val pa = parseVersion(a)
        val pb = parseVersion(b)
        val len = maxOf(pa.core.size, pb.core.size)
        for (i in 0 until len) {
            val diff = pa.core.getOrElse(i) { 0 } - pb.core.getOrElse(i) { 0 }
            if (diff != 0) return diff
        }
        if (pa.preParts.isEmpty() && pb.preParts.isEmpty()) return 0
        if (pa.preParts.isEmpty()) return 1
        if (pb.preParts.isEmpty()) return -1
        val preLen = minOf(pa.preParts.size, pb.preParts.size)
        for (i in 0 until preLen) {
            val x = pa.preParts[i]
            val y = pb.preParts[i]
            val xn = x.toLongOrNull()
            val yn = y.toLongOrNull()
            val cmp = when {
                xn != null && yn != null -> xn.compareTo(yn)
                xn != null -> -1 // numeric identifiers have lower precedence than non-numeric (SemVer §11)
                yn != null -> 1
                else -> x.compareTo(y)
            }
            if (cmp != 0) return cmp
        }
        return pa.preParts.size.compareTo(pb.preParts.size)
    }

    fun isNewer(candidate: String, current: String): Boolean = compareVersions(candidate, current) > 0

    private data class ParsedVersion(val core: List<Int>, val preParts: List<String>)

    private fun parseVersion(raw: String): ParsedVersion {
        val clean = cleanVersion(raw)
        val mainPart = clean.substringBefore('-').substringBefore('+')
        val prePart = clean.substringAfter('-', "").substringBefore('+').ifEmpty { null }
        val nums = mainPart.split('.').mapNotNull { it.toIntOrNull() }
        val preParts = prePart?.split('.')?.filter { it.isNotEmpty() }.orEmpty()
        return ParsedVersion(nums.ifEmpty { listOf(0) }, preParts)
    }

    /** Parses GitHub releases JSON array and returns the newest app release (by version, then list order). */
    internal fun parseLatest(releases: JSONArray, includePrerelease: Boolean = true): AppRelease? {
        val found = ArrayList<AppRelease>()
        for (i in 0 until releases.length()) {
            val rel = releases.getJSONObject(i)
            if (rel.optBoolean("draft")) continue
            if (!includePrerelease && rel.optBoolean("prerelease")) continue
            val tag = rel.optString("tag_name")
            if (!tag.startsWith(TAG_PREFIX, ignoreCase = true)) continue
            val assets = rel.optJSONArray("assets") ?: continue
            var apk: JSONObject? = null
            for (a in 0 until assets.length()) {
                val item = assets.getJSONObject(a)
                val name = item.optString("name")
                if (!name.endsWith(".apk", ignoreCase = true)) continue
                if ("-group-" in name.lowercase()) continue
                apk = item
                if (name.startsWith("LoraOta", ignoreCase = true)) break
            }
            if (apk == null) continue
            val url = apk.optString("browser_download_url")
            if (!url.startsWith("https://", ignoreCase = true)) continue
            val sha = apk.optString("digest")
                .takeIf { it.startsWith("sha256:", ignoreCase = true) }
                ?.substringAfter(':')
                ?.lowercase()
                .orEmpty()
            found.add(
                AppRelease(
                    tag = tag,
                    version = cleanVersion(tag),
                    name = rel.optString("name").ifEmpty { tag },
                    notes = rel.optString("body").trim(),
                    apkName = apk.optString("name"),
                    apkUrl = url,
                    apkSize = apk.optLong("size", 0L),
                    htmlUrl = rel.optString("html_url"),
                    publishedAt = rel.optString("published_at").ifEmpty { rel.optString("created_at") },
                    apkSha256 = sha,
                )
            )
        }
        return found.maxWithOrNull { a, b -> compareVersions(a.version, b.version) }
    }

    /** Searches up to [maxPages] pages of GitHub releases (newest first) until an app release is found. */
    internal fun findLatestAcrossPages(
        maxPages: Int = MAX_PAGES,
        includePrerelease: Boolean = true,
        fetchPage: (Int) -> JSONArray,
    ): AppRelease? {
        for (page in 1..maxPages) {
            val arr = fetchPage(page)
            parseLatest(arr, includePrerelease)?.let { return it }
            if (arr.length() < PER_PAGE) break
        }
        return null
    }

    private const val PER_PAGE = 50
    private const val MAX_PAGES = 5

    /**
     * Fetches the latest app release from GitHub across up to 5 pages of releases.
     * GitHub pre-releases are skipped unless [includePrerelease] (the installed build is itself a pre-release).
     */
    fun checkLatest(repo: String = Releases.DEFAULT_REPO, token: String? = null, includePrerelease: Boolean = false): AppRelease? =
        findLatestAcrossPages(includePrerelease = includePrerelease) { page ->
            val url = "https://api.github.com/repos/$repo/releases?per_page=$PER_PAGE&page=$page"
            JSONArray(String(getBytes(url, token)))
        }

    /** Downloads the release APK into [destFile] over HTTPS only, verifying byte count and SHA-256 digest. */
    fun downloadApk(release: AppRelease, destFile: File, token: String? = null, isActive: () -> Boolean = { true }, onProgress: (Int) -> Unit) {
        if (release.apkSha256.isEmpty()) throw IOException("The release has no SHA-256 checksum; update refused")
        destFile.parentFile?.mkdirs()
        val tmp = File(destFile.parentFile, "${destFile.name}.part")
        var target = release.apkUrl
        for (hop in 0..MAX_REDIRECTS) {
            val parsedUrl = URL(target)
            if (parsedUrl.protocol != "https") throw IOException("refusing non-HTTPS APK URL: ${parsedUrl.protocol}")
            val c = parsedUrl.openConnection() as HttpURLConnection
            Releases.authFor(target, token)?.let { c.setRequestProperty("Authorization", it) }
            c.instanceFollowRedirects = false
            c.connectTimeout = 15_000
            c.readTimeout = 60_000
            c.setRequestProperty("Accept", "application/octet-stream")
            c.setRequestProperty("User-Agent", "LoraOta")
            try {
                val code = c.responseCode
                if (code in 300..399 && code != 304) {
                    val loc = c.getHeaderField("Location") ?: throw HttpError(code, target)
                    val next = URL(parsedUrl, loc)
                    if (next.protocol != "https") throw IOException("redirect to ${next.protocol}")
                    target = next.toString()
                    continue
                }
                if (code !in 200..299) throw HttpError(code, target)
                val contentLen = c.contentLengthLong
                val total = contentLen.takeIf { it > 0 } ?: release.apkSize
                var read = 0L
                var lastPct = -1
                val digest = MessageDigest.getInstance("SHA-256")
                c.inputStream.use { input ->
                    tmp.outputStream().use { out ->
                        val buf = ByteArray(16 * 1024)
                        while (true) {
                            val n = input.read(buf)
                            if (n < 0) break
                            if (!isActive()) throw kotlinx.coroutines.CancellationException("APK download cancelled")
                            out.write(buf, 0, n)
                            digest.update(buf, 0, n)
                            read += n
                            if (total > 0) {
                                val pct = ((read * 100) / total).toInt().coerceIn(0, 100)
                                if (pct != lastPct) {
                                    lastPct = pct
                                    onProgress(pct)
                                }
                            }
                        }
                    }
                }
                if (!isActive()) throw kotlinx.coroutines.CancellationException("APK download cancelled")
                if (read <= 0L) throw IOException("empty APK download")
                if (contentLen > 0L && read != contentLen) throw IOException("truncated APK download ($read / $contentLen bytes)")
                if (release.apkSize > 0L && read != release.apkSize) throw IOException("APK size mismatch ($read / ${release.apkSize} bytes)")
                val actualSha = digest.digest().joinToString("") { "%02x".format(it) }
                if (actualSha != release.apkSha256) throw IOException("APK SHA-256 checksum mismatch")
                if (destFile.exists()) destFile.delete()
                if (!tmp.renameTo(destFile)) {
                    tmp.copyTo(destFile, overwrite = true)
                    tmp.delete()
                }
                onProgress(100)
                return
            } catch (e: Exception) {
                tmp.delete()
                throw e
            } finally {
                c.disconnect()
            }
        }
        throw IOException("too many redirects for ${release.apkUrl}")
    }

    private fun getBytes(url: String, token: String?): ByteArray {
        var target = url
        for (hop in 0..MAX_REDIRECTS) {
            val parsedUrl = URL(target)
            if (parsedUrl.protocol != "https") throw IOException("refusing non-HTTPS URL: ${parsedUrl.protocol}")
            val c = parsedUrl.openConnection() as HttpURLConnection
            Releases.authFor(target, token)?.let { c.setRequestProperty("Authorization", it) }
            c.instanceFollowRedirects = false
            c.connectTimeout = 15_000
            c.readTimeout = 30_000
            c.setRequestProperty("Accept", "application/vnd.github+json")
            c.setRequestProperty("User-Agent", "LoraOta")
            try {
                val code = c.responseCode
                if (code in 300..399 && code != 304) {
                    val loc = c.getHeaderField("Location") ?: throw HttpError(code, target)
                    val next = URL(URL(target), loc)
                    if (next.protocol != "https") throw IOException("redirect to ${next.protocol}")
                    target = next.toString()
                    continue
                }
                if (code !in 200..299) throw HttpError(code, target)
                return c.inputStream.use { it.readBytes() }
            } finally {
                c.disconnect()
            }
        }
        throw IOException("too many redirects for $url")
    }

    private const val ACTION_INSTALL_STATUS = "io.github.n3tk0.loraota.INSTALL_STATUS"

    /** Result of a PackageInstaller session, reported by [InstallResultReceiver]. */
    data class InstallResult(val success: Boolean, val message: String?)

    private val installResultsFlow = MutableSharedFlow<InstallResult>(extraBufferCapacity = 8)

    /** Install outcomes (the ViewModel observes this to show errors). */
    val installResults: SharedFlow<InstallResult> = installResultsFlow.asSharedFlow()

    internal fun reportInstallResult(result: InstallResult) {
        installResultsFlow.tryEmit(result)
    }

    /**
     * Installs [apkFile] through a PackageInstaller session. Blocking (writes the whole APK):
     * call from a background thread. The outcome arrives through [installResults].
     */
    fun installApk(ctx: Context, apkFile: File) {
        val installer = ctx.packageManager.packageInstaller
        val params = PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL)
        params.setAppPackageName(ctx.packageName)
        val sessionId = installer.createSession(params)
        val session = installer.openSession(sessionId)
        try {
            apkFile.inputStream().use { input ->
                session.openWrite("base.apk", 0, apkFile.length()).use { out ->
                    input.copyTo(out)
                    session.fsync(out)
                }
            }
            val intent = Intent(ACTION_INSTALL_STATUS)
                .setPackage(ctx.packageName)
                .setClass(ctx, InstallResultReceiver::class.java)
            // the installer fills in the status extras, so the intent must be mutable on API 31+
            val flags = PendingIntent.FLAG_UPDATE_CURRENT or
                (if (Build.VERSION.SDK_INT >= 31) PendingIntent.FLAG_MUTABLE else 0)
            val pi = PendingIntent.getBroadcast(ctx, sessionId, intent, flags)
            session.commit(pi.intentSender)
        } catch (e: Exception) {
            session.abandon()
            throw e
        } finally {
            session.close()
        }
    }
}
