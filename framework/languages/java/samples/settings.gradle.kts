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
