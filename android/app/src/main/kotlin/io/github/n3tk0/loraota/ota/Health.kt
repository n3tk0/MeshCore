package io.github.n3tk0.loraota.ota

import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/**
 * A repeater's `selftest` line (examples/simple_repeater/MyMesh.cpp, formatSelfTest): "ok" or
 * "WARN radio,i2c", then key=value pairs, e.g.
 * "ok radio=ok rx=120 tx=40 nf=-112 i2c=ok qspi=ok batt=3987mV/72% left=41h day=+420/-310 mail=3 ota=on ps=normal up=3d04h".
 */
class SelfTest(val ok: Boolean, val problems: List<String>, val values: Map<String, String>) {
    val batteryMv: Int? get() = values["batt"]?.substringBefore("mV")?.toIntOrNull()
    val batteryPercent: Int? get() = values["batt"]?.substringAfter('/', "")?.removeSuffix("%")?.toIntOrNull()
    /** Hours left at the average discharge; -2 charging, null not known. */
    val hoursLeft: Int? get() = values["left"]?.let { if (it == "chg") -2 else it.removeSuffix("h").toIntOrNull() }
    val mailbox: Int? get() = values["mail"]?.toIntOrNull()
    val i2c: String? get() = values["i2c"]
    val powerStep: String? get() = values["ps"]

    companion object {
        const val COMMAND = "selftest"

        fun parse(line: String): SelfTest? {
            val words = line.trim().split(' ').filter { it.isNotEmpty() }
            if (words.isEmpty()) return null
            val ok = words[0] == "ok"
            if (!ok && words[0] != "WARN") return null   // an older repeater: "unknown command"
            var rest = words.drop(1)
            val problems = if (!ok && rest.isNotEmpty() && '=' !in rest[0]) {
                val p = rest[0].split(',').filter { it.isNotEmpty() }
                rest = rest.drop(1)
                p
            } else emptyList()
            val values = LinkedHashMap<String, String>()
            for (w in rest) {
                val k = w.substringBefore('=', "")
                if (k.isNotEmpty()) values[k] = w.substringAfter('=')
            }
            return SelfTest(ok, problems, values)
        }
    }
}

/** What the Health page warns about, beyond the repeater's own "WARN" list. */
enum class HealthWarning { RADIO, I2C_OFF, I2C_STUCK, QSPI, BATTERY_STEP, NEW_FIRMWARE, I2C_NEW_OFF, FAST_DRAIN, FRESH_FIRMWARE }

/** How a warning is shown: red (the repeater cannot do its job), amber (degraded, it retries), neutral. */
enum class HealthSeverity { DANGER, WARN, INFO }

/** One card line per problem; [isNew]: it was not there at the check before. */
class HealthProblem(val warning: HealthWarning, val severity: HealthSeverity, val isNew: Boolean = false)

object HealthCheck {
    /** The warnings as the page shows them: one per problem (a sensor off and newly off are one), worst first. */
    fun problems(w: List<HealthWarning>): List<HealthProblem> = w.mapNotNull {
        when (it) {
            HealthWarning.RADIO, HealthWarning.I2C_STUCK, HealthWarning.BATTERY_STEP, HealthWarning.FAST_DRAIN ->
                HealthProblem(it, HealthSeverity.DANGER)
            HealthWarning.I2C_OFF -> HealthProblem(it, HealthSeverity.WARN, isNew = HealthWarning.I2C_NEW_OFF in w)
            HealthWarning.QSPI -> HealthProblem(it, HealthSeverity.WARN)
            HealthWarning.NEW_FIRMWARE, HealthWarning.FRESH_FIRMWARE -> HealthProblem(it, HealthSeverity.INFO)
            HealthWarning.I2C_NEW_OFF -> null
        }
    }.sortedBy { it.severity }


    /** Warnings from a self test, and from what changed since the one before (null: none kept). */
    fun warnings(now: SelfTest, before: SelfTest?): List<HealthWarning> {
        val w = ArrayList<HealthWarning>()
        if (now.values["radio"] == "bad") w.add(HealthWarning.RADIO)
        val i2c = now.i2c ?: "ok"
        if (i2c == "stuck") w.add(HealthWarning.I2C_STUCK)
        else if (i2c.startsWith("off")) {
            w.add(HealthWarning.I2C_OFF)
            if (before != null && !(before.i2c ?: "ok").startsWith("off")) w.add(HealthWarning.I2C_NEW_OFF)
        }
        if (now.values["qspi"] == "bad") w.add(HealthWarning.QSPI)
        if ((now.powerStep ?: "normal") != "normal") w.add(HealthWarning.BATTERY_STEP)
        if (now.values["ota"]?.endsWith(",new") == true) w.add(HealthWarning.NEW_FIRMWARE)
        // confirmed in this boot, by the login or a console command just before the self test
        if (now.values["ota"]?.endsWith(",fresh") == true) w.add(HealthWarning.FRESH_FIRMWARE)
        return w
    }

    /**
     * The battery falls faster than usual: over the last 6 hours (day ring, 10-minute samples of %)
     * at least twice its average fall over the week (hourly samples), and by more than 1 % an hour.
     * Charging stretches (a rise) are left out of both.
     */
    fun fasterDrain(day: List<Int?>, week: List<Int?>): Boolean {
        val recent = fallPerHour(day.takeLast(36), 6.0) ?: return false
        val usual = fallPerHour(week, 1.0) ?: return false
        return recent > 1.0 && recent > 2 * usual.coerceAtLeast(0.1)
    }

    /** The average fall in % per hour over the samples' falling steps; null with too few samples. */
    fun fallPerHour(values: List<Int?>, samplesPerHour: Double): Double? {
        var fall = 0.0
        var steps = 0
        var prev: Int? = null
        for (v in values) {
            if (v != null && prev != null) {
                if (v <= prev) { fall += (prev - v); steps++ }   // a rise is a charger: not counted
            }
            prev = v
        }
        if (steps < 6) return null
        return fall / steps * samplesPerHour
    }
}

/**
 * One value over time from a node's telemetry history (src/helpers/TelemetryHistory.h): the day ring
 * (10-minute samples) or the week ring (hourly). Asked for in pieces: a LoRa packet holds ~64 values.
 */
class HistorySeries(val ring: Int, val field: Int, val lastTime: Long, val intervalMinutes: Int, val values: List<Int?>,
                    /** Bit f: field f has values somewhere in this ring; null from a node that does not say. */
                    val fields: Int? = null) {
    /** The time of sample i, in seconds; null when the node had no clock. */
    fun timeOf(i: Int): Long? = if (lastTime == 0L) null else lastTime - (values.size - 1 - i).toLong() * intervalMinutes * 60

    companion object {
        const val REQ_TYPE = 0x41     // repeater request type (REQ_TYPE_GET_TELEMETRY_HISTORY)
        const val RING_DAY = 0
        const val RING_WEEK = 1
        const val NONE = -32768

        const val F_BATT_MV = 0
        const val F_BATT_PCT = 1
        const val F_TEMP_DC = 2
        const val F_HUMIDITY = 3
        const val F_PRESSURE = 4
        const val F_CURRENT_MA = 5
        const val F_NOISE_DBM = 6
        const val F_RX = 7
        const val F_TX = 8
        const val F_AIR_TX_S = 9

        /** The order the page offers them in. */
        val ORDER = listOf(F_BATT_PCT, F_BATT_MV, F_CURRENT_MA, F_TEMP_DC, F_HUMIDITY, F_PRESSURE, F_NOISE_DBM, F_RX, F_TX, F_AIR_TX_S)

        /** The field to show: [wanted] when it has values (or nothing says), else the first that has. */
        fun pick(wanted: Int, fields: Int?): Int =
            if (fields == null || fields == 0 || fields and (1 shl wanted) != 0) wanted
            else ORDER.firstOrNull { fields and (1 shl it) != 0 } ?: wanted

        /** ring, field, start (from the oldest), how many: the bytes after the request type. */
        fun args(ring: Int, field: Int, start: Int, max: Int) =
            byteArrayOf(ring.toByte(), field.toByte(), (start and 0xff).toByte(), (start shr 8).toByte(), max.toByte())

        /** A repeater request (the companion puts its tag in front). */
        fun request(ring: Int, field: Int, start: Int, max: Int) =
            byteArrayOf(REQ_TYPE.toByte()) + args(ring, field, start, max)

        class Piece(val ring: Int, val field: Int, val total: Int, val start: Int, val lastTime: Long, val interval: Int,
                    val values: List<Int?>, val fields: Int?)

        /**
         * ring(1) field(1) total(2) start(2) n(1) last_time(4) interval(1) values(n x int16) fields(2), all
         * little endian; fields (which fields have values in the ring) came later, so it may be missing.
         */
        fun parsePiece(d: ByteArray): Piece? {
            if (d.size < 12) return null
            fun u8(i: Int) = d[i].toInt() and 0xff
            val n = u8(6)
            if (d.size < 12 + 2 * n) return null
            val t = (u8(7).toLong()) or (u8(8).toLong() shl 8) or (u8(9).toLong() shl 16) or (u8(10).toLong() shl 24)
            val values = (0 until n).map {
                val v = (u8(12 + 2 * it) or (u8(13 + 2 * it) shl 8)).toShort().toInt()
                if (v == NONE) null else v
            }
            val end = 12 + 2 * n
            val fields = if (d.size >= end + 2) u8(end) or (u8(end + 1) shl 8) else null
            return Piece(u8(0), u8(1), u8(2) or (u8(3) shl 8), u8(4) or (u8(5) shl 8), t, u8(11), values, fields)
        }

        /** All of one ring's values of one field, piece by piece; null when the node does not answer. */
        suspend fun load(ring: Int, field: Int, fetch: suspend (start: Int, max: Int) -> ByteArray?): HistorySeries? {
            val out = ArrayList<Int?>()
            var start = 0
            var lastTime = 0L
            var interval = if (ring == RING_DAY) 10 else 60
            var fields: Int? = null
            while (true) {
                // no answer (or a bad one) later on: what came so far is still worth a graph
                val p = fetch(start, 64)?.let { parsePiece(it) }
                    ?: return if (start == 0) null else HistorySeries(ring, field, lastTime, interval, out, fields)
                if (start == 0) { lastTime = p.lastTime; interval = p.interval; fields = p.fields }
                // the node has nothing for this field (it sends only the header): no more pieces
                if (start == 0 && p.fields != null && p.fields != 0 && p.fields and (1 shl field) == 0) {
                    return HistorySeries(ring, field, lastTime, interval, List(p.total) { null }, fields)
                }
                out.addAll(p.values)
                start += p.values.size
                if (p.values.isEmpty() || start >= p.total) break
            }
            return HistorySeries(ring, field, lastTime, interval, out, fields)
        }

        /** The value as shown: e.g. 0.1 degC -> degC, pressure from its 900 hPa offset. */
        fun display(field: Int, v: Int): Double = when (field) {
            F_BATT_MV -> v / 1000.0
            F_TEMP_DC -> v / 10.0
            F_PRESSURE -> 900 + v / 10.0
            else -> v.toDouble()
        }
    }
}

/** Self tests kept on the phone, per device, newest last: the Health page's history and "since last time". */
class HealthStore(dir: File) {
    private val file = File(dir, "health.json")

    class Record(val time: Long, val line: String)

    private fun readAll(): JSONObject = runCatching { JSONObject(file.readText()) }.getOrDefault(JSONObject())

    fun records(device: String): List<Record> {
        val a = readAll().optJSONArray(device) ?: return emptyList()
        return (0 until a.length()).map { val o = a.getJSONObject(it); Record(o.getLong("t"), o.getString("l")) }
    }

    fun add(device: String, line: String, time: Long = System.currentTimeMillis()) {
        val all = readAll()
        val a = all.optJSONArray(device) ?: JSONArray()
        a.put(JSONObject().put("t", time).put("l", line))
        val keep = JSONArray()
        for (i in maxOf(0, a.length() - MAX) until a.length()) keep.put(a.get(i))
        all.put(device, keep)
        file.writeText(all.toString())
    }

    companion object {
        const val MAX = 200
    }
}

/**
 * A repeater's `battsave` reply (examples/simple_repeater/MyMesh.cpp, handlePowerSaveCommand):
 * "on, step normal, 3987 mV ext; low 3600 crit 3450 off 0". Not upstream's `powersaving`.
 */
class BattSave(val enabled: Boolean, val step: String, val mv: Int, val external: Boolean, val low: Int, val crit: Int, val off: Int) {
    fun matches(r: Triple<Int, Int, Int>) = enabled && low == r.first && crit == r.second && off == r.third

    companion object {
        const val COMMAND = "battsave"
        private val RE = Regex("^(on|off), step (\\w+), (\\d+) mV( ext)?; low (\\d+) crit (\\d+) off (\\d+)")

        fun parse(line: String): BattSave? {
            val m = RE.find(line.trim().removePrefix("> ")) ?: return null
            val g = m.groupValues
            return BattSave(g[1] == "on", g[2], g[3].toInt(), g[4].isNotEmpty(), g[5].toInt(), g[6].toInt(), g[7].toInt())
        }

        /**
         * For one Li-ion / LiPo cell: 'low' 3.6 V and 'critical' 3.45 V; 'off' 3.3 V only once the reading was
         * checked (a meter, or an INA219), since a wrong one would take the radio away with no way back from afar.
         */
        fun recommended(checked: Boolean) = Triple(3600, 3450, if (checked) 3300 else 0)

        fun command(r: Triple<Int, Int, Int>) = "$COMMAND ${r.first} ${r.second} ${r.third}"
    }
}

/** The `battery capacity` reply: "> 2000 mAh (learnt)", "> 0 mAh (not known yet)"; "Err ..." without an INA219. */
class BattCapacity(val mah: Int, val learnt: Boolean) {
    companion object {
        const val COMMAND = "battery capacity"

        /** null: no current sensor (or not understood). */
        fun parse(line: String): BattCapacity? {
            val m = Regex("^(\\d+) mAh").find(line.trim().removePrefix("> ")) ?: return null
            return BattCapacity(m.groupValues[1].toInt(), line.contains("(learnt)"))
        }
    }
}
