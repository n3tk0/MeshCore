package io.github.n3tk0.loraota.tools

import io.github.n3tk0.loraota.backup.Backup
import io.github.n3tk0.loraota.backup.BackupItem
import io.github.n3tk0.loraota.backup.DeviceKind
import io.github.n3tk0.loraota.backup.PasswordBox
import io.github.n3tk0.loraota.ota.AirtimeBudget
import io.github.n3tk0.loraota.ota.FirmwareFile
import io.github.n3tk0.loraota.ota.HealthStore
import io.github.n3tk0.loraota.ota.SideRadio
import io.github.n3tk0.loraota.ota.estimateMinutes
import org.json.JSONArray
import org.json.JSONObject
import java.util.Locale
import kotlin.math.atan2
import kotlin.math.ceil
import kotlin.math.cos
import kotlin.math.ln
import kotlin.math.max
import kotlin.math.pow
import kotlin.math.roundToInt
import kotlin.math.sin
import kotlin.math.sqrt

// ---- 1. LoRa Airtime, Bitrate, Sensitivity & Duty-Cycle Calculator (#8) ----

data class LoraRadioMetrics(
    val freqMhz: Double,
    val bwKhz: Double,
    val sf: Int,
    val cr: Int,
    val bitrateBps: Int,
    val packet64AirMs: Double,
    val sensitivityDbm: Double,
    /** Estimated minutes to transfer a typical 25 KB LoRa OTA delta patch at these radio parameters. */
    val estOta25KbMins: Int,
    /** True when frequency + bandwidth spills outside the 869.40–869.65 MHz 10% duty-cycle sub-band. */
    val exceeds869SubBand: Boolean,
    /** True when packet airtime is very slow (> 800 ms for 64 B), making LoRa OTA sluggish. */
    val slowForOta: Boolean,
)

object LoraRadioCalculator {
    /** Parses a `freq,bw,sf,cr` string (e.g. `869.525,250,7,5` or Hz-based `869525000,250000,7,5`). */
    fun computeFromRadioString(radioValue: String): LoraRadioMetrics? {
        val parts = radioValue.split(',').map { it.trim() }
        if (parts.size < 4) return null
        var freq = parts[0].replace(',', '.').toDoubleOrNull() ?: return null
        var bw = parts[1].replace(',', '.').toDoubleOrNull() ?: return null
        val sf = parts[2].toIntOrNull() ?: return null
        val cr = parts[3].toIntOrNull() ?: return null
        if (freq > 10_000.0) freq /= 1_000_000.0
        if (bw > 1_000.0) bw /= 1_000.0
        return compute(freq, bw, sf, cr)
    }

    /**
     * Standard SX126x / Semtech LoRa physical layer formulas:
     * - Symbol time `Ts = 2^SF / BW`
     * - Equivalent bit rate `Rb = SF * (4 / CR_denom) / Ts`
     * - Time-on-Air for 64-byte payload (8-symbol preamble, explicit header, CRC on, LDRO when Ts >= 16 ms)
     * - Receiver sensitivity `S = -174 + 10*log10(BW_Hz) + NF(6 dB) + SNR_limit(SF)`
     */
    fun compute(freqMhz: Double, bwKhz: Double, sf: Int, cr: Int, payloadBytes: Int = 64): LoraRadioMetrics? {
        if (freqMhz !in 150.0..2500.0 || bwKhz !in 7.0..500.0 || sf !in 5..12 || cr !in 5..8) return null
        val bwHz = bwKhz * 1000.0
        val tSymMs = (1 shl sf).toDouble() / bwKhz // 2^SF / (BW_Hz / 1000)
        val ldro = if (tSymMs >= 16.0) 1 else 0
        val ih = 0 // explicit header
        val crc = 1
        val preambleSymbols = 8.0 + 4.25
        val num = 8 * payloadBytes - 4 * sf + 28 + 16 * crc - 20 * ih
        val den = 4 * (sf - 2 * ldro)
        val payloadSymbols = 8.0 + max(ceil(num.toDouble() / den.toDouble()) * cr, 0.0)
        val packetAirMs = (preambleSymbols + payloadSymbols) * tSymMs

        val bitrateBps = ((sf.toDouble() * (4.0 / cr.toDouble()) * bwHz) / (1 shl sf).toDouble()).roundToInt()

        val snrLimit = when (sf) {
            5 -> -2.5
            6 -> -5.0
            7 -> -7.5
            8 -> -10.0
            9 -> -12.5
            10 -> -15.0
            11 -> -17.5
            else -> -20.0
        }
        val sensitivityDbm = -174.0 + 10.0 * (ln(bwHz) / ln(10.0)) + 6.0 + snrLimit

        // A typical 25 KB delta patch, as the OTA session itself plans it: its airtime (MeshCore's preamble
        // per SF, SF5/6 header) and the 10 % hourly limit in the EU 863-870 MHz band
        val side = SideRadio((freqMhz * 1000.0).roundToInt(), (bwKhz * 1000.0).roundToInt(), sf, cr)
        val budget = AirtimeBudget(if (freqMhz in 863.0..870.0) 0.1 else 1.0)
        val estOtaMins = ceil(estimateMinutes(side, budget, 25 * 1024)).toInt().coerceAtLeast(1)

        // EU 869.40 - 869.65 MHz 10% duty sub-band check when frequency is in 868/869 MHz band
        val halfBwMhz = (bwKhz / 1000.0) / 2.0
        val inEu868Band = freqMhz in 863.0..870.0
        val fits869SubBand = (freqMhz - halfBwMhz) >= 869.399 && (freqMhz + halfBwMhz) <= 869.651
        val exceeds869 = inEu868Band && !fits869SubBand

        return LoraRadioMetrics(
            freqMhz = freqMhz,
            bwKhz = bwKhz,
            sf = sf,
            cr = cr,
            bitrateBps = bitrateBps,
            packet64AirMs = packetAirMs,
            sensitivityDbm = sensitivityDbm,
            estOta25KbMins = estOtaMins,
            exceeds869SubBand = exceeds869,
            slowForOta = packetAirMs > 800.0,
        )
    }
}

// ---- 2. Reusable Repeater Config Profiles / Templates & Backup Diff (#5, #9) ----

data class ConfigProfile(
    val name: String,
    val settings: Map<String, String>,
)

object ConfigProfileLogic {
    /** Keys that belong to a specific node's identity/location/secrets and must NEVER be overwritten by a profile. */
    val EXCLUDED_KEYS = setOf(
        "name",
        "lat",
        "lon",
        "password",
        "guest.password",
        "prv.key",
        "pin",
        "wifi.pwd",
        "bridge.secret",
    )

    /** Extracts only reusable non-identity, non-secret settings for saving into a profile. */
    fun extractTemplateSettings(raw: Map<String, String>): Map<String, String> =
        raw.filterKeys { it !in EXCLUDED_KEYS && it.isNotBlank() }

    /** Computes key-by-key differences between two backups (excluding secrets), sorted by setting key. */
    fun diffBackups(a: Backup, b: Backup): List<Triple<String, String, String>> {
        val hidden = PasswordBox.SECRET_SETTINGS + setOf("pin", "wifi.pwd", "prv.key", "bridge.secret")
        fun toComparableMap(bk: Backup): Map<String, String> {
            val out = LinkedHashMap<String, String>()
            for (item in bk.items) {
                when (item.group) {
                    BackupItem.GROUP_SETTING -> if (item.id !in hidden) out[item.id] = item.value
                    BackupItem.GROUP_CHANNEL -> out["channel:${item.id}"] = item.label.ifEmpty { "#${item.id}" }
                }
            }
            val contacts = bk.items.count { it.group == BackupItem.GROUP_CONTACT }
            if (contacts > 0) out["contacts.count"] = contacts.toString()
            return out
        }
        val mapA = toComparableMap(a)
        val mapB = toComparableMap(b)
        val allKeys = (mapA.keys + mapB.keys).toSortedSet()
        return allKeys.mapNotNull { k ->
            val va = mapA[k] ?: "—"
            val vb = mapB[k] ?: "—"
            if (va != vb) Triple(k, va, vb) else null
        }
    }
}

// ---- 3. Haversine Distance/Bearing & Mesh Link Topology / Asymmetry (#10) ----

data class GeoDistanceBearing(
    val distanceKm: Double,
    val bearingDeg: Int,
    val cardinal: String,
)

data class MeshLinkEdge(
    val nodeAKey: String,
    val nodeAName: String,
    val nodeBKey: String,
    val nodeBName: String,
    /** SNR reported by B when hearing A (A -> B), or null if only one direction is known. */
    val snrAtoBDb: Float?,
    /** SNR reported by A when hearing B (B -> A), or null if only one direction is known. */
    val snrBtoADb: Float?,
    val distanceKm: Double?,
) {
    /** True when both directions are known and differ by >= 6.0 dB (antenna/coax/noise asymmetry). */
    val isAsymmetric: Boolean
        get() = snrAtoBDb != null && snrBtoADb != null && kotlin.math.abs(snrAtoBDb - snrBtoADb) >= 6.0f
}

object MeshGeoTopology {
    private val CARDINALS = arrayOf("N", "NE", "E", "SE", "S", "SW", "W", "NW")

    /** Computes Haversine distance in km and initial compass bearing (0..359°) from (lat1, lon1) to (lat2, lon2). */
    fun distanceAndBearing(lat1: Double, lon1: Double, lat2: Double, lon2: Double): GeoDistanceBearing? {
        if ((lat1 == 0.0 && lon1 == 0.0) || (lat2 == 0.0 && lon2 == 0.0)) return null
        if (lat1 !in -90.0..90.0 || lat2 !in -90.0..90.0 || lon1 !in -180.0..180.0 || lon2 !in -180.0..180.0) return null
        val r = 6371.0
        val dLat = Math.toRadians(lat2 - lat1)
        val dLon = Math.toRadians(lon2 - lon1)
        val rLat1 = Math.toRadians(lat1)
        val rLat2 = Math.toRadians(lat2)

        val a = sin(dLat / 2).pow(2.0) + cos(rLat1) * cos(rLat2) * sin(dLon / 2).pow(2.0)
        val c = 2 * atan2(sqrt(a), sqrt(1 - a))
        val distKm = r * c

        val y = sin(dLon) * cos(rLat2)
        val x = cos(rLat1) * sin(rLat2) - sin(rLat1) * cos(rLat2) * cos(dLon)
        val brng = ((Math.toDegrees(atan2(y, x)) + 360.0) % 360.0).roundToInt() % 360
        val cardinal = CARDINALS[((brng + 22) % 360) / 45]
        return GeoDistanceBearing(distKm, brng, cardinal)
    }

    /** Extracts known `(lat, lon)` coordinates per device key (or lowercase name) from saved backups. */
    fun extractCoordsFromBackups(backups: List<Backup>): Map<String, Pair<Double, Double>> {
        val out = LinkedHashMap<String, Pair<Double, Double>>()
        for (b in backups.sortedBy { it.time }) {
            val map = b.items.filter { it.group == BackupItem.GROUP_SETTING }.associate { it.id to it.value }
            val lat = map["lat"]?.replace(',', '.')?.toDoubleOrNull()
            val lon = map["lon"]?.replace(',', '.')?.toDoubleOrNull()
            val pair = if (lat != null && lon != null && (lat != 0.0 || lon != 0.0)) {
                lat to lon
            } else {
                map["latlon"]?.split(',')?.takeIf { it.size >= 2 }?.let { parts ->
                    val la = parts[0].trim().toLongOrNull()?.let { it / 1_000_000.0 }
                    val lo = parts[1].trim().toLongOrNull()?.let { it / 1_000_000.0 }
                    if (la != null && lo != null && (la != 0.0 || lo != 0.0)) la to lo else null
                }
            }
            if (pair != null) {
                if (b.deviceKey.isNotEmpty()) out[b.deviceKey.lowercase(Locale.ROOT)] = pair
                if (b.deviceName.isNotBlank()) out[b.deviceName.lowercase(Locale.ROOT)] = pair
            }
        }
        return out
    }

    /**
     * Builds bidirectional mesh edges from per-repeater observed neighbor lists
     * (`observerKeyHex -> List<NeighborEntry>`), matching 8-hex prefixes to full keys.
     */
    fun buildTopologyEdges(
        observedByNode: Map<String, List<NeighborEntry>>,
        knownNames: Map<String, String>,
        coordsByKeyOrName: Map<String, Pair<Double, Double>> = emptyMap(),
    ): List<MeshLinkEdge> {
        val allFullKeys = (observedByNode.keys + knownNames.keys).map { it.lowercase(Locale.ROOT) }.distinct()
        fun resolveFullKey(keyOrPrefix: String): String {
            val low = keyOrPrefix.lowercase(Locale.ROOT)
            if (low.length > 8) return low
            return allFullKeys.firstOrNull { it.startsWith(low) } ?: low
        }
        fun displayName(k: String): String {
            val exact = knownNames.entries.firstOrNull { it.key.equals(k, ignoreCase = true) }?.value
            if (!exact.isNullOrBlank()) return exact
            val byPrefix = knownNames.entries.firstOrNull {
                it.key.startsWith(k, ignoreCase = true) || k.startsWith(it.key, ignoreCase = true)
            }?.value
            return byPrefix ?: k.take(8).uppercase(Locale.ROOT)
        }
        fun coordsFor(k: String, name: String): Pair<Double, Double>? =
            coordsByKeyOrName[k.lowercase(Locale.ROOT)] ?: coordsByKeyOrName[name.lowercase(Locale.ROOT)]

        // Directed SNR map: (listenerCanonicalKey, speakerCanonicalKey) -> snrDb
        val directedSnr = LinkedHashMap<Pair<String, String>, Float>()
        val nodes = LinkedHashSet<String>()
        for ((observerRaw, list) in observedByNode) {
            val observer = resolveFullKey(observerRaw)
            nodes.add(observer)
            for (n in list) {
                val peer = resolveFullKey(n.prefixHex)
                nodes.add(peer)
                // observer heard peer -> direction is peer -> observer
                directedSnr[peer to observer] = n.snrDb
            }
        }

        val unorderedPairs = LinkedHashSet<Pair<String, String>>()
        for ((src, dst) in directedSnr.keys) {
            if (src == dst) continue
            val pair = if (src < dst) src to dst else dst to src
            unorderedPairs.add(pair)
        }

        return unorderedPairs.map { (a, b) ->
            val nameA = displayName(a)
            val nameB = displayName(b)
            val cA = coordsFor(a, nameA)
            val cB = coordsFor(b, nameB)
            val dist = if (cA != null && cB != null) distanceAndBearing(cA.first, cA.second, cB.first, cB.second)?.distanceKm else null
            MeshLinkEdge(
                nodeAKey = a,
                nodeAName = nameA,
                nodeBKey = b,
                nodeBName = nameB,
                snrAtoBDb = directedSnr[a to b],
                snrBtoADb = directedSnr[b to a],
                distanceKm = dist,
            )
        }.sortedWith(compareByDescending<MeshLinkEdge> { it.isAsymmetric }.thenBy { it.nodeAName.lowercase(Locale.ROOT) })
    }
}

// ---- 4. Live Link Ping / Walk-Test Statistics (#11) ----

data class PingSample(
    val seq: Int,
    val ok: Boolean,
    val rttMs: Long?,
    val detail: String,
)

data class PingSummary(
    val sent: Int,
    val received: Int,
    val successPercent: Int,
    val minRttMs: Long?,
    val avgRttMs: Long?,
    val maxRttMs: Long?,
)

object PingStatsCalculator {
    fun summarize(samples: List<PingSample>): PingSummary {
        if (samples.isEmpty()) return PingSummary(0, 0, 0, null, null, null)
        val okRtts = samples.filter { it.ok && it.rttMs != null }.mapNotNull { it.rttMs }
        val received = samples.count { it.ok }
        val pct = ((received * 100.0) / samples.size.toDouble()).roundToInt()
        return PingSummary(
            sent = samples.size,
            received = received,
            successPercent = pct,
            minRttMs = okRtts.minOrNull(),
            avgRttMs = if (okRtts.isNotEmpty()) okRtts.average().roundToInt().toLong() else null,
            maxRttMs = okRtts.maxOrNull(),
        )
    }
}

// ---- 5. Offline Delta Readiness Inspector (#12) ----

enum class DeltaReadinessStatus {
    READY,
    UP_TO_DATE,
    MISSING_BASE,
    MISSING_TARGET,
}

data class RepeaterDeltaReadiness(
    val deviceKey: String,
    val deviceName: String,
    val boardEnv: String,
    val currentVersion: String,
    val targetVersion: String,
    val status: DeltaReadinessStatus,
)

object OfflineDeltaInspector {
    /**
     * Checks each repeater known from backups against locally cached firmwares to verify whether
     * both the running base `.bin` and the newest target `.bin` for its board are cached offline.
     */
    fun inspect(
        backups: List<Backup>,
        cachedFirmwares: List<FirmwareFile>,
        isOtaCached: (FirmwareFile) -> Boolean,
    ): List<RepeaterDeltaReadiness> {
        val latestBackupByDevice = backups
            .filter { it.kind == DeviceKind.REPEATER && it.firmware.isNotBlank() }
            .groupBy { it.deviceKey.ifEmpty { it.deviceName } }
            .mapValues { (_, list) -> list.maxByOrNull { it.time }!! }

        val otaFirmwares = cachedFirmwares.filter { it.hasLoraOta && isOtaCached(it) }
        val out = ArrayList<RepeaterDeltaReadiness>()

        for ((devKey, b) in latestBackupByDevice) {
            // b.firmware is typically "<env>|<version>" or "<version> (Build: ...)"
            val envHint: String
            val verHint: String
            if ('|' in b.firmware) {
                val parts = b.firmware.split('|')
                envHint = parts.firstOrNull()?.trim().orEmpty()
                verHint = parts.getOrNull(1)?.trim() ?: b.firmware.trim()
            } else {
                // a backup keeps the OTA ident as "<version> <date> <commit> <env>" (its '|' made a space)
                val words = b.firmware.trim().split(Regex("\\s+")).filter { it.isNotEmpty() }
                val envWord = words.firstOrNull { w -> cachedFirmwares.any { it.env.equals(w, ignoreCase = true) } }
                    ?: words.lastOrNull().orEmpty()
                envHint = envWord
                verHint = words.filter { it != envWord }.joinToString(" ")
            }

            val boardMatches = otaFirmwares.filter { f ->
                (envHint.isNotEmpty() && f.env.equals(envHint, ignoreCase = true)) ||
                    (verHint.isNotEmpty() && (f.version == verHint || (f.commit.isNotEmpty() && f.commit in verHint)))
            }
            val boardEnv = boardMatches.firstOrNull()?.env ?: envHint
            if (boardEnv.isEmpty()) continue

            val forBoard = otaFirmwares.filter { it.env.equals(boardEnv, ignoreCase = true) }
            val newestTarget = forBoard.firstOrNull()
            val baseCached = forBoard.any { f ->
                f.version == verHint || (f.commit.isNotEmpty() && f.commit in verHint) || f.file.contains(verHint)
            }

            val status = when {
                newestTarget == null -> DeltaReadinessStatus.MISSING_TARGET
                baseCached && (newestTarget.version == verHint || (newestTarget.commit.isNotEmpty() && newestTarget.commit in verHint)) ->
                    DeltaReadinessStatus.UP_TO_DATE
                !baseCached -> DeltaReadinessStatus.MISSING_BASE
                else -> DeltaReadinessStatus.READY
            }
            out.add(
                RepeaterDeltaReadiness(
                    deviceKey = devKey,
                    deviceName = b.deviceName.ifEmpty { devKey.take(12) },
                    boardEnv = boardEnv,
                    currentVersion = verHint,
                    targetVersion = newestTarget?.version?.ifEmpty { newestTarget.release } ?: "—",
                    status = status,
                )
            )
        }
        return out.sortedBy { it.deviceName.lowercase(Locale.ROOT) }
    }
}

// ---- 6. Fleet Configuration & Security Audit (#13) ----

enum class AuditIssueKind {
    RADIO_MISMATCH,
    MISSING_COORDS,
    REPEAT_DISABLED,
    WEAK_GUEST_ACCESS,
    LOW_BATTERY,
}

data class FleetAuditFinding(
    val deviceName: String,
    val kind: AuditIssueKind,
    val detail: String,
)

object FleetAuditLogic {
    /**
     * Audits saved repeater backups and latest health records for configuration drift and security risks:
     * - Radio parameters differing from the fleet's majority `radio` setting
     * - Missing GPS coordinates (`lat = 0, lon = 0`)
     * - Packet repeating disabled (`repeat = off`)
     * - Guest read-only disabled (`allow.read.only = off`) with empty guest password
     * - Critically low battery (< 3550 mV or < 20%) in latest health check
     */
    fun audit(
        backups: List<Backup>,
        latestHealthByKey: Map<String, HealthStore.Record>,
        knownNames: Map<String, String>,
    ): List<FleetAuditFinding> {
        val findings = ArrayList<FleetAuditFinding>()
        val latestRepeaterBackups = backups
            .filter { it.kind == DeviceKind.REPEATER }
            .groupBy { it.deviceKey.ifEmpty { it.deviceName } }
            .mapValues { (_, list) -> list.maxByOrNull { it.time }!! }
            .values

        val radioCounts = LinkedHashMap<String, Int>()
        for (b in latestRepeaterBackups) {
            val r = b.items.firstOrNull { it.group == BackupItem.GROUP_SETTING && it.id == "radio" }?.value?.trim()
            if (!r.isNullOrEmpty()) radioCounts[r] = (radioCounts[r] ?: 0) + 1
        }
        val majorityRadio = radioCounts.maxByOrNull { it.value }?.takeIf { radioCounts.size > 1 }?.key

        for (b in latestRepeaterBackups) {
            val name = b.deviceName.ifEmpty { b.deviceKey.take(12) }
            val map = b.items.filter { it.group == BackupItem.GROUP_SETTING }.associate { it.id to it.value.trim() }

            val radio = map["radio"]
            if (majorityRadio != null && !radio.isNullOrEmpty() && radio != majorityRadio) {
                findings.add(FleetAuditFinding(name, AuditIssueKind.RADIO_MISMATCH, "$radio ≠ $majorityRadio"))
            }

            val lat = map["lat"]?.replace(',', '.')?.toDoubleOrNull()
            val lon = map["lon"]?.replace(',', '.')?.toDoubleOrNull()
            if (lat != null && lon != null && lat == 0.0 && lon == 0.0) {
                findings.add(FleetAuditFinding(name, AuditIssueKind.MISSING_COORDS, "lat=0, lon=0"))
            }

            val repeat = map["repeat"]
            if (repeat != null && (repeat.equals("off", ignoreCase = true) || repeat == "0")) {
                findings.add(FleetAuditFinding(name, AuditIssueKind.REPEAT_DISABLED, "repeat=$repeat"))
            }

            val allowRo = map["allow.read.only"]
            val guestPw = map["guest.password"]
            if (allowRo != null && (allowRo.equals("off", ignoreCase = true) || allowRo == "0") && guestPw.isNullOrEmpty()) {
                findings.add(FleetAuditFinding(name, AuditIssueKind.WEAK_GUEST_ACCESS, "allow.read.only=off"))
            }
        }

        for ((key, rec) in latestHealthByKey) {
            val parsed = io.github.n3tk0.loraota.ota.SelfTest.parse(rec.line) ?: continue
            val mv = parsed.batteryMv
            val pct = parsed.batteryPercent
            if ((mv != null && mv in 2500..3550) || (pct != null && pct < 20)) {
                val name = knownNames[key] ?: key.take(12)
                val desc = listOfNotNull(mv?.let { "${it}mV" }, pct?.let { "$it%" }).joinToString(" / ")
                findings.add(FleetAuditFinding(name, AuditIssueKind.LOW_BATTERY, desc))
            }
        }

        return findings.sortedBy { it.deviceName.lowercase(Locale.ROOT) }
    }
}

// ---- 7. Encrypted Full Workspace Archive Bundle (#14) ----

data class WorkspaceData(
    val backups: List<Backup>,
    val healthRecords: Map<String, List<HealthStore.Record>>,
    val passwords: Map<String, String>,
    val pinnedKeys: List<String>,
    val cliSnippets: List<String>,
    val profiles: List<ConfigProfile>,
    val deviceNames: Map<String, String>,
    val observedNeighbors: Map<String, List<NeighborEntry>> = emptyMap(),
)

object WorkspaceBundle {
    const val FORMAT = "loraota-workspace-v1"

    /**
     * Serializes the entire user workspace into a JSON bundle.
     * When [password] is non-empty, all secrets (repeater passwords, backup channels/keys) are encrypted
     * using `PasswordBox.seal` (PBKDF2-HMAC-SHA256 + AES-256-GCM). When [password] is empty, repeater
     * passwords and backup secrets are omitted.
     */
    fun export(data: WorkspaceData, password: String): String {
        val inner = JSONObject()
        inner.put("backups", JSONArray().apply {
            for (b in data.backups) {
                val clean = if (password.isEmpty()) {
                    Backup(
                        b.kind, b.deviceKey, b.deviceName, b.time, b.auto, b.firmware,
                        b.items.filter { it.group != BackupItem.GROUP_CHANNEL && it.id !in PasswordBox.SECRET_SETTINGS }
                    ).toJson()
                } else {
                    b.toJson().apply {
                        b.privateKey?.let { put("key", io.github.n3tk0.loraota.backup.b64Encode(it)) }
                    }
                }
                put(clean)
            }
        })

        val healthObj = JSONObject()
        for ((k, list) in data.healthRecords) {
            healthObj.put(k, JSONArray().apply {
                for (r in list) put(JSONObject().put("time", r.time).put("line", r.line))
            })
        }
        inner.put("health", healthObj)

        if (password.isNotEmpty()) {
            val pwObj = JSONObject()
            for ((k, v) in data.passwords) pwObj.put(k, v)
            inner.put("passwords", pwObj)
        }

        inner.put("pins", JSONArray().apply { for (p in data.pinnedKeys) put(p) })
        inner.put("snippets", JSONArray().apply { for (s in data.cliSnippets) if (!CliHelper.isSecretWriteCommand(s)) put(s) })
        inner.put("profiles", JSONArray().apply {
            for (prof in data.profiles) {
                val sObj = JSONObject()
                for ((k, v) in ConfigProfileLogic.extractTemplateSettings(prof.settings)) sObj.put(k, v)
                put(JSONObject().put("name", prof.name).put("settings", sObj))
            }
        })
        val namesObj = JSONObject()
        for ((k, v) in data.deviceNames) namesObj.put(k, v)
        inner.put("device_names", namesObj)

        val neighObj = JSONObject()
        for ((k, list) in data.observedNeighbors) {
            neighObj.put(k, JSONArray().apply {
                for (n in list) {
                    put(
                        JSONObject()
                            .put("hex", n.prefixHex)
                            .put("secs", n.secsAgo)
                            .put("snr", n.snrDb.toDouble())
                            .put("name", n.resolvedName ?: "")
                    )
                }
            })
        }
        inner.put("neighbors", neighObj)

        val outer = JSONObject()
            .put("format", FORMAT)
            .put("time", System.currentTimeMillis())
        return if (password.isEmpty()) {
            outer.put("workspace", inner).toString(2)
        } else {
            outer.put("enc", PasswordBox.seal(inner.toString().toByteArray(Charsets.UTF_8), password)).toString(2)
        }
    }

    fun needsPassword(text: String): Boolean = JSONObject(text).has("enc")

    fun import(text: String, password: String): WorkspaceData {
        val outer = JSONObject(text)
        require(outer.optString("format") == FORMAT) { "not a workspace archive" }
        val inner = if (outer.has("enc")) {
            if (password.isEmpty()) throw io.github.n3tk0.loraota.backup.WrongPassword()
            JSONObject(String(PasswordBox.open(outer.getJSONObject("enc"), password), Charsets.UTF_8))
        } else {
            outer.getJSONObject("workspace")
        }

        val backups = ArrayList<Backup>()
        inner.optJSONArray("backups")?.let { arr ->
            for (i in 0 until arr.length()) {
                val o = arr.getJSONObject(i)
                val pk = o.optString("key").takeIf { it.isNotEmpty() }?.let { io.github.n3tk0.loraota.backup.b64Decode(it) }
                backups.add(Backup.fromJson(o).with(pk))
            }
        }

        val health = LinkedHashMap<String, List<HealthStore.Record>>()
        inner.optJSONObject("health")?.let { hObj ->
            for (k in hObj.keys()) {
                val arr = hObj.optJSONArray(k) ?: continue
                val list = ArrayList<HealthStore.Record>(arr.length())
                for (i in 0 until arr.length()) {
                    val r = arr.getJSONObject(i)
                    list.add(HealthStore.Record(r.getLong("time"), r.getString("line")))
                }
                if (list.isNotEmpty()) health[k] = list
            }
        }

        val passwords = LinkedHashMap<String, String>()
        inner.optJSONObject("passwords")?.let { pObj ->
            for (k in pObj.keys()) {
                val v = pObj.optString(k)
                if (v.isNotEmpty()) passwords[k] = v
            }
        }

        val pins = ArrayList<String>()
        inner.optJSONArray("pins")?.let { arr ->
            for (i in 0 until arr.length()) arr.optString(i).takeIf { it.isNotEmpty() }?.let { pins.add(it) }
        }

        val snippets = ArrayList<String>()
        inner.optJSONArray("snippets")?.let { arr ->
            for (i in 0 until arr.length()) {
                arr.optString(i).takeIf { it.isNotBlank() && !CliHelper.isSecretWriteCommand(it) }?.let { snippets.add(it) }
            }
        }

        val profiles = ArrayList<ConfigProfile>()
        inner.optJSONArray("profiles")?.let { arr ->
            for (i in 0 until arr.length()) {
                val o = arr.getJSONObject(i)
                val name = o.optString("name").trim()
                val sObj = o.optJSONObject("settings")
                if (name.isNotEmpty() && sObj != null) {
                    val map = LinkedHashMap<String, String>()
                    for (k in sObj.keys()) map[k] = sObj.optString(k)
                    profiles.add(ConfigProfile(name, ConfigProfileLogic.extractTemplateSettings(map)))
                }
            }
        }

        val deviceNames = LinkedHashMap<String, String>()
        inner.optJSONObject("device_names")?.let { nObj ->
            for (k in nObj.keys()) {
                val v = nObj.optString(k)
                if (v.isNotBlank()) deviceNames[k] = v
            }
        }

        val neighbors = LinkedHashMap<String, List<NeighborEntry>>()
        inner.optJSONObject("neighbors")?.let { nObj ->
            for (k in nObj.keys()) {
                val arr = nObj.optJSONArray(k) ?: continue
                val list = ArrayList<NeighborEntry>(arr.length())
                for (i in 0 until arr.length()) {
                    val o = arr.getJSONObject(i)
                    list.add(
                        NeighborEntry(
                            prefixHex = o.optString("hex"),
                            secsAgo = o.optLong("secs", 0L),
                            snrDb = o.optDouble("snr", 0.0).toFloat(),
                            resolvedName = o.optString("name").takeIf { it.isNotBlank() },
                        )
                    )
                }
                if (list.isNotEmpty()) neighbors[k] = list
            }
        }

        return WorkspaceData(
            backups = backups,
            healthRecords = health,
            passwords = passwords,
            pinnedKeys = pins,
            cliSnippets = snippets,
            profiles = profiles,
            deviceNames = deviceNames,
            observedNeighbors = neighbors,
        )
    }
}
