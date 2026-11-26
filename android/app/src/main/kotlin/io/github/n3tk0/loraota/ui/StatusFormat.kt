package io.github.n3tk0.loraota.ui

/** Pure helpers of the repeater status card (kept out of Compose so they can be unit tested). */
object StatusFormat {
    /** True when the voltage is plausible for the chemistry: the bar and the percent are shown only then. */
    fun inRange(volts: Double, chem: BatteryChem): Boolean =
        volts in (chem.emptyV - 0.5)..(chem.fullV + 0.5)

    /** Battery fraction 0..1 between the chemistry's empty and full voltage. */
    fun fraction(volts: Double, chem: BatteryChem): Float =
        ((volts - chem.emptyV) / (chem.fullV - chem.emptyV)).toFloat().coerceIn(0f, 1f)

    /** Battery percent, or null when the voltage is outside the range (no bar, no percent). */
    fun percent(volts: Double, chem: BatteryChem): Int? =
        if (inRange(volts, chem)) Math.round(fraction(volts, chem) * 100) else null
}
