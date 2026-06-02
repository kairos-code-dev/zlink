package systems.zlink.samples.kotlin.tictactoe.sessiongateway

import kotlinx.coroutines.runBlocking
import systems.zlink.framework.ZLinkFramework
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.client.SessionActorDispatchClient
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.client.SessionActorDispatchClientOptions
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.api.ApiServer
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.PlayServer
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.registry.RegistryServer
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session.SessionServer

fun main() = runBlocking {
    val client = SessionActorDispatchClient()
    ZLinkFramework.start { options ->
        RegistryServer.configure(options)
        ApiServer.configure(options)
        PlayServer.configure(options)
        SessionServer.configure(options)
    }.use { framework ->
        client.verifyFrameworkActorCreation(framework.actorManager())
    }

    client.runReconnectScenario(SessionActorDispatchClientOptions("player-1"))
    println("TicTacToe.SessionGateway Kotlin sample self-check passed")
}
