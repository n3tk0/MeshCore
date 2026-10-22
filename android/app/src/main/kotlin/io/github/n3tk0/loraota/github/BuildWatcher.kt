package io.github.n3tk0.loraota.github

import android.annotation.SuppressLint
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import androidx.core.app.NotificationCompat
import io.github.n3tk0.loraota.Lang
import io.github.n3tk0.loraota.MainActivity
import io.github.n3tk0.loraota.R
import io.github.n3tk0.loraota.ota.FirmwareFile
import io.github.n3tk0.loraota.ota.Releases
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import org.json.JSONObject
import java.io.ByteArrayInputStream
import java.io.IOException
import java.time.Instant
import java.util.zip.ZipInputStream

enum class BuildPhase { STARTED, QUEUED, RUNNING, DOWNLOADING, DONE, FAILED }

/**
 * A build the app started: [requestId] is in the run's name (the workflow's request_id input), so the
 * app finds its own run among the others; [role] is the workflow input the board was picked in.
 */
data class BuildJob(
    val repo: String, val role: String, val board: String, val requestId: String, val dispatchedAt: Long,
    val runId: Long = 0, val runNumber: Int = 0, val htmlUrl: String = "",
    val phase: BuildPhase = BuildPhase.STARTED, val step: String = "", val offline: Boolean = false,
    val error: String = "", val made: Int = 0,   // firmware files it brought to the phone
) {
    val finished get() = phase == BuildPhase.DONE || phase == BuildPhase.FAILED

    fun toJson(): JSONObject = JSONObject().put("repo", repo).put("role", role).put("board", board)
        .put("request_id", requestId).put("dispatched_at", dispatchedAt).put("run_id", runId).put("run_number", runNumber)
        .put("html_url", htmlUrl).put("phase", phase.name).put("step", step).put("error", error).put("made", made)

    companion object {
        fun fromJson(o: JSONObject) = BuildJob(o.getString("repo"), o.getString("role"), o.getString("board"),
            o.getString("request_id"), o.getLong("dispatched_at"), o.optLong("run_id"), o.optInt("run_number"),
            o.optString("html_url"), BuildPhase.valueOf(o.optString("phase", "STARTED")), o.optString("step"),
            false, o.optString("error"), o.optInt("made"))
    }
}

/**
 * Follows the app's build on GitHub until it ends, also with the app in the background (with
 * [BuildService] keeping it alive), then downloads what it made: the run's artifacts, checked
 * against their .ota.json and kept on the phone as firmware (see [Releases.addBuild]).
 * The job is kept, so a restart of the app goes on following it.
 */
object BuildWatcher {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private var follow: Job? = null
    private val _state = MutableStateFlow<BuildJob?>(null)
    val state: StateFlow<BuildJob?> = _state

    private const val POLL_MS = 15_000L
    private const val FIND_RUN_MS = 3 * 60_000L    // GitHub shows a new run within seconds
    private const val MAX_MS = 120 * 60_000L       // a build of one board takes some 4-8 minutes
    private const val DONE_ID = 3

    private fun prefs(ctx: Context) = ctx.getSharedPreferences("build", Context.MODE_PRIVATE)

    /** A build's files did not check out: waiting does not help (unlike no internet). */
    private class Broken(message: String) : Exception(message)

    private fun save(ctx: Context, j: BuildJob?) {
        _state.value = j
        prefs(ctx).edit().apply { if (j == null) remove("job") else putString("job", j.toJson().toString()) }.apply()
    }

    /** The kept job (on the app's start): follows it again if it had not ended. */
    fun resume(ctx: Context) {
        if (_state.value == null) {
            _state.value = prefs(ctx).getString("job", null)?.let { runCatching { BuildJob.fromJson(JSONObject(it)) }.getOrNull() }
        }
        if (_state.value?.finished == false) start(ctx)
    }

    /** A build just dispatched. */
    fun begin(ctx: Context, j: BuildJob) {
        follow?.cancel()
        follow = null
        save(ctx, j)
        start(ctx)
    }

    /** 'Close' on a finished build's card. */
    fun dismiss(ctx: Context) {
        if (_state.value?.finished != true) return
        save(ctx, null)
        ctx.getSystemService(NotificationManager::class.java)?.cancel(DONE_ID)
    }

    /** Stops following (the run on GitHub goes on: its files can still be taken from the Actions page). */
    fun forget(ctx: Context) {
        follow?.cancel()
        follow = null
        save(ctx, null)
        BuildService.stop(ctx)
    }

    private fun start(ctx: Context) {
        val app = ctx.applicationContext
        if (follow?.isActive == true) return
        // started once `follow` is set: run() checks it is still the current one
        val j = scope.launch(start = CoroutineStart.LAZY) {
            try {
                run(app)
            } finally {
                // not when a new build took over (begin cancels this one)
                if (_state.value?.finished != false) BuildService.stop(app)
            }
        }
        follow = j
        BuildService.start(app)
        j.start()
    }

    private suspend fun run(ctx: Context) {
        var j = _state.value ?: return
        // a call to GitHub cannot be cut short: once this follow is cancelled (a new build, Stop following)
        // nothing it learns afterwards may be saved over the newer state
        val me = currentCoroutineContext()[Job]
        val live = { follow === me && me?.isActive == true }
        fun end(j: BuildJob) { if (live()) finish(ctx, j) }
        val token = TokenStore(ctx).get() ?: return end(j.copy(phase = BuildPhase.FAILED, error = str(ctx, R.string.build_err_token)))
        val api = GitHubApi(token, j.repo)
        while (true) {
            try {
                j = step(ctx, api, j, live)
                if (j.finished) return end(j)
                if (j.offline) j = j.copy(offline = false)
            } catch (e: Broken) {
                return end(j.copy(phase = BuildPhase.FAILED, step = "", error = e.message ?: ""))
            } catch (e: GitHubError) {
                // a token taken back or without the right access: waiting does not help
                if (e.code == 401 || e.code == 403 || e.code == 404) {
                    return end(j.copy(phase = BuildPhase.FAILED, error = str(ctx, R.string.build_err_access, e.message ?: "")))
                }
                j = j.copy(offline = true)
            } catch (e: IOException) {
                j = j.copy(offline = true)   // no internet for now: try again
            }
            if (System.currentTimeMillis() - j.dispatchedAt > MAX_MS) {
                return end(j.copy(phase = BuildPhase.FAILED, error = str(ctx, R.string.build_err_timeout)))
            }
            if (!live()) return
            save(ctx, j)
            delay(POLL_MS)
        }
    }

    private fun step(ctx: Context, api: GitHubApi, job: BuildJob, live: () -> Boolean): BuildJob {
        var j = job
        if (j.runId == 0L) {
            val runs = api.runs(GitHubApi.WORKFLOW)
            // an older workflow with no request_id: the newest run started since
            val run = if (j.requestId.isNotEmpty()) runs.firstOrNull { j.requestId in it.title }
                else runs.firstOrNull { created(it) >= j.dispatchedAt - 60_000 }
            if (run == null) {
                if (System.currentTimeMillis() - j.dispatchedAt > FIND_RUN_MS) {
                    return j.copy(phase = BuildPhase.FAILED, error = str(ctx, R.string.build_err_no_run))
                }
                return j
            }
            j = j.copy(runId = run.id, runNumber = run.number, htmlUrl = run.htmlUrl, phase = BuildPhase.QUEUED)
        }
        val run = api.run(j.runId)
        if (!run.completed) {
            val phase = if (run.status == "queued" || run.status == "waiting" || run.status == "pending") BuildPhase.QUEUED else BuildPhase.RUNNING
            val step = if (phase == BuildPhase.RUNNING) runCatching { api.currentStep(j.runId) }.getOrDefault(j.step) else ""
            return j.copy(phase = phase, step = step)
        }
        if (live()) save(ctx, j.copy(phase = BuildPhase.DOWNLOADING, step = ""))
        // a run with some boards failed still brings the ones that built
        val made = download(ctx, api, j)
        return if (run.succeeded && made > 0) j.copy(phase = BuildPhase.DONE, made = made, step = "")
        else j.copy(phase = BuildPhase.FAILED, made = made, step = "", error = str(ctx,
            if (run.succeeded) R.string.build_err_no_files else R.string.build_err_failed, run.conclusion))
    }

    private fun created(r: Run) = runCatching { Instant.parse(r.createdAt).toEpochMilli() }.getOrDefault(0L)

    /** Every artifact of the run: its firmware (by its .ota.json) to the phone; the count of entries. */
    private fun download(ctx: Context, api: GitHubApi, j: BuildJob): Int {
        var made = 0
        for (a in api.artifacts(j.runId)) {
            if (a.expired) continue
            val zip = api.downloadArtifact(a.id)
            try {
                val files = unzip(zip)
                val entries = entries(files, FirmwareFile.BUILD_PREFIX + j.runNumber)
                if (entries.isEmpty()) continue
                Releases.addBuild(ctx.filesDir, entries, files)
                made += entries.size
            } catch (e: IOException) {
                // a damaged zip or a file that does not match its .ota.json: the same again next time
                throw Broken(e.message ?: e.toString())
            }
        }
        return made
    }

    internal fun unzip(zip: ByteArray): Map<String, ByteArray> {
        val out = HashMap<String, ByteArray>()
        ZipInputStream(ByteArrayInputStream(zip)).use { z ->
            while (true) {
                val e = z.nextEntry ?: break
                if (!e.isDirectory) out[e.name.substringAfterLast('/')] = z.readBytes()
            }
        }
        return out
    }

    /** The firmware an artifact holds, from its .ota.json files (see tools/ota-manifest-entry.sh). */
    internal fun entries(files: Map<String, ByteArray>, release: String): List<FirmwareFile> =
        files.filterKeys { it.endsWith(".ota.json") }.values.mapNotNull { raw ->
            runCatching {
                val e = JSONObject(String(raw))
                FirmwareFile(release, e.getString("env"), e.optString("version"), e.optString("file"), e.optInt("size"),
                    e.optString("sha256").lowercase(), "", e.optString("dfu"), e.optInt("dfu_size"),
                    e.optString("dfu_sha256").lowercase(), "")
            }.getOrNull()
        }.filter { it.hasLoraOta || it.hasDfu }

    private fun finish(ctx: Context, j: BuildJob) {
        save(ctx, j)
        notifyDone(ctx, j)
    }

    private fun str(ctx: Context, res: Int, vararg args: Any) = Lang.wrap(ctx).getString(res, *args)

    @SuppressLint("MissingPermission")
    private fun notifyDone(ctx: Context, j: BuildJob) {
        val nm = ctx.getSystemService(NotificationManager::class.java) ?: return
        if (nm.getNotificationChannel(DONE_CHANNEL) == null) {
            nm.createNotificationChannel(NotificationChannel(DONE_CHANNEL, str(ctx, R.string.build_notif_channel_done),
                NotificationManager.IMPORTANCE_DEFAULT))
        }
        val open = PendingIntent.getActivity(ctx, 0,
            Intent(ctx, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP), PendingIntent.FLAG_IMMUTABLE)
        val ok = j.phase == BuildPhase.DONE
        val n = NotificationCompat.Builder(ctx, DONE_CHANNEL)
            .setSmallIcon(if (ok) android.R.drawable.stat_sys_download_done else android.R.drawable.stat_notify_error)
            .setContentTitle(str(ctx, if (ok) R.string.build_notif_done else R.string.build_notif_failed, j.board))
            .setContentText(if (ok) str(ctx, R.string.build_notif_done_text) else j.error)
            .setAutoCancel(true)
            .setContentIntent(open)
            .build()
        runCatching { nm.notify(DONE_ID, n) }
    }

    private const val DONE_CHANNEL = "build_done"
}
