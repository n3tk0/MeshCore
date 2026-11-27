package io.github.n3tk0.loraota.tools

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

class RepeaterMailboxTest {
    private val self = "a1b2c3d4" + "0".repeat(56)
    private val ivan = "11223344" + "5".repeat(56)

    @Test fun statusAsTheFirmwareWritesIt() {
        val s = RepeaterMailbox.parseStatus("on, owners 2, kept 3/64, ttl 72h, away 10min")
        assertEquals(MailboxStatus(true, 2, 3, 64, 72, 10), s)
        assertEquals(false, RepeaterMailbox.parseStatus("> off, owners 0, kept 0/8, ttl 1h, away 0min")?.enabled)
        assertNull(RepeaterMailbox.parseStatus("Unknown command"))
        assertNull(RepeaterMailbox.parseStatus(null))
    }

    @Test fun listWithHereAndNoOwners() {
        assertEquals(
            listOf(MailboxOwner("a1b2c3d4", 2, true), MailboxOwner("11223344", 0, false)),
            RepeaterMailbox.parseList("A1B2C3D4: 2 here, 11223344: 0"),
        )
        assertEquals(emptyList(), RepeaterMailbox.parseList("no owners"))
        assertNull(RepeaterMailbox.parseList("Err - mailbox [on|off]"))
    }

    @Test fun okAndErrors() {
        assertTrue(RepeaterMailbox.isOk("OK"))
        assertTrue(RepeaterMailbox.isOk("OK - add an owner too"))
        assertFalse(RepeaterMailbox.isOk("Err - max 4 owners"))
        assertFalse(RepeaterMailbox.isOk(""))
        assertFalse(RepeaterMailbox.isOk("Unknown command"))
        assertEquals("max 4 owners", RepeaterMailbox.errorText("Err - max 4 owners"))
        assertTrue(RepeaterMailbox.isUnsupported("Unknown command"))
    }

    @Test fun recipientsGetNamesAndSelf() {
        val owners = listOf(MailboxOwner("a1b2c3d4", 1, true), MailboxOwner("11223344", 0, false), MailboxOwner("deadbeef", 4, false))
        val r = RepeaterMailbox.recipients(owners, mapOf(ivan to "Иван", self to "Me"), self)
        assertTrue(r[0].isSelf); assertNull(r[0].name); assertEquals(self, r[0].fullKey)
        assertEquals("Иван", r[1].name); assertEquals(ivan, r[1].fullKey)
        assertNull(r[2].name); assertNull(r[2].fullKey); assertFalse(r[2].isSelf)
    }

    @Test fun twoContactsOnOnePrefixGetNoName() {
        val other = "11223344" + "6".repeat(56)
        val r = RepeaterMailbox.recipients(listOf(MailboxOwner("11223344", 0, false)), mapOf(ivan to "A", other to "B"), "")
        assertNull(r[0].name)
    }

    @Test fun keyInput() {
        assertEquals(ivan, RepeaterMailbox.normalizeKey("  0x" + ivan.uppercase() + " "))
        assertNull(RepeaterMailbox.normalizeKey("1122"))
        assertNull(RepeaterMailbox.normalizeKey("z".repeat(64)))
        assertEquals("mailbox add $ivan", RepeaterMailbox.cmdAdd(ivan.uppercase()))
        assertTrue(RepeaterMailbox.cmdAdd(ivan).length + 3 <= 160)   // with the reply tag, inside MAX_TEXT_LEN
        assertTrue(RepeaterMailbox.isOwner(listOf(MailboxOwner("11223344", 0, false)), ivan))
    }

    @Test fun ranges() {
        assertEquals(72, RepeaterMailbox.validTtl("72"))
        assertNull(RepeaterMailbox.validTtl("0"))
        assertNull(RepeaterMailbox.validTtl("721"))
        assertEquals(0, RepeaterMailbox.validAway("0"))
        assertNull(RepeaterMailbox.validAway("1441"))
    }

    @Test fun summaryRoundTrip() {
        val s = MailboxSummary(true, listOf(MailboxRecipient("a1b2c3d4", 2, true, null, true, self),
            MailboxRecipient("11223344", 0, false, "Иван", false, ivan)), 123L)
        val back = assertNotNull(RepeaterMailbox.fromJson(RepeaterMailbox.toJson(s)))
        assertTrue(back.enabled); assertTrue(back.selfIncluded); assertEquals(123L, back.time)
        assertEquals("Иван", back.recipients[1].name); assertEquals(2, back.recipients[0].kept)
        assertNull(RepeaterMailbox.fromJson("{bad"))
    }
}
