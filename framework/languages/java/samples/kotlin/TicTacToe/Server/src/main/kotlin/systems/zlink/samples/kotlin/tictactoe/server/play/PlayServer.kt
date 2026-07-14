package systems.zlink.samples.kotlin.tictactoe.server.play

import kotlinx.coroutines.Dispatchers
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.codecs.msgpack.ZLinkMessagePackCodec
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.kotlin.configureDispatch
import systems.zlink.framework.kotlin.useCoroutineHandlers
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleLogging
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleNames
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleSettings
import systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.actors.PlayActorFactory
import systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.actors.PlayActorTransferAdapter
import systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.spots.entryspot.PlayEntrySpot
import systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.spots.tictactoegamespot.TicTacToeGame
import systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.sessions.PlaySession
import systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.sessions.handlers.AuthenticatePlaySessionHandler

object PlayServer {
    fun configure(settings: SampleSettings): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            SampleLogging.configure(settings, "play")
            options.useCoroutineHandlers(Dispatchers.Default)
            options.codecs().use(ZLinkMessagePackCodec.defaultCodec())
            options.configureDispatch {
                messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                traceLogFile(SampleLogging.flowLogPath(settings, settings.playSpotNodeRid))
                traceLabel(settings.playSpotNodeRid)
            }
            options.addHandlersFromPackageOf(PlayServer::class.java)
            options.addClientServerChannel(SampleNames.ApiChannel)
                .enableClient(settings.apiChannelEndpoint)
            options.addClientServerChannel(SampleNames.playChannel(settings.playIndex))
                .enableServer(settings.playChannelEndpoint)
                .addHandlerGroup(SampleNames.PlayHandlerGroup)
            val node = options.addSpotMesh(SampleNames.SpotMesh)
            val routeEndpoint = settings.routeEndpoint.ifBlank { settings.spotEndpoint }

            node.enableRouter(routeEndpoint)
                .setRoutingId(RoutingId.from(settings.playSpotNodeRid))
            node.connectRouter(RoutingId.from(settings.peerPlaySpotNodeRid), settings.peerSpotEndpoint)
            node.enablePubSub(settings.spotPubSubEndpoint)
            node.connectPeerPub(settings.peerSpotPubSubEndpoint)
            node.configureEntrySpot().setRoutingId(RoutingId.from(SampleNames.EntrySpotRoutingId))
            node.addEntrySpot(PlayEntrySpot::class.java)
            node.addSpotFactory(TicTacToeGame::class.java)
            node.addActorFactory(SampleNames.PlayActor, PlayActorFactory::class.java)
            node.addActorTransferAdapter(SampleNames.PlayActor, PlayActorTransferAdapter::class.java)
            options.addStreamNode(SampleNames.PlayStream)
                .bind(settings.playEndpoint)
                .registerSession(PlaySession::class.java)
                .addSessionPacketHandler(AuthenticatePlaySessionHandler::class.java)
        }
}
