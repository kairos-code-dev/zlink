package systems.zlink.samples.kotlin.tictactoe.sessiongateway.client

suspend fun main() {
    SessionActorDispatchClient().runReconnectScenario(
        SessionActorDispatchClientOptions.defaults(),
    )
    println("TicTacToe.SessionGateway Kotlin sample self-check passed")
}
