package io.github.n3tk0.loraota.tools

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class CliCommandsTest {
    @Test fun prefixMatchesAreCappedAtFive() {
        val m = CliCommands.match("get ", false)
        assertEquals(5, m.size)
        assertTrue(m.all { it.template.startsWith("get ") })
    }

    @Test fun matchIsCaseInsensitiveAndIgnoresLeadingSpaces() {
        val m = CliCommands.match("  SELF", false)
        assertEquals(listOf("selftest"), m.map { it.template })
    }

    @Test fun nothingForEmptyExactOrArguments() {
        assertTrue(CliCommands.match("", false).isEmpty())
        assertTrue(CliCommands.match("selftest", false).isEmpty())
        assertTrue(CliCommands.match("set tx 20", false).isEmpty())
    }

    @Test fun templatesFillUpToThePlaceholder() {
        val tx = CliCommands.match("set t", false).first { it.template.startsWith("set tx") }
        assertEquals("set tx <dBm>", tx.template)
        assertEquals("set tx ", tx.fillText)
        assertEquals("selftest", CliCommands.match("self", false).single().fillText)
        val temp = CliCommands.match("tempr", false).single()
        assertEquals("tempradio ", temp.fillText)
    }

    @Test fun companionHasItsOwnList() {
        assertTrue(CliCommands.match("get w", true).all { it.template.startsWith("get wifi") })
        assertTrue(CliCommands.match("neigh", true).isEmpty())
        assertFalse(CliCommands.match("neigh", false).isEmpty())
    }

    @Test fun historyKeepsNewestFirstWithoutRepeats() {
        val h = mutableListOf<String>()
        assertTrue(CliHistory.add(h, "ver"))
        assertFalse(CliHistory.add(h, " ver "))
        assertTrue(CliHistory.add(h, "neighbors"))
        assertEquals(listOf("neighbors", "ver"), h)
    }

    @Test fun historyCapsAtFiftyAndSkipsSecrets() {
        val h = mutableListOf<String>()
        for (i in 1..60) CliHistory.add(h, "get x$i")
        assertEquals(50, h.size)
        assertEquals("get x60", h.first())
        assertFalse(CliHistory.add(h, "password hunter2"))
        assertFalse(CliHistory.add(h, "set prv.key abcdef"))
        assertFalse(CliHistory.add(h, "set guest.password 1234"))
        assertFalse(CliHistory.add(h, "  "))
        assertEquals("get x60", h.first())
    }

    @Test fun historyWalk() {
        assertNull(CliHistory.older(0, -1))
        var idx = -1
        idx = CliHistory.older(3, idx)!!
        assertEquals(0, idx)
        idx = CliHistory.older(3, idx)!!
        idx = CliHistory.older(3, idx)!!
        assertEquals(2, idx)
        assertEquals(2, CliHistory.older(3, idx))   // stays on the oldest
        idx = CliHistory.newer(idx)
        assertEquals(1, idx)
        assertEquals(-1, CliHistory.newer(CliHistory.newer(idx)))
        assertEquals(-1, CliHistory.newer(-1))
    }
}
