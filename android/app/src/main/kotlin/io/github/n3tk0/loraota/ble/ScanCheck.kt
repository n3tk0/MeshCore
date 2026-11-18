package io.github.n3tk0.loraota.ble

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.location.LocationManager
import android.os.Build
import androidx.core.content.ContextCompat
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

/**
 * Android 7-11: the scan needs the Location permission; without it Android 7-9 throws nothing and only
 * drops every result. 12+ uses 'Nearby devices' (BLUETOOTH_SCAN) instead.
 */
fun scanNeedsLocationPermission(ctx: Context): Boolean =
    Build.VERSION.SDK_INT < 31 &&
        ContextCompat.checkSelfPermission(ctx, Manifest.permission.ACCESS_FINE_LOCATION) != PackageManager.PERMISSION_GRANTED

/** The string that says which permission the scan lacks on this Android version. */
fun scanPermissionMessage(): Int =
    if (Build.VERSION.SDK_INT < 31) io.github.n3tk0.loraota.R.string.msg_no_location_permission
    else io.github.n3tk0.loraota.R.string.msg_no_bt_permission
