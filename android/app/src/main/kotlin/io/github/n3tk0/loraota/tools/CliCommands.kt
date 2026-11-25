package io.github.n3tk0.loraota.tools

import java.util.Locale

/** A known CLI command: [template] as in docs/cli_commands.md (`<arg>` placeholders) and a short [hint]. */
data class CliCommandInfo(val template: String, val hint: String) {
    /** What tapping a suggestion puts in the input: the template up to its first `<placeholder>`, so the user types the value. */
    val fillText: String
        get() {
            val i = template.indexOf('<')
            return if (i < 0) template else template.substring(0, i)
        }
}

/** The command catalog (from docs/cli_commands.md) and prefix matching for the console's suggestions. */
object CliCommands {
    val REPEATER: List<CliCommandInfo> = listOf(
        CliCommandInfo("ver", "firmware version"),
        CliCommandInfo("board", "hardware name"),
        CliCommandInfo("clock", "show the time (UTC)"),
        CliCommandInfo("clock sync", "sync the clock with this phone"),
        CliCommandInfo("time <epoch_seconds>", "set the time"),
        CliCommandInfo("advert", "send a flood advert"),
        CliCommandInfo("advert.zerohop", "send a zero-hop advert"),
        CliCommandInfo("neighbors", "list nearby neighbors"),
        CliCommandInfo("neighbor.remove <pubkey_prefix>", "remove a neighbor"),
        CliCommandInfo("discover.neighbors", "discover zero-hop neighbors"),
        CliCommandInfo("stats-core", "battery, uptime, queue, debug flags"),
        CliCommandInfo("stats-radio", "noise floor, RSSI/SNR, airtime"),
        CliCommandInfo("stats-packets", "packets received / sent"),
        CliCommandInfo("clear stats", "reset the counters"),
        CliCommandInfo("power", "battery and power sources"),
        CliCommandInfo("battery", "charge, time left, mAh in/out"),
        CliCommandInfo("battery capacity <mAh>", "set the battery capacity (0 = learn)"),
        CliCommandInfo("battsave", "battery saving steps"),
        CliCommandInfo("battsave on", "turn battery saving on"),
        CliCommandInfo("battsave off", "turn battery saving off"),
        CliCommandInfo("battsave <low> <crit> <off>", "steps in mV"),
        CliCommandInfo("charger", "BQ25798 charger state"),
        CliCommandInfo("selftest", "self test: radio, i2c, qspi, battery"),
        CliCommandInfo("i2c", "I2C devices"),
        CliCommandInfo("sensor list", "list the sensors"),
        CliCommandInfo("ota status", "LoRa OTA status"),
        CliCommandInfo("ota sha", "SHA-256 of the running image"),
        CliCommandInfo("ota rollback", "go back to the previous image"),
        CliCommandInfo("set ota on", "turn LoRa OTA on"),
        CliCommandInfo("set ota off", "turn LoRa OTA off"),
        CliCommandInfo("start ota", "start an OTA update"),
        CliCommandInfo("get radio", "freq, bw, sf, cr"),
        CliCommandInfo("set radio <freq>,<bw>,<sf>,<cr>", "set the radio parameters"),
        CliCommandInfo("tempradio <freq>,<bw>,<sf>,<cr>,<mins>", "radio parameters for a set time"),
        CliCommandInfo("get tx", "transmit power"),
        CliCommandInfo("set tx <dBm>", "set the transmit power (1-22)"),
        CliCommandInfo("get freq", "frequency"),
        CliCommandInfo("set freq <MHz>", "set the frequency"),
        CliCommandInfo("get name", "node name"),
        CliCommandInfo("set name <name>", "set the node name"),
        CliCommandInfo("get lat", "latitude"),
        CliCommandInfo("set lat <degrees>", "set the latitude"),
        CliCommandInfo("get lon", "longitude"),
        CliCommandInfo("set lon <degrees>", "set the longitude"),
        CliCommandInfo("get owner.info", "owner info"),
        CliCommandInfo("set owner.info <text>", "set the owner info"),
        CliCommandInfo("get role", "configured role"),
        CliCommandInfo("get public.key", "public key"),
        CliCommandInfo("get repeat", "repeat flag"),
        CliCommandInfo("set repeat <on|off>", "forward packets or not"),
        CliCommandInfo("get path.hash.mode", "advert path hash size"),
        CliCommandInfo("set path.hash.mode <0-2>", "set the path hash size"),
        CliCommandInfo("get loop.detect", "loop detection"),
        CliCommandInfo("set loop.detect <state>", "set loop detection"),
        CliCommandInfo("get txdelay", "flood retransmit delay factor"),
        CliCommandInfo("set txdelay <0-2>", "set the flood delay factor"),
        CliCommandInfo("get direct.txdelay", "direct retransmit delay factor"),
        CliCommandInfo("set direct.txdelay <0-2>", "set the direct delay factor"),
        CliCommandInfo("get rxdelay", "receive delay base"),
        CliCommandInfo("set rxdelay <0-20>", "set the receive delay"),
        CliCommandInfo("get dutycycle", "duty cycle limit"),
        CliCommandInfo("set dutycycle <1-100>", "set the duty cycle (%)"),
        CliCommandInfo("get af", "airtime factor"),
        CliCommandInfo("set af <0-9>", "set the airtime factor"),
        CliCommandInfo("get int.thresh", "interference threshold"),
        CliCommandInfo("set int.thresh <value>", "set the interference threshold"),
        CliCommandInfo("get cad", "channel activity detection"),
        CliCommandInfo("set cad <on|off>", "turn CAD on or off"),
        CliCommandInfo("get agc.reset.interval", "AGC reset interval"),
        CliCommandInfo("get multi.acks", "multi-acks"),
        CliCommandInfo("get flood.advert.interval", "flood advert interval (hours)"),
        CliCommandInfo("set flood.advert.interval <hours>", "set the flood advert interval"),
        CliCommandInfo("get advert.interval", "zero-hop advert interval (minutes)"),
        CliCommandInfo("set advert.interval <minutes>", "set the zero-hop advert interval"),
        CliCommandInfo("get flood.max", "max hops for a flood"),
        CliCommandInfo("set flood.max <value>", "set the max hops for a flood"),
        CliCommandInfo("get adc.multiplier", "battery reading multiplier"),
        CliCommandInfo("set adc.multiplier <value>", "fine-tune the battery reading"),
        CliCommandInfo("powersaving", "power saving flag"),
        CliCommandInfo("powersaving on", "turn power saving on"),
        CliCommandInfo("powersaving off", "turn power saving off"),
        CliCommandInfo("password <new_password>", "change the admin password"),
        CliCommandInfo("get guest.password", "guest password"),
        CliCommandInfo("set guest.password <password>", "set the guest password"),
        CliCommandInfo("get acl", "show the access list"),
        CliCommandInfo("setperm <pubkey> <permissions>", "change a companion's permissions"),
        CliCommandInfo("region", "show the regions"),
        CliCommandInfo("region save", "save region changes"),
        CliCommandInfo("region get <name>", "info for a region"),
        CliCommandInfo("region put <name>", "create a region"),
        CliCommandInfo("region remove <name>", "remove a region"),
        CliCommandInfo("region allowf <name>", "allow flooding in a region"),
        CliCommandInfo("region denyf <name>", "block flooding in a region"),
        CliCommandInfo("gps", "GPS state"),
        CliCommandInfo("gps sync", "sync the clock with GPS"),
        CliCommandInfo("log start", "start capturing the rx log"),
        CliCommandInfo("log stop", "stop capturing"),
        CliCommandInfo("log erase", "erase the captured log"),
        CliCommandInfo("log", "print the captured log"),
        CliCommandInfo("reboot", "restart the node"),
        CliCommandInfo("clkreboot", "reset the clock and restart"),
        CliCommandInfo("poweroff", "power the node off"),
        CliCommandInfo("erase", "factory reset (erases the file system)"),
    )

    val COMPANION: List<CliCommandInfo> = listOf(
        CliCommandInfo("ver", "firmware version"),
        CliCommandInfo("board", "hardware name"),
        CliCommandInfo("get name", "node name"),
        CliCommandInfo("set name <name>", "set the node name"),
        CliCommandInfo("get tz.offset", "time zone offset"),
        CliCommandInfo("set tz.offset <hours>", "set the time zone offset"),
        CliCommandInfo("get wifi.status", "Wi-Fi status"),
        CliCommandInfo("get wifi.ip", "Wi-Fi IP address"),
        CliCommandInfo("get wifi.ssid", "Wi-Fi network"),
        CliCommandInfo("set wifi.ssid <ssid>", "set the Wi-Fi network"),
        CliCommandInfo("get wifi.enabled", "is Wi-Fi on"),
        CliCommandInfo("set wifi.enabled <on|off>", "turn Wi-Fi on or off"),
        CliCommandInfo("tempradio off", "back to the saved radio"),
        CliCommandInfo("reboot", "restart the node"),
    )

    /**
     * Up to [limit] commands whose template starts with what was typed (case-insensitive, ignoring leading spaces).
     * Nothing for an empty input, and nothing once the input is already the exact command or has moved on to its arguments.
     */
    fun match(input: String, isCompanion: Boolean, limit: Int = 5): List<CliCommandInfo> {
        val typed = input.trimStart().lowercase(Locale.ROOT)
        if (typed.isEmpty()) return emptyList()
        val list = if (isCompanion) COMPANION else REPEATER
        return list.filter {
            val t = it.template.lowercase(Locale.ROOT)
            t.startsWith(typed) && t != typed
        }.take(limit)
    }
}

/** The console's command history: newest first, at most [MAX], never a command that carries a secret. */
object CliHistory {
    const val MAX = 50

    /** Adds [cmd] at the front; false when it is blank, holds a password/key or repeats the newest entry. */
    fun add(history: MutableList<String>, cmd: String): Boolean {
        val c = cmd.trim()
        if (c.isEmpty() || CliHelper.isSecretWriteCommand(c)) return false
        if (history.firstOrNull() == c) return false
        history.add(0, c)
        while (history.size > MAX) history.removeAt(history.lastIndex)
        return true
    }

    /** Up arrow: the next older entry. [idx] -1 is "not browsing"; null when there is no history. */
    fun older(size: Int, idx: Int): Int? = if (size == 0) null else (idx + 1).coerceAtMost(size - 1)

    /** Down arrow: the next newer entry, or -1 (back to an empty input) after the newest. */
    fun newer(idx: Int): Int = (idx - 1).coerceAtLeast(-1)
}
