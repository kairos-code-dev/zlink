package systems.zlink.samples.kotlin.tictactoe.server.play.sessions

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.actors.ZLinkActorManager
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.streams.ZLinkSession
import systems.zlink.framework.streams.ZLinkSessionActor
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.framework.streams.ZLinkStreamError
import systems.zlink.framework.streams.ZLinkStreamHeader
import systems.zlink.framework.streams.ZLinkStreamCodec as FrameworkStreamCodec
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleNames
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.AuthenticatePlayerReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.AuthenticatePlayerRes
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.AuthenticateReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.AuthenticateRes
import systems.zlink.stream.connector.ZLinkStreamCodec as ConnectorStreamCodec
import systems.zlink.stream.connector.ZLinkStreamEncodedPayload
import systems.zlink.stream.connector.json.ZLinkStreamJson

class PlaySession(
    private val context: ZLinkSessionContext,
    private val actors: ZLinkActorManager,
    private val channels: ZLinkClient,
) : ZLinkSession {
    private var actorId: String? = null
    private var actor: ZLinkSessionActor? = null

    override fun context(): ZLinkSessionContext = context

    override fun onConnectedAsync(): CompletionStage<Void> = CompletableFuture.completedFuture(null)

    override fun onDisconnectedAsync(): CompletionStage<Void> {
        actorId = null
        actor = null
        return CompletableFuture.completedFuture(null)
    }

    override fun onErrorAsync(error: ZLinkStreamError): CompletionStage<Void> =
        CompletableFuture.completedFuture(null)

    override fun onDispatchAsync(header: ZLinkStreamHeader, payload: Message): CompletionStage<Void> =
        try {
            if (header.packetName() == "AuthenticateReq") {
                val request = decode(header, payload, AuthenticateReq::class.java)
                channels
                    .requestToChannel(
                        SampleNames.ApiChannel,
                        AuthenticatePlayerReq(request.accessToken),
                    )
                    .packetName("AuthenticatePlayer")
                    .submitAsync(AuthenticatePlayerRes::class.java)
                    .thenCompose { authenticated ->
                        val authenticatedActorId = authenticated.actorId
                        actorId = authenticatedActorId
                        actors.getOrCreateAsync(authenticatedActorId, SampleNames.PlayActor)
                    }
                    .thenCompose(context.actors()::bindAsync)
                    .thenCompose { bound ->
                        actor = bound
                        context.client().reply(AuthenticateRes(bound.actorId())).submitAsync()
                    }
            } else {
                requireActor().relayAsync(header, payload)
            }
        } catch (ex: RuntimeException) {
            CompletableFuture.failedFuture(ex)
        }

    private fun requireActor(): ZLinkSessionActor =
        actor?.takeIf { !actorId.isNullOrBlank() }
            ?: throw IllegalStateException("AuthenticateReq is required before play packets.")

    private fun <T> decode(header: ZLinkStreamHeader, payload: Message, type: Class<T>): T =
        ZLinkStreamJson.decode(
            ZLinkStreamEncodedPayload(
                header.packetName(),
                payload,
                header.metadata(),
                connectorCodec(header),
            ),
            type,
        )

    private fun connectorCodec(header: ZLinkStreamHeader): ConnectorStreamCodec =
        when (header.codec()) {
            FrameworkStreamCodec.RAW -> ConnectorStreamCodec.RAW
            FrameworkStreamCodec.JSON -> ConnectorStreamCodec.JSON
            FrameworkStreamCodec.MESSAGE_PACK -> ConnectorStreamCodec.MESSAGE_PACK
            FrameworkStreamCodec.PROTOBUF -> ConnectorStreamCodec.PROTOBUF
        }
}
