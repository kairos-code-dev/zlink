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

rootProject.name = "zlink-framework-java-samples"

includeBuild("java/Async") {
    name = "zlink-java-sample-async"
}
includeBuild("java/Bingo") {
    name = "zlink-java-sample-bingo"
}
includeBuild("java/StreamingClient") {
    name = "zlink-java-sample-streaming-client"
}
includeBuild("java/TicTacToe") {
    name = "zlink-java-sample-tictactoe"
}
includeBuild("java/TicTacToe.SessionGateway") {
    name = "zlink-java-sample-tictactoe-session-gateway"
}
includeBuild("kotlin/Async") {
    name = "zlink-kotlin-sample-async"
}
includeBuild("kotlin/Bingo") {
    name = "zlink-kotlin-sample-bingo"
}
includeBuild("kotlin/StreamingClient") {
    name = "zlink-kotlin-sample-streaming-client"
}
includeBuild("kotlin/TicTacToe") {
    name = "zlink-kotlin-sample-tictactoe"
}
includeBuild("kotlin/TicTacToe.SessionGateway") {
    name = "zlink-kotlin-sample-tictactoe-session-gateway"
}
