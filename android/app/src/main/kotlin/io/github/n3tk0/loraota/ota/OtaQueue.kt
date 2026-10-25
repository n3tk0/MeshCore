package io.github.n3tk0.loraota.ota

import org.json.JSONArray
import org.json.JSONObject

/** How one repeater of a queue went. */
enum class QueueResult { PENDING, DONE, CURRENT, SKIPPED, FAILED }

/** A repeater in the queue: its public key (hex), its name, and how it went (with a note for people). */
class QueueEntry(val key: String, val name: String, var result: QueueResult = QueueResult.PENDING, var note: String = "")

/**
 * Updates over LoRa for several repeaters, one after another, all to the same release: each one gets
 * that release's file for the board it runs now. Kept on the phone after every repeater, so a queue cut
 * short (Stop, the app closed, the companion gone) goes on where it was.
 */
class OtaQueueState(val release: String, val label: String, val entries: List<QueueEntry>) {
    val next: QueueEntry? get() = entries.firstOrNull { it.result == QueueResult.PENDING }
    val finished get() = next == null
    fun count(r: QueueResult) = entries.count { it.result == r }

    /** Failed and skipped ones once more (e.g. after their route or password was fixed). */
    fun retryFailed() {
        for (e in entries) if (e.result == QueueResult.FAILED || e.result == QueueResult.SKIPPED) {
            e.result = QueueResult.PENDING
            e.note = ""
        }
    }

    fun toJson(): String = JSONObject()
        .put("release", release)
        .put("label", label)
        .put("entries", JSONArray().apply {
            for (e in entries) put(JSONObject().put("key", e.key).put("name", e.name).put("result", e.result.name).put("note", e.note))
        })
        .toString()

    companion object {
        fun fromJson(s: String?): OtaQueueState? = runCatching {
            val o = JSONObject(s ?: return null)
            val a = o.getJSONArray("entries")
            OtaQueueState(o.getString("release"), o.optString("label"), (0 until a.length()).map {
                val e = a.getJSONObject(it)
                QueueEntry(e.getString("key"), e.getString("name"),
                    runCatching { QueueResult.valueOf(e.getString("result")) }.getOrDefault(QueueResult.PENDING), e.optString("note"))
            })
        }.getOrNull()

        /**
         * The order to go in: the ones with no hop first (only they can be updated over LoRa; a route can
         * change since the list was read), then by hops, unknown routes last; by name within each.
         */
        fun <T> order(items: List<T>, hops: (T) -> Int?, name: (T) -> String): List<T> =
            items.sortedWith(compareBy<T>({ hops(it) ?: Int.MAX_VALUE }, { name(it).lowercase() }))

        /** Releases (and builds) that have LoRa OTA files, newest first as listed, once each: (release, label). */
        fun releases(files: List<FirmwareFile>): List<Pair<String, String>> =
            files.filter { it.hasLoraOta }.distinctBy { it.release }.map { it.release to it.label }

        /** The release's file for the board [running] is: the same env. */
        fun target(files: List<FirmwareFile>, release: String, running: FirmwareFile): FirmwareFile? =
            files.firstOrNull { it.release == release && it.env == running.env && it.hasLoraOta }
    }
}
