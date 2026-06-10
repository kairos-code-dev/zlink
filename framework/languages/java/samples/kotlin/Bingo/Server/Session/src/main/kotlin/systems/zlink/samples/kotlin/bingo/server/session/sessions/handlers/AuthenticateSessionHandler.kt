package systems.zlink.samples.kotlin.bingo.server.session.sessions.handlers

import kotlinx.coroutines.future.await
import systems.zlink.contracts.core.RoutingId
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.actors.ZLinkActorRef
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.kotlin.ZLinkCoroutineSessionPacketHandler
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.framework.streams.ZLinkSessionContext
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
import systems.zlink.stream.connector.protobuf.ZLinkStreamProtobuf

class AuthenticateSessionHandler(
    private val channels: ZLinkClient,
    coroutines: ZLinkCoroutineRuntime,
) : ZLinkCoroutineSessionPacketHandler<ZLinkSessionContext>(coroutines, "AuthenticateReq") {
    override suspend fun handle(
        context: ZLinkSessionContext,
        header: ZLinkStreamHeader,
        payload: Message,
    ) {
        val request = ZLinkStreamProtobuf.decode(
            ZLinkStreamEncodedPayload(
                header.packetName(),
                payload,
                header.metadata(),
                connectorCodec(header),
            ),
            AuthenticateReq::class.java,
        )
        if (request.accessToken.isBlank()) {
            throw IllegalArgumentException("access token is required")
        }

        val authenticated = channels
            .requestToChannel(SampleNames.ApiChannel, AuthenticatePlayerReq(request.accessToken))
            .timeout(SampleTimings.RequestTimeout)
            .submit(AuthenticatePlayerRes::class.java)
            .await()
        if (!authenticated.accepted ||
            authenticated.actorId.isBlank() ||
            authenticated.displayName.isBlank()
        ) {
            throw IllegalStateException(
                authenticated.reason ?: "Player authentication failed.",
            )
        }
        val ensured = channels
            .requestToChannel(
                SampleNames.PlayChannel,
                EnsurePlayerActorReq(
                    authenticated.actorId,
                    authenticated.displayName,
                ),
            )
            .timeout(SampleTimings.RequestTimeout)
            .submit(EnsurePlayerActorRes::class.java)
            .await()
        context.actors()
            .bindAsync(
                ZLinkActorRef(
                    RoutingId.from(ensured.actor.nodeRid),
                    ensured.actor.actorId,
                    ensured.actor.generation,
                ),
            )
            .await()
        context.client()
            .reply(
                AuthenticateRes(
                    ensured.actorId,
                    authenticated.displayName,
                ),
            )
            .submit()
            .await()
    }

    private fun connectorCodec(header: ZLinkStreamHeader): ConnectorStreamCodec =
        when (header.codec()) {
            FrameworkStreamCodec.RAW -> ConnectorStreamCodec.RAW
            FrameworkStreamCodec.JSON -> ConnectorStreamCodec.JSON
            FrameworkStreamCodec.MESSAGE_PACK -> ConnectorStreamCodec.MESSAGE_PACK
            FrameworkStreamCodec.PROTOBUF -> ConnectorStreamCodec.PROTOBUF
        }
}
