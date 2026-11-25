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
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.LocalView
import androidx.core.view.WindowCompat
import io.github.n3tk0.loraota.ui.AppScreen
import io.github.n3tk0.loraota.ui.AppViewModel

class MainActivity : ComponentActivity() {
    private val vm: AppViewModel by viewModels()

    private val permissions = registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { results ->
        if (results.values.any { it }) vm.onPermissionsGranted()
    }

    override fun attachBaseContext(base: Context) {
        super.attachBaseContext(Lang.wrap(base))
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        if (savedInstanceState == null) {
            val wanted = if (Build.VERSION.SDK_INT >= 33) {
                // notifications: the update's progress while the app is not on screen
                arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT, Manifest.permission.POST_NOTIFICATIONS)
            } else if (Build.VERSION.SDK_INT >= 31) {
                arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
            } else {
                arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
            }
            if (wanted.any { androidx.core.content.ContextCompat.checkSelfPermission(this, it) != android.content.pm.PackageManager.PERMISSION_GRANTED }) {
                permissions.launch(wanted)
            }
        }
        setContent {
            var themeMode by remember { mutableStateOf(AppPrefs.theme(this)) }
            var dynamicColor by remember { mutableStateOf(AppPrefs.dynamicColor(this)) }
            val darkTheme = when (themeMode) {
                ThemeMode.SYSTEM -> isSystemInDarkTheme()
                ThemeMode.LIGHT -> false
                ThemeMode.DARK -> true
            }
            val colorScheme = when {
                dynamicColor && Build.VERSION.SDK_INT >= Build.VERSION_CODES.S ->
                    if (darkTheme) dynamicDarkColorScheme(this) else dynamicLightColorScheme(this)
                darkTheme -> DarkColors
                else -> LightColors
            }
            val view = LocalView.current
            if (!view.isInEditMode) {
                SideEffect {
                    @Suppress("DEPRECATION")
                    window.statusBarColor = colorScheme.surface.toArgb()
                    @Suppress("DEPRECATION")
                    window.navigationBarColor = colorScheme.surface.toArgb()
                    val controller = WindowCompat.getInsetsController(window, view)
                    controller.isAppearanceLightStatusBars = !darkTheme
                    controller.isAppearanceLightNavigationBars = !darkTheme
                }
            }
            // the screen stays on only while an update, a flash, a build or a queue runs
            val keepOn = vm.keepScreenOn || (vm.busy && vm.queueCurrent != null)
            androidx.compose.runtime.DisposableEffect(keepOn) {
                if (keepOn) window.addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                else window.clearFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                onDispose { window.clearFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON) }
            }
            MaterialTheme(colorScheme = colorScheme) {
                AppScreen(
                    vm = vm,
                    themeMode = themeMode,
                    setThemeMode = { mode ->
                        themeMode = mode
                        AppPrefs.setTheme(this, mode)
                    },
                    dynamicColor = dynamicColor,
                    setDynamicColor = { on ->
                        dynamicColor = on
                        AppPrefs.setDynamicColor(this, on)
                    },
                    setLang = { lang ->
                        Lang.set(this, lang)
                        recreate()
                    },
                )
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
