package io.github.n3tk0.loraota.flash

/** What a firmware is for, from its build env (e.g. Xiao_nrf52_repeater, Faketec_v5_companion_radio_ble). */
enum class Role { REPEATER, COMPANION, ROOM_SERVER, OTHER }

object BoardCheck {
    // words that say nothing about the board
    private val generic = setOf("seeed", "diy", "board", "repeater", "companion", "radio", "ble", "usb", "room",
        "server", "lite", "ota", "sensor", "modem", "kiss", "adadfu")

    fun role(env: String): Role {
        val e = env.lowercase()
        return when {
            "repeater" in e -> Role.REPEATER
            "companion" in e -> Role.COMPANION
            "room_server" in e -> Role.ROOM_SERVER
            else -> Role.OTHER
        }
    }

    /** A companion over Bluetooth needs a BLE build: a USB one would leave the phone with no way back in. */
    fun fitsCompanion(env: String) = role(env) == Role.COMPANION && "usb" !in env.lowercase()

    // chip names say the platform (checked on its own), not the board: an nRF52840 build fits many boards
    private val chips = setOf("esp32", "esp32s3", "esp32c3", "esp32c6", "nrf52", "nrf52840", "rp2040", "stm32", "stm32wl")

    // two letters count when one is a digit: ThinkNode M1 is not an M6
    private fun words(s: String) = s.lowercase().split(Regex("[^a-z0-9]+"))
        .filter { (it.length >= 3 || (it.length == 2 && it.any(Char::isDigit))) && it !in generic && it !in chips }

    /** [w] in [e], and a model number not inside a longer one (T1 is not a T114). */
    private fun has(e: String, w: String): Boolean {
        if (w.none(Char::isDigit)) return w in e
        var at = e.indexOf(w)
        while (at >= 0) {
            if (e.getOrNull(at + w.length)?.isDigit() != true) return true
            at = e.indexOf(w, at + 1)
        }
        return false
    }

    /**
     * Does the build env name the board the device says it is (its Bluetooth DFU name like XIAO_NRF52_OTA,
     * or the companion's manufacturer name like "RAK WisMesh Tag")? null: nothing to compare.
     */
    fun matches(env: String, device: String): Boolean? {
        val d = words(device)
        if (d.isEmpty() || env.isEmpty()) return null
        val e = env.lowercase().replace(Regex("[^a-z0-9]"), "")
        // a model number must be there (M1 is not M6), and at least half the words ("RAK WisMesh Tag" is not RAK_4631)
        if (d.any { w -> w.any(Char::isDigit) && !has(e, w) }) return false
        return d.count { has(e, it) } * 2 >= d.size
    }
}
