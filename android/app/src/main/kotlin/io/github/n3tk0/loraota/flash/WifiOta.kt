package io.github.n3tk0.loraota.flash

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.net.wifi.WifiNetworkSpecifier
import android.os.Build
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import java.net.HttpURLConnection
import java.net.URL
import java.security.MessageDigest

/**
 * ESP32 over Wi-Fi: after `start ota` the repeater opens the open access point "MeshCore-OTA" with
 * AsyncElegantOTA at http://192.168.4.1/update (see src/helpers/ESP32Board.cpp, arch/esp32/AsyncElegantOTA).
 */
object WifiOta {
    const val SSID = "MeshCore-OTA"
    private const val URL_UPDATE = "http://192.168.4.1/update"

    /** The repeater's access point; Android 10+ asks the user once to join it. null: not found in time. */
    suspend fun join(ctx: Context, timeoutMs: Long): Pair<Network, () -> Unit>? {
        val cm = ctx.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
        if (Build.VERSION.SDK_INT < 29) {
            // older Android: the user joins it in the Wi-Fi settings; only its own subnet counts, not the home Wi-Fi
            @Suppress("DEPRECATION")
            val n = cm.allNetworks.firstOrNull {
                cm.getNetworkCapabilities(it)?.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) == true &&
                    cm.getLinkProperties(it)?.linkAddresses?.any { a -> a.address.hostAddress?.startsWith("192.168.4.") == true } == true
            }
            return n?.let { it to {} }
        }
        val got = CompletableDeferred<Network?>()
        val cb = object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(network: Network) { got.complete(network) }
            override fun onUnavailable() { got.complete(null) }
        }
        val request = NetworkRequest.Builder()
            .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
            .removeCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
            .setNetworkSpecifier(WifiNetworkSpecifier.Builder().setSsid(SSID).build())
            .build()
        cm.requestNetwork(request, cb)
        val release = { runCatching { cm.unregisterNetworkCallback(cb) }; Unit }
        val n = withTimeoutOrNull(timeoutMs) { got.await() }
        if (n == null) {
            release()
            return null
        }
        return n to release
    }

    /** Uploads the app image .bin (with its MD5, which AsyncElegantOTA wants); the repeater restarts with it. */
    suspend fun upload(network: Network, bin: ByteArray, progress: (Int) -> Unit) = withContext(Dispatchers.IO) {
        val md5 = MessageDigest.getInstance("MD5").digest(bin).joinToString("") { "%02x".format(it) }
        val boundary = "----LoraOta" + System.currentTimeMillis()
        val head = ("--$boundary\r\nContent-Disposition: form-data; name=\"MD5\"\r\n\r\n$md5\r\n" +
            "--$boundary\r\nContent-Disposition: form-data; name=\"firmware\"; filename=\"firmware.bin\"\r\n" +
            "Content-Type: application/octet-stream\r\n\r\n").toByteArray()
        val tail = "\r\n--$boundary--\r\n".toByteArray()
        val c = network.openConnection(URL(URL_UPDATE)) as HttpURLConnection
        try {
            c.requestMethod = "POST"
            c.doOutput = true
            c.connectTimeout = 15_000
            c.readTimeout = 120_000   // the repeater writes the flash before it answers
            c.setRequestProperty("Content-Type", "multipart/form-data; boundary=$boundary")
            c.setFixedLengthStreamingMode(head.size + bin.size + tail.size)
            c.outputStream.use { out ->
                out.write(head)
                var at = 0
                while (at < bin.size) {
                    val n = minOf(4096, bin.size - at)
                    out.write(bin, at, n)
                    at += n
                    progress(at * 100 / bin.size)
                }
                out.write(tail)
            }
            val code = c.responseCode
            val body = runCatching { (if (code in 200..299) c.inputStream else c.errorStream)?.use { String(it.readBytes()) } }.getOrNull() ?: ""
            if (code !in 200..299 || !body.contains("OK")) throw FlashFailed("HTTP $code ${body.trim()}")
        } finally {
            c.disconnect()
        }
    }
}
