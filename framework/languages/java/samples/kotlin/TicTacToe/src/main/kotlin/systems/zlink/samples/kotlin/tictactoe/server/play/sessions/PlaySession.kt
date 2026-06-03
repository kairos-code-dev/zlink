package systems.zlink.samples.kotlin.tictactoe.server.play.sessions

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.actors.ZLinkActorManager
import systems.zlink.framework.streams.ZLinkSession
import systems.zlink.framework.streams.ZLinkSessionActor
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.framework.streams.ZLinkStreamError
import systems.zlink.framework.streams.ZLinkStreamHeader
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleNames
import systems.zlink.samples.kotlin.tictactoe.server.play.actors.PlayActor
import systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.TicTacToeGameDirectory
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.AuthenticateReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.AuthenticateRes
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.JoinGameReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.PlaceMarkReq
import systems.zlink.stream.connector.ZLinkStreamEncodedPayload
import systems.zlink.stream.connector.json.ZLinkStreamJson

class PlaySession(
    private val context: ZLinkSessionContext,
    private val actors: ZLinkActorManager,
) : ZLinkSession {
    private var actorId: String? = null
    private var playActor: PlayActor? = null
    private var actor: ZLinkSessionActor? = null

    override fun context(): ZLinkSessionContext = context
    override fun onConnectedAsync(): CompletionStage<Void> = CompletableFuture.completedFuture(null)
    override fun onDisconnectedAsync(): CompletionStage<Void> {
        actorId = null
        playActor = null
        actor = null
        return CompletableFuture.completedFuture(null)
    }
    override fun onErrorAsync(error: ZLinkStreamError): CompletionStage<Void> =
        CompletableFuture.completedFuture(null)

    override fun onDispatchAsync(header: ZLinkStreamHeader, payload: Message): CompletionStage<Void> =
        try {
            if (header.packetName() == "AuthenticateReq") {
                val request = decode(header, payload, AuthenticateReq::class.java)
                actorId = request.actorId
                actors.getOrCreateAsync(request.actorId, SampleNames.PlayActor)
                    .thenCompose { created ->
                        playActor = created as PlayActor
                        context.actors().bindAsync(created)
                    }
                    .thenCompose { bound ->
                        actor = bound
                        context.client().reply(AuthenticateRes(request.actorId)).submitAsync()
                    }
            } else if (header.packetName() == "JoinGameReq") {
                val request = decode(header, payload, JoinGameReq::class.java)
                val currentActor = requirePlayActor()
                currentActor.joinGame(request.gameId)
                context.client()
                    .reply(TicTacToeGameDirectory.get(request.gameId).join(currentActor.actorId()))
                    .submitAsync()
            } else if (header.packetName() == "PlaceMarkReq") {
                val request = decode(header, payload, PlaceMarkReq::class.java)
                val currentActor = requirePlayActor()
                context.client()
                    .reply(
                        TicTacToeGameDirectory.get(currentActor.gameId)
                            .placeMark(currentActor.actorId(), request.cell),
                    )
                    .submitAsync()
            } else {
                requireActor().relayAsync(header, payload)
            }
        } catch (ex: RuntimeException) {
            CompletableFuture.failedFuture(ex)
        }

    private fun requireActor(): ZLinkSessionActor =
        actor?.takeIf { !actorId.isNullOrBlank() }
            ?: throw IllegalStateException("AuthenticateReq is required before play packets.")

    private fun requirePlayActor(): PlayActor {
        requireActor()
        return playActor ?: throw IllegalStateException("bound PlayActor is missing.")
    }

    private fun <T> decode(header: ZLinkStreamHeader, payload: Message, type: Class<T>): T =
        ZLinkStreamJson.decode(
            ZLinkStreamEncodedPayload(header.packetName(), payload, header.metadata()),
            type,
        )
}
