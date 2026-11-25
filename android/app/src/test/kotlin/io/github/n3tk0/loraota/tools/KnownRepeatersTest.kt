package io.github.n3tk0.loraota.tools

import io.github.n3tk0.loraota.backup.Backup
import io.github.n3tk0.loraota.backup.BackupItem
import io.github.n3tk0.loraota.backup.DeviceKind
import io.github.n3tk0.loraota.ota.HealthStore
import io.github.n3tk0.loraota.ui.BatteryChem
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class KnownRepeatersTest {
    private fun rec(t: Long, line: String) = HealthStore.Record(t, line)
    private fun backup(key: String, name: String, t: Long, fw: String, kind: DeviceKind = DeviceKind.REPEATER) =
        Backup(kind, key, name, t, false, fw, listOf(BackupItem(BackupItem.GROUP_SETTING, "tx", "tx", "10")))

    private val ok = "ok radio=ok i2c=ok batt=3900mV/80%"
    private val bad = "WARN radio radio=bad batt=3300mV/10%"
    private val warn = "WARN qspi qspi=bad batt=3700mV/50%"

    private fun build(
        health: Map<String, List<HealthStore.Record>> = emptyMap(),
        backups: List<Backup> = emptyList(),
        pinned: List<String> = emptyList(),
        names: Map<String, String> = emptyMap(),
        chems: Map<String, BatteryChem> = emptyMap(),
        pw: List<String> = emptyList(),
    ) = KnownRepeaters.build(health, backups, pinned, names, chems, pw)

    @Test
    fun emptyWhenNothingIsKnown() {
        assertTrue(build(names = mapOf("aa" to "Some contact")).isEmpty())
    }

    @Test
    fun statusAndBatteryFromTheLastCheck() {
        val rows = build(
            health = mapOf("a1" to listOf(rec(1, bad), rec(2, ok)), "b2" to listOf(rec(5, bad)), "c3" to listOf(rec(3, warn))),
            names = mapOf("a1" to "Alpha", "b2" to "Beta", "c3" to "Gamma"),
        )
        val byName = rows.associateBy { it.name }
        assertEquals(KnownHealth.OK, byName.getValue("Alpha").health)
        assertEquals(3.9, byName.getValue("Alpha").batteryV!!, 0.0001)
        assertEquals(KnownHealth.DANGER, byName.getValue("Beta").health)
        assertEquals(KnownHealth.WARN, byName.getValue("Gamma").health)
        assertEquals(2L, byName.getValue("Alpha").lastCheckedMs)
    }

    @Test
    fun percentOnlyWhenChemistryIsKnown() {
        val h = mapOf("a1" to listOf(rec(1, "ok batt=3690mV/50%")), "b2" to listOf(rec(1, "ok batt=3690mV/50%")))
        val rows = build(health = h, chems = mapOf("a1" to BatteryChem.LI_ION)).associateBy { it.key }
        assertNull(rows.getValue("b2").batteryPercent)
        // Li-ion: (3.69 - 3.20) / (4.18 - 3.20) = 50%
        assertEquals(50, rows.getValue("a1").batteryPercent)
        assertEquals(0, KnownRepeaters.percent(2.0, BatteryChem.LI_ION))
        assertEquals(100, KnownRepeaters.percent(9.0, BatteryChem.LIFEPO4))
    }

    @Test
    fun firmwareFromTheLatestBackupThatHasOne() {
        val rows = build(
            backups = listOf(backup("a1", "Alpha", 10, "v1.0"), backup("a1", "Alpha", 20, ""), backup("a1", "Alpha", 5, "v0.9")),
        )
        assertEquals(1, rows.size)
        assertEquals("v1.0", rows[0].firmware)
        assertEquals(20L, rows[0].lastCheckedMs)
        assertTrue(rows[0].hasBackup)
        assertEquals(KnownHealth.UNKNOWN, rows[0].health)
    }

    @Test
    fun companionBackupsAndNonSelfTestLinesAreNotRepeaters() {
        val rows = build(
            health = mapOf("c1" to listOf(rec(1, "telemetry 12 34"))),
            backups = listOf(backup("c2", "Phone node", 5, "x", DeviceKind.COMPANION)),
        )
        assertTrue(rows.isEmpty())
    }

    @Test
    fun pinnedFirstThenMostRecentThenName() {
        val rows = build(
            health = mapOf(
                "a1" to listOf(rec(100, ok)), "b2" to listOf(rec(300, ok)),
                "c3" to listOf(rec(200, ok)), "d4" to listOf(rec(50, ok)),
            ),
            pinned = listOf("d4"),
            names = mapOf("a1" to "A", "b2" to "B", "c3" to "C", "d4" to "D"),
            pw = listOf("e5"),
        )
        assertEquals(listOf("D", "B", "C", "A", "e5".take(8)), rows.map { it.name })
        assertTrue(rows[0].pinned)
        assertTrue(rows.last().hasPassword)
        assertNull(rows.last().lastCheckedMs)
    }

    @Test
    fun nameFallsBackToBackupThenKey() {
        val rows = build(backups = listOf(backup("abcdef0123", "From backup", 1, "")), pinned = listOf("zz99zz99zz"))
        assertEquals(setOf("From backup", "zz99zz99"), rows.map { it.name }.toSet())
    }
}
