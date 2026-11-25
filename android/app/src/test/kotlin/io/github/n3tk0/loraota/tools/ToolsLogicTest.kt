package io.github.n3tk0.loraota.tools

import io.github.n3tk0.loraota.ota.HealthStore
import io.github.n3tk0.loraota.ota.HistorySeries
import java.io.File
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

class ToolsLogicTest {

    @Test
    fun redactSecretsHidesPasswordInConfirmText() {
        assertEquals("password [REDACTED]", CliHelper.redactSecrets("password hunter2"))
    }

    @Test
    fun newPasswordOfParsesPasswordCommand() {
        assertEquals("abc def", CliHelper.newPasswordOf("ab|Password  abc def "))
        assertNull(CliHelper.newPasswordOf("password"))
        assertNull(CliHelper.newPasswordOf("set guest.password 1"))
    }

    @Test
    fun neighborsParserHandlesNoneAndValidLines() {
        assertTrue(NeighborsParser.parse("-none-").isEmpty())
        assertTrue(NeighborsParser.parse("> -none-").isEmpty())

        val raw = """
            > a1b2c3d4:120:24
            00ff1122:15:-28
            badline
            deadbeef:300:0
        """.trimIndent()
        val contacts = mapOf(
            "a1b2c3d499887766" to "Vitosha Repeater",
            "deadbeef01020304" to "Plana Node",
        )
        val parsed = NeighborsParser.parse(raw, contacts)
        assertEquals(3, parsed.size)
        // Sorted by secsAgo ascending
        assertEquals("00ff1122", parsed[0].prefixHex)
        assertEquals(15L, parsed[0].secsAgo)
        assertEquals(-7.0f, parsed[0].snrDb)
        assertEquals(null, parsed[0].resolvedName)

        assertEquals("a1b2c3d4", parsed[1].prefixHex)
        assertEquals(120L, parsed[1].secsAgo)
        assertEquals(6.0f, parsed[1].snrDb)
        assertEquals("Vitosha Repeater", parsed[1].resolvedName)

        assertEquals("deadbeef", parsed[2].prefixHex)
        assertEquals("Plana Node", parsed[2].resolvedName)
        assertEquals(0.0f, parsed[2].snrDb)
    }

    @Test
    fun cliHelperDetectsDangerousAndNoReplyCommands() {
        assertTrue(CliHelper.isDangerous("reboot"))
        assertTrue(CliHelper.isDangerous("  POWEROFF "))
        assertTrue(CliHelper.isDangerous("clear stats"))
        assertTrue(CliHelper.isDangerous("password mysecret"))
        assertTrue(CliHelper.isDangerous("set guest.password 1234"))
        assertTrue(CliHelper.isDangerous("set radio 869.618,125,8,5"))
        assertTrue(CliHelper.isDangerous("set freq 869.5"))
        assertTrue(CliHelper.isDangerous("SET TX 20"))
        assertTrue(CliHelper.isDangerous("tempradio 869.5 125 8 5 30"))
        assertTrue(CliHelper.isDangerous("start ota"))
        assertFalse(CliHelper.isDangerous("tempradio off"))
        assertFalse(CliHelper.isDangerous("get radio"))
        assertFalse(CliHelper.isDangerous("ver"))
        assertFalse(CliHelper.isDangerous("get name"))
        assertFalse(CliHelper.isDangerous("clock sync"))

        assertTrue(CliHelper.expectsNoReply("reboot"))
        assertTrue(CliHelper.expectsNoReply("shutdown"))
        assertFalse(CliHelper.expectsNoReply("advert"))
        assertFalse(CliHelper.expectsNoReply("clear stats"))
    }

    @Test
    fun cliHelperSuggestionsCoverGetSetAndCompanion() {
        val getSug = CliHelper.suggestions("get fl", isCompanion = false)
        assertTrue("get flood.advert.interval" in getSug)
        assertTrue("get flood.max" in getSug)

        val setSug = CliHelper.suggestions("set tx", isCompanion = false)
        assertTrue("set tx " in setSug)

        val compSug = CliHelper.suggestions("get wifi", isCompanion = true)
        assertTrue("get wifi.status" in compSug)
        assertTrue("get wifi.ip" in compSug)
    }

    @Test
    fun cliHelperRedactsPasswordsWithSpacesAndSecretReadReplies() {
        val sample = """
            > password Super Secret 123
            password now: Super Secret 123
            > set guest.password Guest Pass 99
            > set wifi.pwd My Wifi Secret
            Authorization: Bearer ghp_1234567890abcdefghij
            token=github_pat_ABCDEF1234567890
        """.trimIndent()
        val clean = CliHelper.redactSecrets(sample)
        assertFalse("Super" in clean)
        assertFalse("Secret 123" in clean)
        assertFalse("Guest Pass 99" in clean)
        assertFalse("My Wifi Secret" in clean)
        assertFalse("ghp_1234567890abcdefghij" in clean)
        assertFalse("github_pat_ABCDEF1234567890" in clean)
        assertTrue("[REDACTED]" in clean)

        assertTrue(CliHelper.isSecretWriteCommand("password foo bar"))
        assertTrue(CliHelper.isSecretWriteCommand("set guest.password 123"))
        assertTrue(CliHelper.isSecretWriteCommand("set wifi.pwd abc"))
        assertTrue(CliHelper.isSecretWriteCommand("set mqtt.password hunter2"))
        assertTrue(CliHelper.isSecretWriteCommand("set mqtt.secret x"))
        assertFalse(CliHelper.isSecretWriteCommand("set tx 20"))
        assertFalse(CliHelper.isSecretWriteCommand("setperm abcd 3"))
        assertFalse(CliHelper.isSecretWriteCommand("get name"))

        val redactedEntries = CliHelper.redactCliEntries(
            listOf(
                true to "get guest.password",
                false to "> hiddenGuestPass",
                true to "get wifi.pwd",
                false to "> hiddenWifiPass",
                true to "ver",
                false to "v1.18.2",
            )
        )
        assertEquals("[REDACTED]", redactedEntries[1])
        assertEquals("[REDACTED]", redactedEntries[3])
        assertEquals("v1.18.2", redactedEntries[5])
    }

    @Test
    fun telemetryCsvExportsCleanRfc4180() {
        val series = HistorySeries(
            ring = HistorySeries.RING_DAY,
            field = HistorySeries.F_BATT_MV,
            lastTime = 1_700_000_600L,
            intervalMinutes = 10,
            values = listOf(3950, null, 4010),
        )
        val csv = TelemetryCsv.export("Node, East", "Battery", "V", series)
        val lines = csv.trim().lines()
        assertEquals(4, lines.size)
        assertTrue(lines[0].startsWith("# device=Node, East"))
        assertEquals("index,timestamp_utc,value,unit", lines[1])
        assertEquals("0,2023-11-14T22:03:20Z,3.950,V", lines[2])
        assertEquals("2,2023-11-14T22:23:20Z,4.010,V", lines[3])
    }

    @Test
    fun diagnosticReportRedactsSecretsInCliAndLogs() {
        val padding = (1..49).map { "[REPLY] ok-$it" }
        val report = DiagnosticReport.build(
            appVersion = "0.2.0",
            versionCode = 142,
            isGroupApk = true,
            androidVersion = "14 (API 34)",
            deviceModel = "Google Pixel",
            companionName = "MyComp",
            companionModel = "T-Deck",
            companionVersion = "v1.18",
            companionOta = true,
            bleLastError = "",
            repeaterName = "R1",
            repeaterIdent = "heltec|v1.18",
            logs = listOf("Bearer ghp_secretvalue123456"),
            cliHistory = listOf(
                "[COMMAND] get guest.password",
                "[REPLY] > boundarySecretMustNotLeak",
            ) + padding.take(44) + listOf(
                "[COMMAND] ab|password my admin pass",
                "[COMMAND] set prv.key deadbeefcafebabe",
                "[COMMAND] set bridge.secret topbridgepw",
                "[COMMAND] set pin 654321",
                "[REPLY] > pin is now 654321",
            ),
        )
        assertFalse("ghp_secretvalue123456" in report)
        assertFalse("my admin pass" in report)
        assertFalse("boundarySecretMustNotLeak" in report)
        assertFalse("deadbeefcafebabe" in report)
        assertFalse("topbridgepw" in report)
        assertFalse("654321" in report)
        assertTrue("[REPLY] [REDACTED]" in report)
        assertTrue(CliHelper.isDangerous("ota rollback"))
        assertTrue(CliHelper.isDangerous("ab|set prv.key 001122"))
        assertTrue(CliHelper.isSecretWriteCommand("ab|set pin 123456"))
        assertTrue(CliHelper.isSecretReadCommand("ab|get bridge.secret"))
        assertTrue(CliHelper.isSecretReadCommand("get guest.password extra"))
        assertTrue("MeshCore Service Diagnostic Report" in report)
    }

    @Test
    fun healthStoreAllLatestReturnsMostRecentPerDevice() {
        val tmp = File.createTempFile("health_test", "").apply { delete(); mkdirs() }
        try {
            val store = HealthStore(tmp)
            store.add("devA", "ok batt=3900mV/60%", time = 1000L)
            store.add("devA", "ok batt=4000mV/80%", time = 2000L)
            store.add("devB", "WARN i2c i2c=stuck batt=3700mV/35%", time = 1500L)
            val latest = store.allLatest()
            assertEquals(2, latest.size)
            assertEquals(2000L, latest["devA"]?.time)
            assertEquals("ok batt=4000mV/80%", latest["devA"]?.line)
            assertEquals(1500L, latest["devB"]?.time)
        } finally {
            tmp.deleteRecursively()
        }
    }
}
