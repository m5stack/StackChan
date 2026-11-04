/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import com.android.build.gradle.BaseExtension
import org.gradle.api.tasks.compile.JavaCompile
import org.jetbrains.kotlin.gradle.dsl.JvmTarget
import org.jetbrains.kotlin.gradle.tasks.KotlinCompile

allprojects {
    repositories {
        google()
        mavenCentral()
    }
}

val newBuildDir: Directory =
    rootProject.layout.buildDirectory
        .dir("../../build")
        .get()
rootProject.layout.buildDirectory.value(newBuildDir)

subprojects {
    val newSubprojectBuildDir: Directory = newBuildDir.dir(project.name)
    project.layout.buildDirectory.value(newSubprojectBuildDir)
}

subprojects {
    // file_picker 11 skips applying Kotlin on AGP 9 because it assumes AGP's
    // built-in Kotlin support is enabled. Flutter currently keeps that support
    // disabled for compatibility with plugins that still apply KGP, so apply
    // KGP explicitly to ensure file_picker's Kotlin sources are compiled.
    if (name == "file_picker") {
        pluginManager.apply("org.jetbrains.kotlin.android")
    }

    // Keep Java and Kotlin bytecode targets aligned across app and plugin modules.
    tasks.withType<JavaCompile>().configureEach {
        sourceCompatibility = JavaVersion.VERSION_17.toString()
        targetCompatibility = JavaVersion.VERSION_17.toString()
    }
    tasks.withType<KotlinCompile>().configureEach {
        compilerOptions.jvmTarget.set(JvmTarget.JVM_17)
    }
    afterEvaluate {
        (extensions.findByName("android") as? BaseExtension)?.apply {
            // permission_handler_android 14 uses API 37 constants such as
            // ACCESS_LOCAL_NETWORK, so plugin modules must compile against 37.
            compileSdkVersion(37)
            compileOptions.apply {
                sourceCompatibility = JavaVersion.VERSION_17
                targetCompatibility = JavaVersion.VERSION_17
            }
        }
        if (project.name == "camera_android_camerax") {
            dependencies.add("compileOnly", "androidx.concurrent:concurrent-futures:1.1.0")
        }
    }
}

subprojects {
    project.evaluationDependsOn(":app")
}

tasks.register<Delete>("clean") {
    delete(rootProject.layout.buildDirectory)
}
