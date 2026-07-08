pluginManagement {
    repositories {
        gradlePluginPortal()
        mavenCentral()
    }
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

rootProject.name = "zlink-java-e2e-to-actor-messaging"

if (gradle.parent == null) {
    includeBuild("../..") {
        name = "zlink-framework-java-build"
    }
}

include(":Shared")
include(":Client")
include(":Server:Actor")
include(":Server:Caller")
