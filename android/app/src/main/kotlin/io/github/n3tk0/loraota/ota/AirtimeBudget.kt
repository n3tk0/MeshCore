package io.github.n3tk0.loraota.ota

/**
 * Time on air over the last hour, against the band's duty cycle (869.40-869.65 MHz: 10 % = 360 s an hour).
 * One per companion: a paused update that goes on counts what it already sent.
 */
class AirtimeBudget(
    var duty: Double,
    private val windowMs: Long = 3_600_000,
    private val clock: () -> Long = System::currentTimeMillis,
) {
    private val used = ArrayDeque<Pair<Long, Double>>()   // when, seconds on air

    val limitSecs: Double get() = duty * windowMs / 1000.0

    private fun prune(now: Long) {
        while (used.isNotEmpty() && used.first().first + windowMs <= now) used.removeFirst()
    }

    fun usedSecs(): Double {
        prune(clock())
        return used.sumOf { it.second }
    }

    /** Milliseconds until [secs] more fit in the budget (0: now). */
    fun waitMs(secs: Double): Long {
        val now = clock()
        prune(now)
        var free = limitSecs - used.sumOf { it.second }
        if (free >= secs) return 0
        for ((t, s) in used) {
            free += s
            if (free >= secs) return t + windowMs - now
        }
        return windowMs
    }

    fun add(secs: Double) {
        used.addLast(clock() to secs)
    }
}
