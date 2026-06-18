package systems.zlink.samples.kotlin.tictactoe.server.play

import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.framework.codecs.msgpack.ZLinkMessagePackCodec
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleLogging
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleNames
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleSettings
import systems.zlink.samples.kotlin.tictactoe.server.play.adapters.zlink.actors.PlayActorFactory
import systems.zlink.samples.kotlin.tictactoe.server.play.adapters.zlink.spots.PlayEntrySpot
import systems.zlink.samples.kotlin.tictactoe.server.play.adapters.zlink.spots.TicTacToeGame
import systems.zlink.samples.kotlin.tictactoe.server.play.adapters.zlink.sessions.PlaySession
import systems.zlink.samples.kotlin.tictactoe.server.play.adapters.zlink.sessions.handlers.AuthenticatePlaySessionHandler

object PlayServer {
    fun configure(settings: SampleSettings): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            SampleLogging.configure(settings, "play")
            options.codecs().use(ZLinkMessagePackCodec.defaultCodec())
            options.addHandlersFromPackageOf(PlayServer::class.java)
            options.addActorFactory(SampleNames.PlayActor, PlayActorFactory::class.java)
            options.addClientServerChannel(SampleNames.ApiChannel)
                .enableClient(settings.apiChannelEndpoint)
            options.addClientServerChannel(SampleNames.PlayChannel)
                .enableServer(settings.playChannelEndpoint)
                .addHandlerGroup(SampleNames.PlayChannel)
            val route = options.addRouteMeshChannel(SampleNames.PlayRouteChannel)
            route.enableServer(settings.playRouterEndpoint)
            route.enableClient(settings.playRouterEndpoint)
            route.configureRouting().setRoutingId(RoutingId.from(SampleNames.PlayRouterId))
            val node = options.addSpotMesh(SampleNames.SpotMesh)
                .addNode(SampleNames.PlayNode)
            node.enableRouter(settings.spotEndpoint)
                .setRouterRoutingId(RoutingId.from(SampleNames.PlayNodeRoutingId))
            node.configureEntrySpot().setRoutingId(RoutingId.from(SampleNames.EntrySpotRoutingId))
            node.addEntrySpot(PlayEntrySpot::class.java)
            node.addSpotFactory(TicTacToeGame::class.java)
            options.addStreamNode(SampleNames.PlayStream)
                .bind(settings.playEndpoint)
                .registerSession(PlaySession::class.java)
                .addSessionPacketHandler(AuthenticatePlaySessionHandler::class.java)
        }
}
