plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

android {
    namespace = "io.github.n3tk0.loraota"
    compileSdk = 35

    defaultConfig {
        applicationId = "io.github.n3tk0.loraota"
        minSdk = 26
        targetSdk = 35
        versionCode = 1
        versionName = "0.1.0"
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
    }
    testOptions {
        unitTests.isReturnDefaultValues = true
    }
}

dependencies {
    implementation(project(":delta"))
    implementation(platform("androidx.compose:compose-bom:2024.10.01"))
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.ui:ui")
    implementation("androidx.activity:activity-compose:1.9.3")
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.8.7")
    implementation("androidx.lifecycle:lifecycle-runtime-compose:2.8.7")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.9.0")
    implementation("no.nordicsemi.android:dfu:2.9.0")   // Bluetooth DFU of nRF52 boards (2.10+ needs compileSdk 36)
    testImplementation(kotlin("test"))
    testImplementation("junit:junit:4.13.2")
}
