pluginManagement {
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

val zlinkGitHubPackagesUrl = providers.gradleProperty("zlink.githubPackagesUrl")
    .orElse(providers.environmentVariable("ZLINK_GITHUB_PACKAGES_URL"))
    .orElse("https://maven.pkg.github.com/kairos-code-dev/zlink")
val zlinkGitHubPackagesUser = providers.gradleProperty("zlink.githubPackagesUser")
    .orElse(providers.environmentVariable("MAVEN_REPOSITORY_USERNAME"))
    .orElse(providers.environmentVariable("GITHUB_ACTOR"))
val zlinkGitHubPackagesToken = providers.gradleProperty("zlink.githubPackagesToken")
    .orElse(providers.environmentVariable("MAVEN_REPOSITORY_PASSWORD"))
    .orElse(providers.environmentVariable("GITHUB_TOKEN"))
val zlinkLocalPackageRoot = providers.gradleProperty("zlink.localPackageRoot")
    .orElse(providers.environmentVariable("ZLINK_LOCAL_PACKAGE_ROOT"))
val zlinkLocalMavenRepo = zlinkLocalPackageRoot
    .map { file(it).resolve("maven") }
    .orElse(
        providers.provider {
            val wslRepo = settingsDir.resolve("../../../.artifacts/wsl/maven").normalize()
            val windowsRepo = settingsDir.resolve("../../../.artifacts/windows/maven").normalize()
            when {
                wslRepo.isDirectory -> wslRepo
                windowsRepo.isDirectory -> windowsRepo
                else -> wslRepo
            }
        }
    )

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
            url = uri(zlinkLocalMavenRepo.get())
        }
        mavenCentral()
        val packageUser = zlinkGitHubPackagesUser.orNull
        val packageToken = zlinkGitHubPackagesToken.orNull
        if (!packageUser.isNullOrBlank() && !packageToken.isNullOrBlank()) {
            maven {
                name = "zlinkGitHubPackages"
                url = uri(zlinkGitHubPackagesUrl.get())
                credentials {
                    username = packageUser
                    password = packageToken
                }
            }
        }
    }
}

rootProject.name = "zlink-framework-java"

if (gradle.parent == null) {
    includeBuild("samples") {
        name = "zlink-framework-java-samples"
    }
}

include(
    "zlink-framework-core",
    "zlink-framework-codec-protobuf",
    "zlink-framework-codec-msgpack",
    "zlink-framework-locations-redis",
    "zlink-http-client",
    "zlink-framework-spring-boot-starter",
    "zlink-stream-connector",
    "zlink-framework-kotlin",
    "zlink-http-client-kotlin",
    "zlink-framework-testkit",
)
