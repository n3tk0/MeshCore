import java.security.SecureRandom

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

// The group's GitHub token for "New build" (Petko's choice, risk accepted 2026-10-22): from the environment,
// the secret LORAOTA_GITHUB_TOKEN in CI, only for the group APK (never the public release). XOR-ed with a key
// made for each build so it is not readable as text in the APK; anyone who takes the app apart can still get it.
val groupToken = System.getenv("LORAOTA_GITHUB_TOKEN")?.trim().orEmpty().toByteArray()
val groupKey = ByteArray(groupToken.size).also { SecureRandom().nextBytes(it) }
fun hexOf(b: ByteArray) = b.joinToString("") { "%02x".format(it) }

android {
    namespace = "io.github.n3tk0.loraota"
    compileSdk = 35

    defaultConfig {
        applicationId = "io.github.n3tk0.loraota"
        minSdk = 26
        targetSdk = 35
        versionCode = 1
        versionName = "0.1.0"
        buildConfigField("String", "GROUP_TOKEN_A", "\"${hexOf(ByteArray(groupToken.size) { (groupToken[it].toInt() xor groupKey[it].toInt()).toByte() })}\"")
        buildConfigField("String", "GROUP_TOKEN_B", "\"${hexOf(groupKey)}\"")
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
    testImplementation(kotlin("test"))
    testImplementation("junit:junit:4.13.2")
    testImplementation("org.json:json:20240303")   // the real one: the Android stubs return nothing in unit tests
}
