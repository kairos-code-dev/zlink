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

rootProject.name = "zlink-java-sample-tictactoe-session-gateway"

include("Client")
include("Server:Api")
include("Server:Play")
include("Server:Registry")
include("Server:Session")
include("Shared")
