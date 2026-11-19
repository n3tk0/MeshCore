package io.github.n3tk0.loraota.ota

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull

class IdentTest {
    private fun info(ident: String) = OtaProtocol.Info(1, 0, 1, 100, ByteArray(32), 0, 0, ident)

    @Test
    fun commitIsTheLastWordBeforeTheBar() {
        val i = OtaProtocol.parseIdent("v1.18.0-abc1234 19-Nov-2026 abc1234|Seeed Studio Xiao_nrf52_repeater")
        assertEquals("abc1234", i.commit)
        assertEquals("v1.18.0-abc1234 19-Nov-2026 abc1234", i.head)
        assertEquals("Seeed Studio Xiao_nrf52_repeater", i.board)
        assertEquals("abc1234", info("v1 19-Nov-2026 abc1234|X").commit)
    }

    @Test
    fun buildByHandIsLocal() {
        assertEquals("local", OtaProtocol.parseIdent("v1.18.0 15 Oct 2026 local|Seeed").commit)
    }

    @Test
    fun oldIdentHasNoCommit() {
        assertNull(OtaProtocol.parseIdent("v1.18.0-abc1234 19-Nov-2026|Seeed").commit)
        assertNull(OtaProtocol.parseIdent("v1.18.0 15 Oct 2026|Seeed").commit)   // a year is not a commit
        assertEquals("Seeed", OtaProtocol.parseIdent("v1 x|Seeed").board)
    }

    @Test
    fun oddIdentsDoNotCrash() {
        assertNull(OtaProtocol.parseIdent("").commit)
        assertNull(OtaProtocol.parseIdent("|").commit)
        assertNull(OtaProtocol.parseIdent("abc1234").commit)   // one word is the version, not a commit
        assertEquals("", OtaProtocol.parseIdent("nobar").board)
    }
}
