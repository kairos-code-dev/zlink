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

rootProject.name = "zlink-java-e2e-to-actor-messaging"

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

include(":Shared")
include(":Client")
include(":Server:Actor")
include(":Server:Caller")
