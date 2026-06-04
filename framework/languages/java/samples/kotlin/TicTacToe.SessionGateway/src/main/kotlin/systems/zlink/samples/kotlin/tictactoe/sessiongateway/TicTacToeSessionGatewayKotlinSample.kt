package systems.zlink.samples.kotlin.tictactoe.sessiongateway

import kotlinx.coroutines.runBlocking
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.client.SessionActorDispatchClient
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.client.SessionActorDispatchClientOptions
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.api.ApiServerApplication
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.PlayServerApplication
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.registry.RegistryServerApplication
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session.SessionServerApplication

fun main() = runBlocking {
    val client = SessionActorDispatchClient()
    RegistryServerApplication.start().use {
        ApiServerApplication.start().use {
            PlayServerApplication.start().use {
                SessionServerApplication.start().use {
                    client.runReconnectScenario(SessionActorDispatchClientOptions.defaults())
                }
            }
        }
    }

    println("TicTacToe.SessionGateway Kotlin sample self-check passed")
}
