package io.github.n3tk0.loraota.ota

/**
 * A device's `bootloader` line (src/helpers/nrf52/BootloaderInfo.h): what INFO_UF2.TXT says, read
 * from the bootloader's flash, e.g.
 * "ours ver=0.9.2-OTAFIX2.3-BP1.4-mcota1 board=nRF52840-SeeedXiaoSense-v1 date=2026-10-22 sd=S140/6.1.1 feat=staged,rollback".
 */
class BootloaderInfo(val kind: Kind, val values: Map<String, String>) {
    enum class Kind { OURS, OTAFIX, ADAFRUIT, OTHER, NONE, NOT_NRF52 }

    val version: String? get() = values["ver"]
    val board: String? get() = values["board"]
    val date: String? get() = values["date"]
    val softDevice: String? get() = values["sd"]?.replace('/', ' ')
    val staged: Boolean get() = values["feat"]?.split(',')?.contains("staged") == true
    val rollback: Boolean get() = values["feat"]?.split(',')?.contains("rollback") == true

    companion object {
        const val COMMAND = "bootloader"

        fun parse(line: String): BootloaderInfo? {
            val words = line.trim().removePrefix("> ").trim().split(' ').filter { it.isNotEmpty() }
            if (words.isEmpty()) return null
            if (words[0] == "n/a") return BootloaderInfo(Kind.NOT_NRF52, emptyMap())
            val kind = when (words[0]) {
                "ours" -> Kind.OURS
                "otafix" -> Kind.OTAFIX
                "adafruit" -> Kind.ADAFRUIT
                "other" -> Kind.OTHER
                "none" -> Kind.NONE
                else -> return null   // an older firmware: "Unknown command"
            }
            val values = LinkedHashMap<String, String>()
            for (w in words.drop(1)) {
                val k = w.substringBefore('=', "")
                if (k.isNotEmpty()) values[k] = w.substringAfter('=')
            }
            return BootloaderInfo(kind, values)
        }
    }
}
