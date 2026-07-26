package systems.zlink.samples.kotlin.tictactoe.server.play

import kotlinx.coroutines.Dispatchers
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
import systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.handlers.CreateGameHandler
import systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.spots.entryspot.PlayEntrySpot
import systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.spots.tictactoegamespot.TicTacToeGame
import systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.sessions.PlaySession
import systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.sessions.handlers.AuthenticatePlaySessionHandler
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.CreateGameReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.CreateGameRes

object PlayServer {
    fun configure(settings: SampleSettings): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            SampleLogging.configure(settings, "play")
            options.useCoroutineHandlers(Dispatchers.Default)
            options.codecs().use(ZLinkMessagePackCodec.defaultCodec())
            options.configureDispatch {
                messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                traceLogFile(SampleLogging.flowLogPath(settings, "play-${settings.playIndex}"))
                traceLabel("play-${settings.playIndex}")
            }
            options.addClientServerChannel(SampleNames.ApiChannel)
                .enableClient(settings.apiChannelEndpoint)
            val playChannel = options.addClientServerChannel(SampleNames.PlayChannel)
                .enableServer(settings.playChannelEndpoint)
            playChannel.addRequestHandler(
                CreateGameHandler::class.java,
                CreateGameReq::class.java,
                CreateGameRes::class.java,
            )
            val node = options.addRouteMesh(SampleNames.SpotMesh)
            val routeEndpoint = settings.routeEndpoint.ifBlank { settings.spotEndpoint }

            node.listen(routeEndpoint)
                .setRoutingIdPrefix("tictactoe-play")
            node.channelName(SampleNames.PlayNode)
            node.peerConnections().connect(settings.peerSpotEndpoint)
            node.addEntrySpot(PlayEntrySpot::class.java)
            node.addSpotFactory(TicTacToeGame::class.java)
            node.addActorFactory(SampleNames.PlayActor, PlayActorFactory::class.java)
            node.addActorTransferAdapter(SampleNames.PlayActor, PlayActorTransferAdapter::class.java)
            options.addStreamNode(SampleNames.PlayStream)
                .bind(settings.playEndpoint)
                .enableActorDispatch(SampleNames.SpotMesh)
                .registerSession(PlaySession::class.java)
                .addSessionPacketHandler(AuthenticatePlaySessionHandler::class.java)
        }
}
