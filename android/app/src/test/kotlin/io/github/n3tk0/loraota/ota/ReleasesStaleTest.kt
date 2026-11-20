package io.github.n3tk0.loraota.ota

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ReleasesStaleTest {
    @Test
    fun staleAfterFiveMinutes() {
        val t = 1_000_000L
        assertTrue(Releases.isStale(0L, t))
        assertFalse(Releases.isStale(t, t + Releases.STALE_MS))
        assertTrue(Releases.isStale(t, t + Releases.STALE_MS + 1))
    }
}
