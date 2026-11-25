import java.security.SecureRandom

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
    id("app.cash.paparazzi")
}

// The group's GitHub token for "New build" (Petko's choice, risk accepted 2026-10-22): from the environment,
// the secret LORAOTA_GITHUB_TOKEN in CI, only for the group APK (never the public release). XOR-ed with a key
// made for each build so it is not readable as text in the APK; anyone who takes the app apart can still get it.
val groupToken = System.getenv("LORAOTA_GITHUB_TOKEN")?.trim().orEmpty().toByteArray()
val groupKey = ByteArray(groupToken.size).also { SecureRandom().nextBytes(it) }
fun hexOf(b: ByteArray) = b.joinToString("") { "%02x".format(it) }

val appVersionName = System.getenv("LORAOTA_VERSION_NAME")?.trim().takeUnless { it.isNullOrEmpty() }
    ?: (project.findProperty("appVersionName") as? String) ?: "0.2.0"
val appVersionCode = System.getenv("LORAOTA_VERSION_CODE")?.trim()?.toIntOrNull()
    ?: (project.findProperty("appVersionCode") as? String)?.toIntOrNull() ?: 2

android {
    namespace = "io.github.n3tk0.loraota"
    compileSdk = 35

    defaultConfig {
        applicationId = "io.github.n3tk0.loraota"
        minSdk = 24
        targetSdk = 35
        versionCode = appVersionCode
        versionName = appVersionName
        buildConfigField("String", "GROUP_TOKEN_A", "\"${hexOf(ByteArray(groupToken.size) { (groupToken[it].toInt() xor groupKey[it].toInt()).toByte() })}\"")
        buildConfigField("String", "GROUP_TOKEN_B", "\"${hexOf(groupKey)}\"")
        buildConfigField("boolean", "IS_GROUP_APK", "${groupToken.isNotEmpty()}")
        // set by CI for builds published as a GitHub pre-release (branches other than main, -rc tags)
        buildConfigField("boolean", "IS_PRERELEASE", "${System.getenv("LORAOTA_PRERELEASE")?.trim() == "true"}")
    }

    // the release key: from the environment (GitHub Secrets in CI), so every version installs over the last
    val keystore = System.getenv("LORAOTA_KEYSTORE")?.let { file(it) }?.takeIf { it.exists() }
    signingConfigs {
        if (keystore != null) {
            create("release") {
                storeFile = keystore
                storePassword = System.getenv("LORAOTA_KEYSTORE_PASSWORD")
                keyAlias = System.getenv("LORAOTA_KEY_ALIAS")
                keyPassword = System.getenv("LORAOTA_KEY_PASSWORD")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            // no key given (a local build, a fork): the debug key, so it still installs
            signingConfig = signingConfigs.getByName(if (keystore != null) "release" else "debug")
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
        // java.time / java.util.Base64 on Android 7 (API 24-25)
        isCoreLibraryDesugaringEnabled = true
    }
    kotlinOptions {
        jvmTarget = "17"
    }
    buildFeatures {
        compose = true
        buildConfig = true
    }
    testOptions {
        unitTests.isReturnDefaultValues = true
    }
}

// The screenshot tests (Paparazzi, see tools/android-screens.sh) run only with the record/verify tasks,
// not with the plain unit tests.
val paparazziRun = gradle.startParameter.taskNames.any { it.contains("paparazzi", ignoreCase = true) }
tasks.withType<Test>().configureEach {
    if (!paparazziRun) filter.excludeTestsMatching("io.github.n3tk0.loraota.screens.*")
}

dependencies {
    implementation(project(":delta"))
    implementation(platform("androidx.compose:compose-bom:2024.10.01"))
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.material:material-icons-core")
    implementation("androidx.compose.ui:ui")
    implementation("androidx.activity:activity-compose:1.9.3")
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.8.7")
    implementation("androidx.lifecycle:lifecycle-runtime-compose:2.8.7")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.9.0")
    implementation("no.nordicsemi.android:dfu:2.9.0")   // Bluetooth DFU of nRF52 boards (2.10+ needs compileSdk 36)
    implementation("org.osmdroid:osmdroid-android:6.1.20")   // network map; own tile cache, no API key
    coreLibraryDesugaring("com.android.tools:desugar_jdk_libs:2.1.3")
    testImplementation(kotlin("test"))
    testImplementation("junit:junit:4.13.2")
    testImplementation("org.json:json:20240303")   // the real one: the Android stubs return nothing in unit tests
}
