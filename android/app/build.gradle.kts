// Project Alice Android app.
//
// The native code is NOT compiled by Gradle: libAlice.so comes from the CMake build
// (android-arm64 preset, see CMakePresets.json) and is copied to
// app/src/main/jniLibs/<abi>/ by the android/collect-native-libs.sh script.
// Gradle only packages and signs the APK.
//
// Alice's own files (the repository's assets/ folder: shaders, fonts,
// sounds, localization) go inside the APK at assets/assets/... along with a list
// (alice_assets_manifest.txt); C++ extracts everything to internal storage
// on the first run and whenever the list changes (entry_point_android.cpp).

import java.util.zip.CRC32

plugins {
    id("com.android.application")
}

val aliceAssetsSource = rootProject.file("../assets")
val aliceAssetsOut = layout.buildDirectory.dir("generated/aliceAssets")

val prepareAliceAssets by tasks.registering {
    inputs.dir(aliceAssetsSource)
    outputs.dir(aliceAssetsOut)
    doLast {
        val out = aliceAssetsOut.get().asFile
        out.deleteRecursively()
        val dest = File(out, "assets")
        aliceAssetsSource.copyRecursively(dest, overwrite = true)
        // list "path<TAB>size<TAB>crc32" -- the NDK cannot list
        // APK subfolders, and the crc makes the extraction repeat if anything changes
        val manifest = dest.walkTopDown()
            .filter { it.isFile }
            .map {
                val crc = CRC32()
                crc.update(it.readBytes())
                it.relativeTo(out).invariantSeparatorsPath + "\t" + it.length() + "\t" + crc.value.toString(16)
            }
            .sorted()
            .joinToString("\n")
        File(out, "alice_assets_manifest.txt").writeText(manifest + "\n")
    }
}

android {
    namespace = "org.projectalice.mobile"
    compileSdk = 35

    defaultConfig {
        applicationId = "org.projectalice.mobile"
        minSdk = 30 // Android 11; ICU: see src/text/icu_android_compat.hpp
        targetSdk = 35
        versionCode = 5
        versionName = "0.6"

        ndk {
            abiFilters += listOf("arm64-v8a")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    // Fixed key: with it every APK comes out with the same signature and a new version
    // installs on top of the previous one. The workflow assembles it from the secrets
    // ANDROID_KEYSTORE_BASE64 and ANDROID_KEYSTORE_PASSWORD (see android/README.md).
    // Locally it is also picked up from android/alice-release.jks when present.
    // Without a key, it signs with the machine's debug key.
    val aliceKeystorePassword = System.getenv("ALICE_KEYSTORE_PASSWORD")
    val aliceKeystorePath = System.getenv("ALICE_KEYSTORE")?.takeIf { it.isNotEmpty() } ?: "alice-release.jks"
    val aliceKeystoreFile = file(aliceKeystorePath)
    val hasAliceKey = aliceKeystoreFile.exists() && !aliceKeystorePassword.isNullOrEmpty()
    signingConfigs {
        if (hasAliceKey) {
            create("alice") {
                storeFile = aliceKeystoreFile
                storePassword = aliceKeystorePassword
                keyAlias = System.getenv("ALICE_KEY_ALIAS") ?: "alice"
                keyPassword = System.getenv("ALICE_KEY_PASSWORD") ?: aliceKeystorePassword
            }
        }
    }

    buildTypes {
        val signing = signingConfigs.getByName(if (hasAliceKey) "alice" else "debug")
        debug {
            signingConfig = signing
        }
        release {
            isMinifyEnabled = false
            signingConfig = signing
        }
    }

    sourceSets {
        getByName("main") {
            assets.srcDir(aliceAssetsOut)
        }
    }

    androidResources {
        // the default ignores files starting with "." and folders with "_"
        ignoreAssetsPattern = "!.svn:!.git:!.ds_store:!*.scc:!CVS:!thumbs.db:!picasa.ini:!*~"
    }

    packaging {
        jniLibs {
            // the .so files already arrive without symbols (strip in collect-native-libs.sh)
            keepDebugSymbols += listOf("**/*.so")
        }
    }
}

tasks.named("preBuild") {
    dependsOn(prepareAliceAssets)
}
