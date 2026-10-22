package io.github.n3tk0.loraota.github

import org.json.JSONArray
import org.json.JSONObject
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL

/** GitHub said no: [code] is the HTTP status (401 a bad token, 403/404 a token without the right access). */
class GitHubError(val code: Int, message: String) : IOException(message)

/** One run of the build workflow, as GitHub tells it. */
data class Run(val id: Long, val number: Int, val status: String, val conclusion: String, val title: String,
               val createdAt: String, val htmlUrl: String) {
    val completed get() = status == "completed"
    val succeeded get() = conclusion == "success"
}

data class Artifact(val id: Long, val name: String, val size: Long, val expired: Boolean)

/**
 * The few GitHub REST calls the app makes with the user's token: read the workflow file, start
 * "Build Firmware for Boards" (workflow_dispatch), follow its run and download what it made.
 * A fine-grained token for the repository needs Actions: read and write, and Contents: read.
 */
class GitHubApi(private val token: String, val repo: String) {
    private fun open(url: String, method: String = "GET"): HttpURLConnection {
        val c = URL(if (url.startsWith("http")) url else "https://api.github.com$url").openConnection() as HttpURLConnection
        c.requestMethod = method
        c.connectTimeout = 15_000
        c.readTimeout = 60_000
        c.setRequestProperty("Accept", "application/vnd.github+json")
        c.setRequestProperty("Authorization", "Bearer $token")
        c.setRequestProperty("X-GitHub-Api-Version", "2022-11-28")
        c.setRequestProperty("User-Agent", "LoraOta")
        return c
    }

    private fun check(c: HttpURLConnection) {
        val code = c.responseCode
        if (code in 200..299) return
        val why = runCatching { JSONObject(String(c.errorStream.use { it.readBytes() })).optString("message") }.getOrNull()
        throw GitHubError(code, "GitHub: HTTP $code" + if (why.isNullOrEmpty()) "" else " ($why)")
    }

    private fun get(path: String, accept: String? = null): ByteArray {
        val c = open(path)
        if (accept != null) c.setRequestProperty("Accept", accept)
        try {
            check(c)
            return c.inputStream.use { it.readBytes() }
        } finally {
            c.disconnect()
        }
    }

    private fun json(path: String) = JSONObject(String(get(path)))

    /** The branch a build is made from. */
    fun defaultBranch(): String = json("/repos/$repo").optString("default_branch", "main")

    /** A file of the repository as text (the workflow file, for its boards and options). */
    fun file(path: String, ref: String): String =
        String(get("/repos/$repo/contents/$path?ref=$ref", "application/vnd.github.raw+json"))

    /** Starts the workflow; GitHub answers before the run exists (see [runs]). */
    fun dispatch(workflow: String, ref: String, inputs: Map<String, String>) {
        val c = open("/repos/$repo/actions/workflows/$workflow/dispatches", "POST")
        c.doOutput = true
        c.setRequestProperty("Content-Type", "application/json")
        val body = JSONObject().put("ref", ref).put("inputs", JSONObject(inputs as Map<*, *>))
        try {
            c.outputStream.use { it.write(body.toString().toByteArray()) }
            check(c)
        } finally {
            c.disconnect()
        }
    }

    /** The workflow's latest runs started by hand, newest first. */
    fun runs(workflow: String): List<Run> {
        val a = json("/repos/$repo/actions/workflows/$workflow/runs?event=workflow_dispatch&per_page=10").getJSONArray("workflow_runs")
        return (0 until a.length()).map { run(a.getJSONObject(it)) }
    }

    fun run(id: Long): Run = run(json("/repos/$repo/actions/runs/$id"))

    private fun run(o: JSONObject) = Run(o.getLong("id"), o.optInt("run_number"), o.optString("status"),
        o.optString("conclusion").let { if (it == "null") "" else it }, o.optString("display_title"),
        o.optString("created_at"), o.optString("html_url"))

    /** What the run does now: the step of a job that is running (empty when none is). */
    fun currentStep(id: Long): String {
        val jobs = json("/repos/$repo/actions/runs/$id/jobs?per_page=30").getJSONArray("jobs")
        for (j in 0 until jobs.length()) {
            val job = jobs.getJSONObject(j)
            if (job.optString("status") != "in_progress") continue
            val steps = job.optJSONArray("steps") ?: JSONArray()
            for (s in 0 until steps.length()) {
                val step = steps.getJSONObject(s)
                if (step.optString("status") == "in_progress") return step.optString("name")
            }
            return job.optString("name")
        }
        return ""
    }

    fun artifacts(id: Long): List<Artifact> {
        val a = json("/repos/$repo/actions/runs/$id/artifacts?per_page=100").getJSONArray("artifacts")
        return (0 until a.length()).map { a.getJSONObject(it) }.map {
            Artifact(it.getLong("id"), it.optString("name"), it.optLong("size_in_bytes"), it.optBoolean("expired"))
        }
    }

    /**
     * An artifact's .zip. GitHub answers with a redirect to a short-lived storage link, which is
     * fetched without the token (it must not leave GitHub).
     */
    fun downloadArtifact(id: Long): ByteArray {
        val c = open("/repos/$repo/actions/artifacts/$id/zip")
        c.instanceFollowRedirects = false
        val location = try {
            if (c.responseCode in 300..399) c.getHeaderField("Location") else {
                check(c)
                return c.inputStream.use { it.readBytes() }
            }
        } finally {
            c.disconnect()
        }
        val d = URL(location).openConnection() as HttpURLConnection
        d.connectTimeout = 15_000
        d.readTimeout = 60_000
        try {
            if (d.responseCode !in 200..299) throw IOException("HTTP ${d.responseCode} for the artifact")
            return d.inputStream.use { it.readBytes() }
        } finally {
            d.disconnect()
        }
    }

    companion object {
        const val WORKFLOW = "build-boards.yml"
        const val WORKFLOW_PATH = ".github/workflows/$WORKFLOW"
        /** Where the user makes a fine-grained token. */
        const val NEW_TOKEN_URL = "https://github.com/settings/personal-access-tokens/new"
    }
}
