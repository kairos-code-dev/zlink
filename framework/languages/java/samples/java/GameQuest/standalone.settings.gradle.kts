pluginManagement {
    repositories {
        gradlePluginPortal()
        mavenCentral()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        mavenCentral()
    }
}

rootProject.name = "zlink-framework-java-gamequest-sample"

includeBuild("../../..") {
    name = "zlink-framework-java-build"
}

include(
    ":Client",
    ":Server:Configuration",
    ":Server:GameApi",
    ":Server:QuestMission",
    ":Shared",
)
