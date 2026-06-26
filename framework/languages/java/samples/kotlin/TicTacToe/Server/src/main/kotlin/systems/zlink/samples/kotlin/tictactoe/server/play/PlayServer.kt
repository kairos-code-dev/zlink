package systems.zlink.samples.kotlin.tictactoe.server.play

import kotlinx.coroutines.Dispatchers
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.configuration.RouteMeshChannelBuilder
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.framework.codecs.msgpack.ZLinkMessagePackCodec
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.kotlin.configureDispatch
import systems.zlink.framework.kotlin.useCoroutineHandlers
import systems.zlink.samples.kotlin.tictactoe.server.configuration.RedisSpotRemoteAddressResolver
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleLogging
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleNames
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleSettings
import systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.actors.PlayActorFactory
import systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.spots.entryspot.PlayEntrySpot
import systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.spots.tictactoegamespot.TicTacToeGame
import systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.sessions.PlaySession
import systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.sessions.handlers.AuthenticatePlaySessionHandler

object PlayServer {
    fun configure(settings: SampleSettings): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            SampleLogging.configure(settings, "play")
            options.useCoroutineHandlers(Dispatchers.Default)
            options.configureDispatch {
                messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                traceLogFile(SampleLogging.flowLogPath(settings.playSpotNodeRid))
                traceLabel(settings.playSpotNodeRid)
            }
            options.codecs().use(ZLinkMessagePackCodec.defaultCodec())
            options.addHandlersFromPackageOf(PlayServer::class.java)
            options.addClientServerChannel(SampleNames.ApiChannel)
                .enableClient(settings.apiChannelEndpoint)
            options.addClientServerChannel(SampleNames.PlayChannel)
                .enableServer(settings.playChannelEndpoint)
                .addHandlerGroup(SampleNames.PlayChannel)
            val route: RouteMeshChannelBuilder = options.addRouteMesh(SampleNames.RouteChannel)
            route.enableServer(settings.routeEndpoint)
                .enableClient(settings.peerRouteEndpoint)
                .setRoutingId(RoutingId.from(settings.playSpotNodeRid))
            options.addSpotRemoteAddressResolver(RedisSpotRemoteAddressResolver::class.java)
            val node = options.addSpotMesh(SampleNames.SpotMesh)

            node.enableRouter(settings.spotEndpoint)
                .setRoutingId(RoutingId.from(settings.playSpotNodeRid))
            node.connectRouter(RoutingId.from(settings.peerPlaySpotNodeRid), settings.peerSpotEndpoint)
            node.enablePubSub(settings.spotPubSubEndpoint)
            node.connectPeerPub(settings.peerSpotPubSubEndpoint)
            node.configureEntrySpot().setRoutingId(RoutingId.from(SampleNames.EntrySpotRoutingId))
            node.addEntrySpot(PlayEntrySpot::class.java)
            node.addSpotFactory(TicTacToeGame::class.java)
            node.addActorFactory(SampleNames.PlayActor, PlayActorFactory::class.java)
            options.addStreamNode(SampleNames.PlayStream)
                .bind(settings.playEndpoint)
                .registerSession(PlaySession::class.java)
                .addSessionPacketHandler(AuthenticatePlaySessionHandler::class.java)
        }
}
