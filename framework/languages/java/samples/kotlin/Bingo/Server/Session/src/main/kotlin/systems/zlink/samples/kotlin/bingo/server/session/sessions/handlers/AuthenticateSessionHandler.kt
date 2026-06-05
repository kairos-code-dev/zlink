package systems.zlink.samples.kotlin.bingo.server.session.sessions.handlers

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.contracts.core.RoutingId
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.actors.ZLinkActorRef
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.framework.streams.ZLinkSessionPacketHandler
import systems.zlink.framework.streams.ZLinkStreamCodec as FrameworkStreamCodec
import systems.zlink.framework.streams.ZLinkStreamHeader
import systems.zlink.samples.kotlin.bingo.shared.configuration.SampleNames
import systems.zlink.samples.kotlin.bingo.shared.configuration.SampleTimings
import systems.zlink.samples.kotlin.bingo.shared.contracts.AuthenticatePlayerReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.AuthenticatePlayerRes
import systems.zlink.samples.kotlin.bingo.shared.contracts.AuthenticateReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.AuthenticateRes
import systems.zlink.samples.kotlin.bingo.shared.contracts.EnsurePlayerActorReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.EnsurePlayerActorRes
import systems.zlink.stream.connector.ZLinkStreamCodec as ConnectorStreamCodec
import systems.zlink.stream.connector.ZLinkStreamEncodedPayload
import systems.zlink.stream.connector.json.ZLinkStreamJson

class AuthenticateSessionHandler(
    private val channels: ZLinkClient,
) : ZLinkSessionPacketHandler<ZLinkSessionContext> {
    override fun packetName(): String = "AuthenticateReq"

    override fun handleAsync(
        context: ZLinkSessionContext,
        header: ZLinkStreamHeader,
        payload: Message,
    ): CompletionStage<Void> {
        val request = ZLinkStreamJson.decode(
            ZLinkStreamEncodedPayload(
                header.packetName(),
                payload,
                header.metadata(),
                connectorCodec(header),
            ),
            AuthenticateReq::class.java,
        )
        if (request.accessToken.isBlank()) {
            return CompletableFuture.failedFuture(
                IllegalArgumentException("access token is required"),
            )
        }

        return channels
            .requestToChannel(SampleNames.ApiChannel, AuthenticatePlayerReq(request.accessToken))
            .packetName("AuthenticatePlayer")
            .timeout(SampleTimings.RequestTimeout)
            .submitAsync(AuthenticatePlayerRes::class.java)
            .thenCompose { authenticated ->
                if (!authenticated.accepted ||
                    authenticated.actorId.isBlank() ||
                    authenticated.displayName.isBlank()
                ) {
                    CompletableFuture.failedFuture(
                        IllegalStateException(
                            authenticated.reason ?: "Player authentication failed.",
                        ),
                    )
                } else {
                    channels
                        .requestToChannel(
                            SampleNames.PlayChannel,
                            EnsurePlayerActorReq(
                                authenticated.actorId,
                                authenticated.displayName,
                            ),
                        )
                        .packetName("EnsurePlayerActor")
                        .timeout(SampleTimings.RequestTimeout)
                        .submitAsync(EnsurePlayerActorRes::class.java)
                        .thenCompose { ensured ->
                            context.actors()
                                .bindAsync(
                                    ZLinkActorRef(
                                        RoutingId.from(ensured.actor.nodeRid),
                                        ensured.actor.actorId,
                                        ensured.actor.generation,
                                    ),
                                )
                                .thenCompose {
                                    context.client()
                                        .reply(
                                            AuthenticateRes(
                                                ensured.actorId,
                                                authenticated.displayName,
                                            ),
                                        )
                                        .submitAsync()
                                }
                        }
                }
            }
    }

    private fun connectorCodec(header: ZLinkStreamHeader): ConnectorStreamCodec =
        when (header.codec()) {
            FrameworkStreamCodec.RAW -> ConnectorStreamCodec.RAW
            FrameworkStreamCodec.JSON -> ConnectorStreamCodec.JSON
            FrameworkStreamCodec.MESSAGE_PACK -> ConnectorStreamCodec.MESSAGE_PACK
            FrameworkStreamCodec.PROTOBUF -> ConnectorStreamCodec.PROTOBUF
        }
}
