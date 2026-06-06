pluginManagement {
    plugins {
        id("org.jetbrains.kotlin.jvm") version "2.1.0"
        id("org.jetbrains.kotlin.plugin.spring") version "2.1.0"
    }
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

rootProject.name = "zlink-framework-java-samples"

includeBuild("..") {
    name = "zlink-framework-java-build"
}

val localBindingsDir = settingsDir.resolve("../../../../bindings/java").normalize()
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
    ":java:Async",
    ":java:Bingo:Client",
    ":java:Bingo:Server:Api",
    ":java:Bingo:Server:Play",
    ":java:Bingo:Server:Registry",
    ":java:Bingo:Server:Session",
    ":java:Bingo:Shared",
    ":java:StreamingClient",
    ":java:TicTacToe:Client",
    ":java:TicTacToe:Server",
    ":java:TicTacToe:Shared",
    ":java:TicTacToe.SessionGateway:Client",
    ":java:TicTacToe.SessionGateway:Server:Api",
    ":java:TicTacToe.SessionGateway:Server:Play",
    ":java:TicTacToe.SessionGateway:Server:Registry",
    ":java:TicTacToe.SessionGateway:Server:Session",
    ":java:TicTacToe.SessionGateway:Shared",
    ":kotlin:Async",
    ":kotlin:Bingo:Client",
    ":kotlin:Bingo:Server:Api",
    ":kotlin:Bingo:Server:Play",
    ":kotlin:Bingo:Server:Registry",
    ":kotlin:Bingo:Server:Session",
    ":kotlin:Bingo:Shared",
    ":kotlin:StreamingClient",
    ":kotlin:TicTacToe:Client",
    ":kotlin:TicTacToe:Server",
    ":kotlin:TicTacToe:Shared",
    ":kotlin:TicTacToe.SessionGateway:Client",
    ":kotlin:TicTacToe.SessionGateway:Server:Api",
    ":kotlin:TicTacToe.SessionGateway:Server:Play",
    ":kotlin:TicTacToe.SessionGateway:Server:Registry",
    ":kotlin:TicTacToe.SessionGateway:Server:Session",
    ":kotlin:TicTacToe.SessionGateway:Shared",
)
