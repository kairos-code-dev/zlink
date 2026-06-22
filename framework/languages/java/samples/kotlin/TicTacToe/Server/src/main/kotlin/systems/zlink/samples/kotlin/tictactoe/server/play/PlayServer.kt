package systems.zlink.samples.kotlin.tictactoe.server.play

import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.configuration.RouteMeshChannelBuilder
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.framework.codecs.msgpack.ZLinkMessagePackCodec
import systems.zlink.samples.kotlin.tictactoe.server.configuration.RedisSpotRemoteAddressResolver
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
            val route: RouteMeshChannelBuilder = options.addRouteMeshChannel(SampleNames.RouteChannel)
            route.enableServer(settings.routeEndpoint)
                .enableClient(settings.peerRouteEndpoint)
                .enableSpotRouteEgress(SampleNames.RouteChannel)
                .setRoutingId(RoutingId.from(settings.playSpotNodeRid))
            options.addSpotRemoteAddressResolver(RedisSpotRemoteAddressResolver::class.java)
            val node = options.addSpotMesh(SampleNames.SpotMesh)
                .addNode(SampleNames.PlayNode)
            node.enableRouter(settings.spotEndpoint)
                .setRouterRoutingId(RoutingId.from(settings.playSpotNodeRid))
            node.connectRouter(RoutingId.from(settings.peerPlaySpotNodeRid), settings.peerSpotEndpoint)
            node.acceptSpotRoutesFromChannel(SampleNames.RouteChannel, settings.peerRouteEndpoint)
            node.enablePubSub(settings.spotPubSubEndpoint)
                .setPubSubRoutingId(RoutingId.from("${settings.playSpotNodeRid}-pub"))
            node.connectPeerPub(settings.peerSpotPubSubEndpoint)
            node.configureEntrySpot().setRoutingId(RoutingId.from(SampleNames.EntrySpotRoutingId))
            node.addEntrySpot(PlayEntrySpot::class.java)
            node.addSpotFactory(TicTacToeGame::class.java)
            options.addStreamNode(SampleNames.PlayStream)
                .bind(settings.playEndpoint)
                .attachActorGateway(SampleNames.PlayNode)
                .registerSession(PlaySession::class.java)
                .addSessionPacketHandler(AuthenticatePlaySessionHandler::class.java)
        }
}
