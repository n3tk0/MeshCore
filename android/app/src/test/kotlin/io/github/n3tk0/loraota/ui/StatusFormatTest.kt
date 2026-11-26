package io.github.n3tk0.loraota.ui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class StatusFormatTest {
    @Test fun percentInsideRange() {
        // Li-ion 3.20..4.18: 3.69 V is half
        assertEquals(50, StatusFormat.percent(3.69, BatteryChem.LI_ION))
        assertEquals(100, StatusFormat.percent(4.18, BatteryChem.LI_ION))
        assertEquals(0, StatusFormat.percent(3.20, BatteryChem.LI_ION))
    }

    @Test fun percentClampedJustOutsideButHiddenFarOutside() {
        assertEquals(100, StatusFormat.percent(4.5, BatteryChem.LI_ION))
        assertEquals(0, StatusFormat.percent(2.9, BatteryChem.LI_ION))
        assertNull(StatusFormat.percent(0.0, BatteryChem.LI_ION))
        assertNull(StatusFormat.percent(5.0, BatteryChem.LI_ION))
    }

    @Test fun chemistryChangesPercent() {
        assertEquals(50, StatusFormat.percent(3.175, BatteryChem.LIFEPO4))
    }
}
