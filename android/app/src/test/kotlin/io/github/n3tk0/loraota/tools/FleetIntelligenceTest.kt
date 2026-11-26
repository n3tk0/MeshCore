package io.github.n3tk0.loraota.tools

import io.github.n3tk0.loraota.backup.Backup
import io.github.n3tk0.loraota.backup.BackupItem
import io.github.n3tk0.loraota.backup.DeviceKind
import io.github.n3tk0.loraota.ota.FirmwareFile
import io.github.n3tk0.loraota.ota.HealthStore
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertTrue

class FleetIntelligenceTest {

    @Test
    fun loraRadioCalculatorComputesAirtimeBitrateAndSubBandCompliance() {
        // Default EU 869.525 MHz, 250 kHz BW, SF7, CR5 -> fits inside 869.40..869.65 MHz sub-band
        val m = LoraRadioCalculator.compute(869.525, 250.0, 7, 5)
        assertNotNull(m)
        assertFalse(m.exceeds869SubBand)
        assertFalse(m.slowForOta)
        assertTrue(m.bitrateBps in 10_000..12_000)
        assertTrue(m.packet64AirMs in 40.0..120.0)
        assertTrue(m.sensitivityDbm < -120.0)

        // 868.1 MHz with 250 kHz BW is outside the 869.40..869.65 10% duty sub-band
        val outOfBand = LoraRadioCalculator.compute(868.1, 250.0, 11, 8)
        assertNotNull(outOfBand)
        assertTrue(outOfBand.exceeds869SubBand)
        assertTrue(outOfBand.slowForOta)

        // Hz-based radio string parsing
        val fromStr = LoraRadioCalculator.computeFromRadioString("869525000,250000,7,5")
        assertNotNull(fromStr)
        assertEquals(869.525, fromStr.freqMhz, 0.001)
        assertEquals(250.0, fromStr.bwKhz, 0.1)
    }

    @Test
    fun configProfileExcludesIdentityAndDiffBackupsFindsDifferences() {
        val raw = mapOf(
            "name" to "Vitosha-R1",
            "lat" to "42.6977",
            "lon" to "23.3219",
            "password" to "secret",
            "guest.password" to "guest123",
            "tx" to "22",
            "advert.interval" to "120",
            "repeat" to "on",
        )
        val clean = ConfigProfileLogic.extractTemplateSettings(raw)
        assertEquals(mapOf("tx" to "22", "advert.interval" to "120", "repeat" to "on"), clean)

        val b1 = Backup(
            kind = DeviceKind.REPEATER,
            deviceKey = "k1",
            deviceName = "R1",
            time = 1000L,
            auto = false,
            firmware = "v1",
            items = listOf(
                BackupItem(BackupItem.GROUP_SETTING, "tx", "tx", "20"),
                BackupItem(BackupItem.GROUP_SETTING, "repeat", "repeat", "on"),
                BackupItem(BackupItem.GROUP_SETTING, "password", "password", "p1"),
            ),
        )
        val b2 = Backup(
            kind = DeviceKind.REPEATER,
            deviceKey = "k2",
            deviceName = "R2",
            time = 2000L,
            auto = false,
            firmware = "v1",
            items = listOf(
                BackupItem(BackupItem.GROUP_SETTING, "tx", "tx", "22"),
                BackupItem(BackupItem.GROUP_SETTING, "repeat", "repeat", "on"),
                BackupItem(BackupItem.GROUP_SETTING, "password", "password", "p2"),
            ),
        )
        val diff = ConfigProfileLogic.diffBackups(b1, b2)
        assertEquals(listOf(Triple("tx", "20", "22")), diff)
    }

    @Test
    fun meshGeoTopologyComputesDistanceBearingAndDetectsAsymmetricLinks() {
        // Sofia center (42.6977, 23.3219) to Cherni Vrah (42.5636, 23.2784) -> ~15.3 km South-Southwest
        val geo = MeshGeoTopology.distanceAndBearing(42.6977, 23.3219, 42.5636, 23.2784)
        assertNotNull(geo)
        assertTrue(geo.distanceKm in 14.0..17.0)
        assertTrue(geo.bearingDeg in 180..210)

        val keyA = "aaaa1111222233334444555566667777888899990000aaaabbbbccccddddeeee"
        val keyB = "bbbb1111222233334444555566667777888899990000aaaabbbbccccddddeeee"
        val observed = mapOf(
            keyA to listOf(NeighborEntry("bbbb1111", 30L, -9.5f, "Node-B")),
            keyB to listOf(NeighborEntry("aaaa1111", 20L, 2.0f, "Node-A")),
        )
        val names = mapOf(keyA to "Node-A", keyB to "Node-B")
        val coords = mapOf(keyA to (42.6977 to 23.3219), keyB to (42.5636 to 23.2784))

        val edges = MeshGeoTopology.buildTopologyEdges(observed, names, coords)
        assertEquals(1, edges.size)
        val e = edges.first()
        assertTrue(e.isAsymmetric)
        assertNotNull(e.distanceKm)
    }

    @Test
    fun pingStatsOfflineDeltaAndFleetAuditIdentifyIssues() {
        val summary = PingStatsCalculator.summarize(
            listOf(
                PingSample(1, true, 420L, "OK"),
                PingSample(2, false, null, "Timeout"),
                PingSample(3, true, 580L, "OK"),
            ),
        )
        assertEquals(3, summary.sent)
        assertEquals(2, summary.received)
        assertEquals(67, summary.successPercent)
        assertEquals(420L, summary.minRttMs)
        assertEquals(500L, summary.avgRttMs)
        assertEquals(580L, summary.maxRttMs)

        val backups = listOf(
            Backup(
                DeviceKind.REPEATER, "k1", "R1", 100L, false, "Xiao_nrf52_repeater|v1.17.0",
                listOf(
                    BackupItem(BackupItem.GROUP_SETTING, "radio", "radio", "869.525,250,7,5"),
                    BackupItem(BackupItem.GROUP_SETTING, "lat", "lat", "0.0"),
                    BackupItem(BackupItem.GROUP_SETTING, "lon", "lon", "0.0"),
                ),
            ),
            Backup(
                DeviceKind.REPEATER, "k2", "R2", 100L, false, "Xiao_nrf52_repeater|v1.18.0",
                listOf(BackupItem(BackupItem.GROUP_SETTING, "radio", "radio", "869.525,250,7,5")),
            ),
            Backup(
                DeviceKind.REPEATER, "k3", "R3-Drift", 100L, false, "Xiao_nrf52_repeater|v1.18.0",
                listOf(
                    BackupItem(BackupItem.GROUP_SETTING, "radio", "radio", "868.100,125,9,5"),
                    BackupItem(BackupItem.GROUP_SETTING, "repeat", "repeat", "off"),
                ),
            ),
        )
        val audit = FleetAuditLogic.audit(
            backups = backups,
            latestHealthByKey = mapOf("k1" to HealthStore.Record(100L, "ok batt=3420mV/12%")),
            knownNames = mapOf("k1" to "R1"),
        )
        assertTrue(audit.any { it.kind == AuditIssueKind.RADIO_MISMATCH && it.deviceName == "R3-Drift" })
        assertTrue(audit.any { it.kind == AuditIssueKind.REPEAT_DISABLED && it.deviceName == "R3-Drift" })
        assertTrue(audit.any { it.kind == AuditIssueKind.MISSING_COORDS && it.deviceName == "R1" })
        assertTrue(audit.any { it.kind == AuditIssueKind.LOW_BATTERY && it.deviceName == "R1" })

        val cachedFw = listOf(
            FirmwareFile("r2", "Xiao_nrf52_repeater", "v1.18.0", "f2.bin", 100, "sha2", ""),
            FirmwareFile("r1", "Xiao_nrf52_repeater", "v1.17.0", "f1.bin", 100, "sha1", ""),
        )
        val readiness = OfflineDeltaInspector.inspect(backups, cachedFw) { true }
        val r1Ready = readiness.first { it.deviceName == "R1" }
        val r2Ready = readiness.first { it.deviceName == "R2" }
        assertEquals(DeltaReadinessStatus.READY, r1Ready.status)
        assertEquals(DeltaReadinessStatus.UP_TO_DATE, r2Ready.status)

        // a backup label without '|' ("<version> <date> <commit> <env>"), the base .bin not cached
        val plain = listOf(
            Backup(
                DeviceKind.REPEATER, "k4", "R4", 100L, false, "v1.16.0 25-Nov-2026 abc1234 Xiao_nrf52_repeater",
                emptyList(),
            ),
        )
        val missingBase = OfflineDeltaInspector.inspect(plain, cachedFw) { true }.first()
        assertEquals("Xiao_nrf52_repeater", missingBase.boardEnv)
        assertEquals(DeltaReadinessStatus.MISSING_BASE, missingBase.status)
    }

    @Test
    fun encryptedWorkspaceBundleRoundTripsAllState() {
        val orig = WorkspaceData(
            backups = listOf(
                Backup(
                    DeviceKind.REPEATER, "dev1", "Peak-1", 12345L, false, "v1.18",
                    listOf(BackupItem(BackupItem.GROUP_SETTING, "tx", "tx", "22")),
                ),
            ),
            healthRecords = mapOf("dev1" to listOf(HealthStore.Record(12345L, "ok batt=4050mV/85%"))),
            passwords = mapOf("dev1" to "topSecretAdmin"),
            pinnedKeys = listOf("dev1"),
            cliSnippets = listOf("sensor list", "region"),
            profiles = listOf(ConfigProfile("Solar Peak", mapOf("tx" to "22", "repeat" to "on"))),
            deviceNames = mapOf("dev1" to "Peak-1"),
            observedNeighbors = mapOf("dev1" to listOf(NeighborEntry("aabbccdd", 15L, 4.5f, "Valley-2"))),
        )
        val encryptedJson = WorkspaceBundle.export(orig, "MasterPass!2026")
        assertTrue(WorkspaceBundle.needsPassword(encryptedJson))
        assertFalse("topSecretAdmin" in encryptedJson)

        val restored = WorkspaceBundle.import(encryptedJson, "MasterPass!2026")
        assertEquals(1, restored.backups.size)
        assertEquals("Peak-1", restored.backups.first().deviceName)
        assertEquals("topSecretAdmin", restored.passwords["dev1"])
        assertEquals(listOf("dev1"), restored.pinnedKeys)
        assertEquals(listOf("sensor list", "region"), restored.cliSnippets)
        assertEquals(1, restored.profiles.size)
        assertEquals("Solar Peak", restored.profiles.first().name)
        assertEquals("Valley-2", restored.observedNeighbors["dev1"]?.first()?.resolvedName)
    }
}
