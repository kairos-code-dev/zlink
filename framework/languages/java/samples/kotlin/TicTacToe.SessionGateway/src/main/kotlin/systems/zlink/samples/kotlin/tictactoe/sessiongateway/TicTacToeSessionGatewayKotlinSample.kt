package systems.zlink.samples.kotlin.tictactoe.sessiongateway

import kotlinx.coroutines.runBlocking
import systems.zlink.framework.ZLinkFramework
import systems.zlink.framework.ZLinkRegistry
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.client.SessionActorDispatchClient
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.client.SessionActorDispatchClientOptions
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.api.ApiServer
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.PlayServer
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.registry.RegistryServer
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session.SessionServer
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration.SampleTopology

fun main() = runBlocking {
    val client = SessionActorDispatchClient()
    ZLinkRegistry.start { options ->
        options.setPubEndpoint(SampleTopology.RegistryPubEndpoint)
        options.setRouterEndpoint(SampleTopology.RegistryRouterEndpoint)
    }.use {
        ZLinkFramework.start { options ->
            RegistryServer.configure(options)
            ApiServer.configure(options)
            PlayServer.configure(options)
            SessionServer.configure(options)
        }.use { framework ->
            client.verifyFrameworkActorCreation(framework.actorManager())
            client.runReconnectScenario(
                framework,
                SessionActorDispatchClientOptions("player-1"),
            )
        }
    }

    println("TicTacToe.SessionGateway Kotlin sample self-check passed")
}
