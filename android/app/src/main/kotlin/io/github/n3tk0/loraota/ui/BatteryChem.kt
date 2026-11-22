package io.github.n3tk0.loraota.ui

/** The repeater's battery, for the status card's bar: empty, low (red) and full in volts at rest. */
enum class BatteryChem(val label: String, val emptyV: Double, val lowV: Double, val fullV: Double) {
    LI_ION("Li-ion", 3.20, 3.55, 4.18),
    LIFEPO4("LiFePO4", 2.90, 3.15, 3.45),
    LTO_2S("LTO 2S", 3.80, 4.10, 5.30);

    companion object {
        fun byName(n: String?) = entries.firstOrNull { it.name == n } ?: LI_ION
    }
}
