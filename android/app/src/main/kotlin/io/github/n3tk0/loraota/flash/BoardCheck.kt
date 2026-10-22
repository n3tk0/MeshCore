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

    private fun words(s: String) = s.lowercase().split(Regex("[^a-z0-9]+")).filter { it.length >= 3 && it !in generic }

    /**
     * Does the build env name the board the device says it is (its Bluetooth DFU name like XIAO_NRF52_OTA,
     * or the companion's manufacturer name like "RAK WisMesh Tag")? null: nothing to compare.
     */
    fun matches(env: String, device: String): Boolean? {
        val d = words(device)
        if (d.isEmpty() || env.isEmpty()) return null
        val e = env.lowercase().replace(Regex("[^a-z0-9]"), "")
        // at least half the words: "RAK WisMesh Tag" is not a RAK_4631 build
        return d.count { it in e } * 2 >= d.size
    }
}
