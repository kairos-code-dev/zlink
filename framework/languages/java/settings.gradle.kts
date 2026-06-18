pluginManagement {
    repositories {
        gradlePluginPortal()
        mavenCentral()
    }
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

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
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

includeBuild("samples") {
    name = "zlink-framework-java-samples"
}

val localBindingsDir = settingsDir.resolve("../../../bindings/java").normalize()
val useLocalBindings = providers.gradleProperty("zlink.useLocalBindings")
    .map(String::toBoolean)
    .getOrElse(true)

if (useLocalBindings) {
    includeBuild(localBindingsDir) {
        name = "zlink-bindings-java"
        dependencySubstitution {
            substitute(module("systems.zlink:zlink")).using(project(":"))
        }
    }
}

include(
    "zlink-framework-core",
    "zlink-framework-codec-protobuf",
    "zlink-framework-codec-msgpack",
    "zlink-http-client",
    "zlink-framework-spring-boot-starter",
    "zlink-stream-connector",
    "zlink-framework-kotlin",
    "zlink-http-client-kotlin",
    "zlink-framework-testkit",
)
