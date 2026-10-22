package io.github.n3tk0.loraota

import android.Manifest
import android.content.Context
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.ui.graphics.Color
import io.github.n3tk0.loraota.ui.AppScreen
import io.github.n3tk0.loraota.ui.AppViewModel

class MainActivity : ComponentActivity() {
    private val vm: AppViewModel by viewModels()

    private val permissions = registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { }

    override fun attachBaseContext(base: Context) {
        super.attachBaseContext(Lang.wrap(base))
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        permissions.launch(
            if (Build.VERSION.SDK_INT >= 33) {
                // notifications: the update's progress while the app is not on screen
                arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT, Manifest.permission.POST_NOTIFICATIONS)
            } else if (Build.VERSION.SDK_INT >= 31) {
                arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
            } else {
                arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
            }
        )
        setContent {
            MaterialTheme(colorScheme = if (isSystemInDarkTheme()) DarkColors else LightColors) {
                AppScreen(vm) { lang ->
                    Lang.set(this, lang)
                    recreate()
                }
            }
        }
    }
}

// the green of the launcher icon (#1B5E20)
val LightColors = lightColorScheme(
    primary = Color(0xFF2E7D32), onPrimary = Color.White,
    primaryContainer = Color(0xFFB8F0B9), onPrimaryContainer = Color(0xFF002106),
    secondary = Color(0xFF52634F), secondaryContainer = Color(0xFFD5E8CF), onSecondaryContainer = Color(0xFF101F10),
    tertiary = Color(0xFF38656A), tertiaryContainer = Color(0xFFBCEBF0), onTertiaryContainer = Color(0xFF002023),
)

val DarkColors = darkColorScheme(
    primary = Color(0xFF8BD68D), onPrimary = Color(0xFF00390F),
    primaryContainer = Color(0xFF1B5E20), onPrimaryContainer = Color(0xFFA6F2A7),
    secondary = Color(0xFFB9CCB4), secondaryContainer = Color(0xFF3B4B39), onSecondaryContainer = Color(0xFFD5E8CF),
    tertiary = Color(0xFFA0CFD4), tertiaryContainer = Color(0xFF1F4D52), onTertiaryContainer = Color(0xFFBCEBF0),
)
