package io.github.n3tk0.loraota.ota

/**
 * A repeater's telemetry (REQ_TYPE_GET_TELEMETRY_DATA): Cayenne LPP, channel 1 the repeater itself, the
 * next channels its sensors (see EnvironmentSensorManager). Any repeater answers it, also a stock one.
 */
class RepeaterTelemetry(val fields: List<Field>) {
    class Field(val channel: Int, val type: Int, val value: Double)

    /**
     * A sensor with a voltage and a current (an INA219 and the like) on the battery line: its voltage and
     * its current in amps, negative while charging; the sign is only known from our firmware (an analog
     * value next to the current), null otherwise.
     */
    class Battery(val volts: Double, val amps: Double, val signed: Boolean)

    fun battery(): Battery? {
        val ch = fields.filter { it.channel > SELF && it.type == VOLTAGE }.map { it.channel }
            .firstOrNull { c -> fields.any { it.channel == c && it.type == CURRENT } } ?: return null
        val volts = fields.first { it.channel == ch && it.type == VOLTAGE }.value
        val amps = fields.first { it.channel == ch && it.type == CURRENT }.value
        val signed = fields.firstOrNull { it.channel == ch && it.type == ANALOG_INPUT }?.value
        return Battery(volts, signed ?: amps, signed != null)
    }

    companion object {
        const val REQ_TYPE_GET_TELEMETRY_DATA = 0x03
        const val SELF = 1
        const val ANALOG_INPUT = 2
        const val VOLTAGE = 116
        const val CURRENT = 117

        // the inverse permission mask 0: everything the login allows
        fun request() = byteArrayOf(REQ_TYPE_GET_TELEMETRY_DATA.toByte(), 0, 0, 0, 0)

        // type -> (size, divisor, signed); a type not here ends the parse (its size is unknown)
        private val TYPES = mapOf(
            0 to Triple(1, 1.0, false), 1 to Triple(1, 1.0, false), 2 to Triple(2, 100.0, true), 3 to Triple(2, 100.0, true),
            100 to Triple(4, 1.0, false), 101 to Triple(2, 1.0, false), 102 to Triple(1, 1.0, false),
            103 to Triple(2, 10.0, true), 104 to Triple(1, 2.0, false), 113 to Triple(6, 1000.0, true),
            115 to Triple(2, 10.0, false), 116 to Triple(2, 100.0, false), 117 to Triple(2, 1000.0, false),
            118 to Triple(4, 1.0, false), 120 to Triple(1, 1.0, false), 121 to Triple(2, 1.0, true),
            125 to Triple(2, 1.0, false), 128 to Triple(2, 1.0, false), 130 to Triple(4, 1000.0, false),
            131 to Triple(4, 1000.0, false), 132 to Triple(2, 1.0, false), 133 to Triple(4, 1.0, false),
            134 to Triple(6, 100.0, true), 135 to Triple(3, 1.0, false), 136 to Triple(9, 1.0, true), 142 to Triple(1, 1.0, false),
        )

        /** The response after its tag. */
        fun parse(data: ByteArray): RepeaterTelemetry {
            val out = ArrayList<Field>()
            var i = 0
            while (i + 2 <= data.size) {
                val ch = data[i].toInt() and 0xff
                val type = data[i + 1].toInt() and 0xff
                val (size, div, signed) = TYPES[type] ?: break
                if (i + 2 + size > data.size) break
                // MSB first; multi-value types (axes, GPS) keep only their first value, unused here
                val n = if (size > 4) size / 3 else size
                var v = 0L
                for (k in 0 until n) v = (v shl 8) or (data[i + 2 + k].toLong() and 0xff)
                if (signed && v and (1L shl (n * 8 - 1)) != 0L) v -= 1L shl (n * 8)
                out.add(Field(ch, type, v / div))
                i += 2 + size
            }
            return RepeaterTelemetry(out)
        }
    }
}
