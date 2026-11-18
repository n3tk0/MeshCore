package io.github.n3tk0.loraota.ble

import android.content.Context
import android.location.LocationManager
import android.os.Build
import androidx.core.location.LocationManagerCompat

/**
 * Android 7-11 (API 24-30): a Bluetooth scan finds nothing, with no error, while Location is off in the
 * phone's settings. 12+ scans with 'Nearby devices' and does not need it.
 */
fun scanNeedsLocation(ctx: Context): Boolean {
    if (Build.VERSION.SDK_INT >= 31) return false
    val lm = ctx.getSystemService(Context.LOCATION_SERVICE) as? LocationManager ?: return false
    return !LocationManagerCompat.isLocationEnabled(lm)
}
