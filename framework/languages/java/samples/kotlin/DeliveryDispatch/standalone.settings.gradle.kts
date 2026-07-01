pluginManagement {
    plugins {
        id("org.jetbrains.kotlin.jvm") version "2.1.0"
    }
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
