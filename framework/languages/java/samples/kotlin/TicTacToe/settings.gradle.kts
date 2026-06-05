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

rootProject.name = "zlink-kotlin-sample-tictactoe"

includeBuild("../../..") {
    name = "zlink-framework-java-build"
}
includeBuild("../../../../../../bindings/java") {
    name = "zlink-binding-java-build"
    dependencySubstitution {
        substitute(module("systems.zlink:zlink")).using(project(":"))
    }
}

include("Client")
include("Server")
include("Shared")
