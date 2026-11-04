/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

plugins {
    id("com.android.application")
    id("dev.flutter.flutter-gradle-plugin")
}

android {
    namespace = "com.m5stack.stackchan"
    compileSdk = 37
    ndkVersion = flutter.ndkVersion

    packaging {
        jniLibs {
            pickFirsts.add("lib/arm64-v8a/libc++_shared.so")
            excludes.add("lib/armeabi-v7a/**")
            excludes.add("lib/x86/**")
            excludes.add("lib/x86_64/**")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    // Signing credentials are never committed. Export them before building:
    //   STACKCHAN_STORE_FILE, STACKCHAN_STORE_PASSWORD,
    //   STACKCHAN_KEY_ALIAS, STACKCHAN_KEY_PASSWORD
    signingConfigs {
        create("release") {
            storeFile = file(System.getenv("STACKCHAN_STORE_FILE") ?: "m5stack.jks")
            storePassword = System.getenv("STACKCHAN_STORE_PASSWORD") ?: ""
            keyAlias = System.getenv("STACKCHAN_KEY_ALIAS") ?: "key0"
            keyPassword = System.getenv("STACKCHAN_KEY_PASSWORD") ?: ""
        }
        getByName("debug") {
            storeFile = file(System.getenv("STACKCHAN_STORE_FILE") ?: "m5stack.jks")
            storePassword = System.getenv("STACKCHAN_STORE_PASSWORD") ?: ""
            keyAlias = System.getenv("STACKCHAN_KEY_ALIAS") ?: "key0"
            keyPassword = System.getenv("STACKCHAN_KEY_PASSWORD") ?: ""
        }
    }

    defaultConfig {
        applicationId = "com.m5stack.stackchan"
        minSdk = flutter.minSdkVersion
        targetSdk = flutter.targetSdkVersion
        versionCode = flutter.versionCode
        versionName = flutter.versionName

        ndk {
            abiFilters.clear()
            abiFilters.add("arm64-v8a")
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            isShrinkResources = false
            signingConfig = signingConfigs.getByName("release")
        }
        debug {
            isMinifyEnabled = false
            isShrinkResources = false
            signingConfig = signingConfigs.getByName("debug")
        }
    }
}

flutter {
    source = "../.."
}

dependencies {
    implementation("androidx.datastore:datastore-core:1.2.1")
}
