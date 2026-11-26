package io.github.n3tk0.loraota.tools

import kotlinx.coroutines.runBlocking
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

class RepeaterFilterTest {
    private fun list(l: FilterList, r: String?) = RepeaterFilter.parseList(l, r)

    @Test fun unknownCommandMeansNoFilterFirmware() {
        assertTrue(RepeaterFilter.isUnsupported("Unknown command"))
        assertTrue(RepeaterFilter.isUnsupported("> Unknown command"))
        assertTrue(RepeaterFilter.isUnsupported("??: filter"))
        assertFalse(RepeaterFilter.isUnsupported("> Filter: command error"))
        assertFalse(RepeaterFilter.isUnsupported("Filter on: Blocked [ Hash: 3 ]"))
        assertFalse(RepeaterFilter.isUnsupported(null))
    }

    @Test fun okAndErrorReplies() {
        assertTrue(RepeaterFilter.isOk("> Filter: OK"))
        assertTrue(RepeaterFilter.isOk("> Filter: path A1B2 added"))
        assertFalse(RepeaterFilter.isOk("Failed"))
        assertFalse(RepeaterFilter.isOk("> Filter: error hash bytes range is 1-3"))
        assertFalse(RepeaterFilter.isOk("> Filter: command error"))
        assertFalse(RepeaterFilter.isOk(null))
        assertFalse(RepeaterFilter.isOk("  "))
        assertEquals("hash bytes range is 1-3", RepeaterFilter.errorText("> Filter: error hash bytes range is 1-3"))
    }

    @Test fun statusLine() {
        assertEquals(true to false, RepeaterFilter.parseStatus("> Filter on: Blocked [ Hops: 3 | Rate: 12 ]"))
        assertEquals(true to true, RepeaterFilter.parseStatus("> Filter on: Blocked [ Hash: 0 ] (dry-run)"))
        assertEquals(false to false, RepeaterFilter.parseStatus("Filter off: Blocked [ Hash: 0 ]"))
        assertEquals(true to false, RepeaterFilter.parseStatus("> Filter: on"))
        assertNull(RepeaterFilter.parseStatus("Unknown command"))
    }

    @Test fun scalarReplies() {
        assertEquals(true, RepeaterFilter.parseDryRun("> Filter: dry-run on"))
        assertEquals(false, RepeaterFilter.parseDryRun("> Filter: dry-run off"))
        assertNull(RepeaterFilter.parseDryRun("???"))
        assertEquals(2, RepeaterFilter.parseHash("> Filter: minimal 2 bytes path hash size"))
        assertNull(RepeaterFilter.parseHash("> Filter: minimal 9 bytes path hash size"))
        assertEquals(48, RepeaterFilter.parseAdvertHours("> Filter: advert origin window 48h (cache 87/256)"))
        assertEquals(0, RepeaterFilter.parseAdvertHours("> Filter: advert origin window 0h (cache 0/256)"))
        assertEquals(false, RepeaterFilter.parseMalformed("> Filter: malformed text scan off"))
        assertEquals(true, RepeaterFilter.parseMalformed("> Filter: malformed scan on"))
    }

    @Test fun ageReply() {
        assertEquals(60 to false, RepeaterFilter.parseAge("> Message age: max 60m, dropped 0"))
        assertEquals(60 to true, RepeaterFilter.parseAge("> Message age: max 60m, dropped 0 (clock not set, inactive)"))
        assertEquals(0 to false, RepeaterFilter.parseAge("> Message age: off, dropped 0"))
        assertNull(RepeaterFilter.parseAge("Unknown command"))
        assertNull(RepeaterFilter.parseAge(""))
    }

    @Test fun ingressAndNewKeyAreParsedDefensively() {
        assertEquals(FilterIngress(0, 0, 0), RepeaterFilter.parseIngress("> Filter: ingress off"))
        assertEquals(FilterIngress(20, 60, 0), RepeaterFilter.parseIngress("> Filter: ingress 20/60s"))
        assertEquals(FilterIngress(20, 60, 15), RepeaterFilter.parseIngress("> Filter: ingress 20 per 60s soft 15"))
        assertNull(RepeaterFilter.parseIngress("Unknown command"))
        assertNull(RepeaterFilter.parseIngress("> Filter: ingress"))
        assertEquals(FilterNewKey(0, 0), RepeaterFilter.parseNewKey("> Filter: newkey off"))
        assertEquals(FilterNewKey(24, 60), RepeaterFilter.parseNewKey("> Filter: newkey 24h"))
        assertEquals(FilterNewKey(24, 30), RepeaterFilter.parseNewKey("> Filter: newkey 24h, 30 min"))
        // the firmware's own wording
        assertEquals(FilterIngress(0, 0, 0), RepeaterFilter.parseIngress("> Ingress limit off"))
        assertEquals(FilterIngress(5, 60, 3), RepeaterFilter.parseIngress("> Ingress limit 5 per 60s, soft 3 (4 keys)"))
        assertEquals(FilterNewKey(0, 0), RepeaterFilter.parseNewKey("> Newkey quota off (12 keys known)"))
        assertEquals(FilterNewKey(24, 60), RepeaterFilter.parseNewKey("> Newkey: keys under 24h get 1 advert per 60m (12 keys known)"))
    }

    @Test fun listsAreCommaSeparatedOrNone() {
        assertEquals(emptyList(), list(FilterList.PATH, "None")!!.first)
        assertEquals(emptyList(), list(FilterList.PATH, "> None")!!.first)
        assertNull(list(FilterList.PATH, ""))   // no reply text at all: not an empty list
        assertEquals(listOf("A1B2", "C3"), list(FilterList.PATH, "A1B2,C3")!!.first.map { it.key })
        assertNull(list(FilterList.PATH, "> Filter: command error"))
        assertNull(list(FilterList.BLOCK, "Unknown command"))
    }

    @Test fun channelWatchSenderTextKeys() {
        val ch = list(FilterList.CHANNEL, "#bot (a3),#test (5c)")!!.first
        assertEquals(listOf("#bot", "#test"), ch.map { it.key })
        val w = list(FilterList.WATCH, "Public (11),#bots (a3)")!!.first
        assertEquals(listOf("Public", "#bots"), w.map { it.key })
        assertEquals(listOf(false, true), w.map { it.removable })
        val s = list(FilterList.SENDER, "Bob throttle 60s,Bot* block 50%,SpamBot block")!!.first
        assertEquals(listOf("Bob", "Bot*", "SpamBot"), s.map { it.key })
        // a name that has "error" in it is not an error reply
        assertEquals(1, list(FilterList.SENDER, "ErrorBot block")!!.first.size)
    }

    @Test fun truncatedListIsFlagged() {
        val r = list(FilterList.BLOCK, "aabbccdd,11223344..")!!
        assertTrue(r.second)
        assertEquals(2, r.first.size)
        assertFalse(list(FilterList.BLOCK, "aabbccdd")!!.second)
    }

    @Test fun applyUpdatesOnlyWhatItUnderstands() {
        var c = FilterConfig()
        c = RepeaterFilter.apply(c, "filter", "> Filter on: Blocked [ Hash: 0 ] (dry-run)")
        assertTrue(c.enabled)
        assertFalse(c.dryRun, "the dry-run flag comes from `filter dryrun`, not from the summary line")
        c = RepeaterFilter.apply(c, "filter dryrun", "> Filter: dry-run on")
        assertTrue(c.dryRun)
        // a status line without the marker (cut, or a long counter list) does not clear it
        c = RepeaterFilter.apply(c, "filter", "> Filter on: Blocked [ Hash:1 Path:2 ..")
        assertTrue(c.dryRun)
        c = RepeaterFilter.apply(c, "filter hash", "> Filter: minimal 2 bytes path hash size")
        assertEquals(2, c.hash)
        c = RepeaterFilter.apply(c, "filter hash", "garbage")
        assertEquals(2, c.hash)
        c = RepeaterFilter.apply(c, "filter block list", "aabbccdd")
        c = RepeaterFilter.apply(c, "filter block list", "Failed")
        assertEquals(1, c.lists[FilterList.BLOCK]!!.size)
        c = RepeaterFilter.apply(c, "filter ingress", "Unknown command")
        assertNull(c.ingress)
    }

    @Test fun commandsMatchTheCliSpec() {
        assertEquals("filter on", RepeaterFilter.cmdEnable(true))
        assertEquals("filter dryrun off", RepeaterFilter.cmdOnOff("filter dryrun", false))
        assertEquals("filter age off", RepeaterFilter.cmdAge(null))
        assertEquals("filter age 60", RepeaterFilter.cmdAge(60))
        assertEquals("filter ingress off", RepeaterFilter.cmdIngress(null))
        assertEquals("filter ingress 20 60", RepeaterFilter.cmdIngress(FilterIngress(20, 60, 0)))
        assertEquals("filter ingress 20 60 15", RepeaterFilter.cmdIngress(FilterIngress(20, 60, 15)))
        assertEquals("filter newkey 24 60", RepeaterFilter.cmdNewKey(FilterNewKey(24, 60)))
        assertEquals("filter block add aabbccdd", RepeaterFilter.cmdAdd(FilterList.BLOCK, "aabbccdd"))
        assertEquals("filter sender add Bob", RepeaterFilter.cmdAdd(FilterList.SENDER, "Bob"))
        assertEquals("filter sender add Bob 60", RepeaterFilter.cmdAdd(FilterList.SENDER, "Bob", 60))
        assertEquals("filter sender add Bot* 0 50", RepeaterFilter.cmdAdd(FilterList.SENDER, "Bot*", null, 50))
        assertEquals("filter text remove ^BEACON", RepeaterFilter.cmdRemove(FilterList.TEXT, "^BEACON"))
    }

    @Test fun validation() {
        assertNull(RepeaterFilter.validateBlock("aabbccdd"))
        assertEquals(RepeaterFilter.Err.BLOCK_LEN, RepeaterFilter.validateBlock("aabbcc"))
        assertEquals(RepeaterFilter.Err.BLOCK_LEN, RepeaterFilter.validateBlock("a".repeat(66)))
        assertEquals(RepeaterFilter.Err.ODD, RepeaterFilter.validateBlock("aabbccddE"))
        assertEquals(RepeaterFilter.Err.HEX, RepeaterFilter.validateBlock("aabbccgg"))
        assertNull(RepeaterFilter.validatePath("a1"))
        assertNull(RepeaterFilter.validatePath("A1B2C3D4"))
        assertEquals(RepeaterFilter.Err.ODD, RepeaterFilter.validatePath("a1b"))
        assertEquals(RepeaterFilter.Err.PATH_LEN, RepeaterFilter.validatePath("a1b2c3d4e5"))
        assertEquals(RepeaterFilter.Err.PATH_LEN, RepeaterFilter.validatePath("a"))
        assertNull(RepeaterFilter.validateChannel("#bot", false))
        assertEquals(RepeaterFilter.Err.NAME_HASH, RepeaterFilter.validateChannel("bot", false))
        assertEquals(RepeaterFilter.Err.NAME_HASH, RepeaterFilter.validateChannel("Public", false))
        assertNull(RepeaterFilter.validateChannel("Public", true))
        assertEquals(RepeaterFilter.Err.NAME_SPACE, RepeaterFilter.validateChannel("#a b", false))
        assertNull(RepeaterFilter.validateSender("a".repeat(15)))
        assertEquals(RepeaterFilter.Err.SENDER_LONG, RepeaterFilter.validateSender("a".repeat(16)))
        assertNull(RepeaterFilter.validateText("^BEACON"))
        assertEquals(RepeaterFilter.Err.TEXT_LONG, RepeaterFilter.validateText("a".repeat(24)))
        assertNull(RepeaterFilter.validateRuleNumbers("", ""))
        assertNull(RepeaterFilter.validateRuleNumbers("60", "50"))
        assertEquals(RepeaterFilter.Err.PROB, RepeaterFilter.validateRuleNumbers("", "0"))
        assertEquals(RepeaterFilter.Err.SECS, RepeaterFilter.validateRuleNumbers("70000", ""))
        assertNull(RepeaterFilter.validateHash("3"))
        assertEquals(RepeaterFilter.Err.RANGE, RepeaterFilter.validateHash("4"))
        assertNull(RepeaterFilter.validateAdvert("0"))
        assertEquals(RepeaterFilter.Err.RANGE, RepeaterFilter.validateAdvert("721"))
        assertNull(RepeaterFilter.validateAge("10080"))
        assertEquals(RepeaterFilter.Err.RANGE, RepeaterFilter.validateAge("0"))
        assertNull(RepeaterFilter.validateIngress("20", "60", "15"))
        assertEquals(RepeaterFilter.Err.RANGE, RepeaterFilter.validateIngress("20", "60", "20"))
        assertEquals(RepeaterFilter.Err.RANGE, RepeaterFilter.validateIngress("", "60", ""))
        assertNull(RepeaterFilter.validateNewKey("24", ""))
        assertEquals(RepeaterFilter.Err.RANGE, RepeaterFilter.validateNewKey("0", ""))
        // the firmware's ranges: newkey hours 1-720, mins 1-1440; ingress n 1-10000, secs 1-3600; channel name < 24
        assertNull(RepeaterFilter.validateNewKey("720", "1440"))
        assertEquals(RepeaterFilter.Err.RANGE, RepeaterFilter.validateNewKey("721", ""))
        assertEquals(RepeaterFilter.Err.RANGE, RepeaterFilter.validateNewKey("24", "1441"))
        assertNull(RepeaterFilter.validateIngress("10000", "3600", ""))
        assertEquals(RepeaterFilter.Err.RANGE, RepeaterFilter.validateIngress("10001", "60", ""))
        assertEquals(RepeaterFilter.Err.RANGE, RepeaterFilter.validateIngress("20", "3601", ""))
        assertNull(RepeaterFilter.validateChannel("#" + "a".repeat(22), false))
        assertEquals(RepeaterFilter.Err.NAME_LONG, RepeaterFilter.validateChannel("#" + "a".repeat(23), false))
        assertNull(RepeaterFilter.restoreScalar("newkey", "8760 60"))
        assertNull(RepeaterFilter.restoreScalar("ingress", "20000 60"))
        assertNull(RepeaterFilter.restoreScalar("ingress", "20 7200"))
    }

    @Test fun backupRoundTrip() {
        val cfg = FilterConfig(enabled = true, dryRun = false, hash = 2, advertHours = 24, ageMin = 0,
            malformed = true, ingress = FilterIngress(20, 60, 15), newKey = FilterNewKey(0, 0))
        val sc = RepeaterFilter.backupScalars(cfg).toMap()
        assertEquals("on", sc["enabled"]); assertEquals("off", sc["age"]); assertEquals("20 60 15", sc["ingress"]); assertEquals("off", sc["newkey"])
        assertEquals("filter ingress 20 60 15", RepeaterFilter.restoreScalar("ingress", sc["ingress"]!!))
        assertEquals("filter age off", RepeaterFilter.restoreScalar("age", "off"))
        assertEquals("filter newkey off", RepeaterFilter.restoreScalar("newkey", "off"))
        assertNull(RepeaterFilter.restoreScalar("hash", "9"))
        assertNull(RepeaterFilter.restoreScalar("bogus", "1"))
        assertEquals("enabled", RepeaterFilter.RESTORE_SCALAR_ORDER.last())
    }

    @Test fun restoreAddsOnlyMissingEntries() {
        val now = list(FilterList.PATH, "A1B2")!!.first
        assertEquals(listOf("filter path add C3"), RepeaterFilter.restoreAdds(FilterList.PATH, "a1b2,C3", now))
        assertEquals(emptyList(), RepeaterFilter.restoreAdds(FilterList.PATH, "None", now))
        val rules = RepeaterFilter.restoreAdds(FilterList.SENDER, "Bob throttle 60s,Bot* block 50%,SpamBot block", emptyList())
        assertEquals(listOf("filter sender add Bob 60", "filter sender add Bot* 0 50", "filter sender add SpamBot"), rules)
        val w = RepeaterFilter.restoreAdds(FilterList.WATCH, "Public (11),#bots (a3)", emptyList())
        assertEquals(listOf("filter watch add #bots"), w)
    }

    @Test fun restoreDoesNotReadDigitsOfARuleNameAsThrottle() {
        val rules = RepeaterFilter.restoreAdds(FilterList.SENDER, "Rover7s block,Bot9s throttle 60s 25%,Mix3% block 40%", emptyList())
        assertEquals(listOf("filter sender add Rover7s", "filter sender add Bot9s 60 25", "filter sender add Mix3% 0 40"), rules)
    }

    // ---- the block list, with the firmware's own strings ----

    private val K1 = "A0A1A2A3A4A5A6A7A8A9AAABACADAEAFB0B1B2B3B4B5B6B7B8B9BABBBCBDBEBF"
    private val K2 = "1011121314151617"   // an entry of exactly 8 bytes

    @Test fun blockListShortFormStripsPlusAndPageTail() {
        val reply = "A0A1A2A3A4A5A6A7+,1011121314151617,2021222324252627+ [3/10 p0]"
        val r = list(FilterList.BLOCK, reply)!!
        assertFalse(r.second)
        assertEquals(listOf("A0A1A2A3A4A5A6A7", "1011121314151617", "2021222324252627"), r.first.map { it.key })
        assertEquals(3 to 10, RepeaterFilter.blockPageInfo(reply))
        // the old wording with a comma inside must not split into a bogus entry either
        assertEquals(1, list(FilterList.BLOCK, "A0A1A2A3A4A5A6A7+ [1/7, page 0]")!!.first.size)
        assertNull(RepeaterFilter.blockPageInfo("A0A1A2A3A4A5A6A7+"))
    }

    @Test fun readBlockWalksEveryPageInTheShortAndTheFullForm() = runBlocking {
        val pages = mapOf(
            "filter block list" to "A0A1A2A3A4A5A6A7+,B0B1B2B3B4B5B6B7+ [2/3 p0]",
            "filter block list 1" to "C0C1C2C3C4C5C6C7 [3/3 p1]",
        )
        val asked = ArrayList<String>()
        val r = RepeaterFilter.readBlock(full = false) { asked.add(it); pages[it] }!!
        assertEquals(listOf("filter block list", "filter block list 1"), asked)
        assertEquals(listOf("A0A1A2A3A4A5A6A7", "B0B1B2B3B4B5B6B7", "C0C1C2C3C4C5C6C7"), r.first.map { it.key })
        assertFalse(r.second)

        val full = mapOf(
            "filter block list 0 full" to "$K1,$K2 [2/3 p0]",
            "filter block list 1 full" to "C0C1C2C3C4C5C6C7 [3/3 p1]",
        )
        val f = RepeaterFilter.readBlock(full = true) { full[it] }!!
        assertEquals(listOf(K1, K2, "C0C1C2C3C4C5C6C7"), f.first.map { it.key })
        assertFalse(f.second)

        // a page that never comes: what was read is kept, flagged incomplete
        val partial = RepeaterFilter.readBlock(full = false) { if (it == "filter block list") pages[it] else null }!!
        assertEquals(2, partial.first.size)
        assertTrue(partial.second)
        // an unsupported / empty first reply
        assertNull(RepeaterFilter.readBlock(full = false) { null })
        assertEquals(emptyList(), RepeaterFilter.readBlock(full = false) { "None" }!!.first)
    }

    @Test fun backupKeepsFullKeysAndRestoreAddsOnlyWholeOnes() {
        // the backup value is the full list reply: whole keys
        val saved = "$K1,$K2"
        val have = list(FilterList.BLOCK, "$K2 [1/1 p0]")!!.first
        assertEquals(listOf("filter block add $K1"), RepeaterFilter.restoreAdds(FilterList.BLOCK, saved, have))
        // an entry that is only a printed prefix (a "+" in an old backup) is not restored as a short key
        assertEquals(listOf("filter block add $K2"), RepeaterFilter.restoreAdds(FilterList.BLOCK, "A0A1A2A3A4A5A6A7+,$K2", emptyList()))
        // present in either case
        assertEquals(emptyList(), RepeaterFilter.restoreAdds(FilterList.BLOCK, saved.lowercase(), list(FilterList.BLOCK, "$K1,$K2")!!.first))
    }

    @Test fun removeUsesThePrintedPrefix() {
        val e = list(FilterList.BLOCK, "A0A1A2A3A4A5A6A7+")!!.first.single()
        assertEquals("filter block remove A0A1A2A3A4A5A6A7", RepeaterFilter.cmdRemove(FilterList.BLOCK, e.key))
        assertNull(RepeaterFilter.validateBlock(e.key))
    }

    @Test fun replyBufferComment() {
        assertEquals("filter block list 2 full", RepeaterFilter.cmdBlockPage(2, true))
        assertEquals("filter block list", RepeaterFilter.cmdBlockPage(0, false))
        assertEquals("filter block list 3", RepeaterFilter.cmdBlockPage(3, false))
    }
}
