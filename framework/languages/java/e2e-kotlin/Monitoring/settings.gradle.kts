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

rootProject.name = "zlink-kotlin-e2e-monitoring"

if (gradle.parent == null) {
    includeBuild("../..") {
        name = "zlink-framework-java-build"
    }
}

includeBuild("../../../../../bindings/java") {
    name = "zlink-bindings-java"
    dependencySubstitution {
        substitute(module("systems.zlink:zlink")).using(project(":"))
    }
}
