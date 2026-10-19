plugins {
    id("org.jetbrains.kotlin.jvm")
}

java {
    sourceCompatibility = JavaVersion.VERSION_17
    targetCompatibility = JavaVersion.VERSION_17
}

kotlin {
    compilerOptions { jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17) }
}

dependencies {
    testImplementation(kotlin("test"))
}

tasks.test {
    useJUnitPlatform()
    maxHeapSize = "1g"
    // ./gradlew :delta:test -PfixtureDir=... writes the patch set used by the firmware's unit test
    project.findProperty("fixtureDir")?.let { systemProperty("fixtureDir", it.toString()) }
}

// desktop use: java -cp delta.jar io.github.n3tk0.loraota.delta.DeltaCliKt old.bin new.bin out.patch
tasks.jar {
    manifest { attributes("Main-Class" to "io.github.n3tk0.loraota.delta.DeltaCliKt") }
}
