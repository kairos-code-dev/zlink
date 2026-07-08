pluginManagement {
    plugins {
        id("org.jetbrains.kotlin.jvm") version "2.1.0"
    }
    repositories {
        gradlePluginPortal()
        mavenCentral()
    }
}

fun zlinkFrameworkJavaRoot(): java.io.File {
    var current = settingsDir
    while (current.parentFile != null && !current.resolve("gradle/libs.versions.toml").isFile) {
        current = current.parentFile
    }
    return current
}

fun zlinkLocalMavenRepository(): java.io.File {
    val configuredRoot = providers.gradleProperty("zlink.localPackageRoot")
        .orElse(providers.environmentVariable("ZLINK_LOCAL_PACKAGE_ROOT"))
        .orNull
    if (!configuredRoot.isNullOrBlank()) {
        return file(configuredRoot).resolve("maven")
    }
    var current = settingsDir
    while (current.parentFile != null && !current.resolve(".git").exists()) {
        current = current.parentFile
    }
    return current.resolve(".artifacts/wsl/maven")
}

dependencyResolutionManagement {
    versionCatalogs {
        create("zlinkLibs") {
            from(files(zlinkFrameworkJavaRoot().resolve("gradle/libs.versions.toml")))
        }
    }
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        maven {
            name = "zlinkLocalPackages"
            url = uri(zlinkLocalMavenRepository())
        }
        mavenCentral()
    }
}

rootProject.name = "zlink-kotlin-sample-deliverydispatch"

if (gradle.parent == null) {
    includeBuild("../../..") {
        name = "zlink-framework-java-build"
    }
}

include("Client")
include("Server:Configuration")
include("Server:Registry")
include("Server:Dispatch")
include("Server:CourierGateway")
include("Server:CourierSession")
include("Server:CourierSpotNode")
include("Server:Tracking")
include("Server:CustomerGateway")
include("Shared")
