package systems.zlink.samples.kotlin.tictactoe.server.play.adapters.zlink.sessions.handlers

import kotlinx.coroutines.future.await
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.actors.ZLinkActorManager
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.framework.kotlin.ZLinkCoroutineSessionPacketHandler
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.framework.streams.ZLinkStreamHeader
import systems.zlink.framework.streams.ZLinkStreamCodec as FrameworkStreamCodec
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleNames
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.AuthenticatePlayerReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.AuthenticatePlayerRes
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.AuthenticateReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.AuthenticateRes
import systems.zlink.stream.connector.ZLinkStreamCodec as ConnectorStreamCodec
import systems.zlink.stream.connector.ZLinkStreamEncodedPayload
import systems.zlink.stream.connector.msgpack.ZLinkStreamMessagePack

class AuthenticatePlaySessionHandler(
    private val actors: ZLinkActorManager,
    private val channels: ZLinkClient,
    coroutines: ZLinkCoroutineRuntime,
) : ZLinkCoroutineSessionPacketHandler<ZLinkSessionContext>(coroutines, "AuthenticateReq") {
    override suspend fun handle(
        context: ZLinkSessionContext,
        header: ZLinkStreamHeader,
        payload: Message,
    ) {
        val request = ZLinkStreamMessagePack.decode(
            ZLinkStreamEncodedPayload(
                header.packetName(),
                payload,
                header.metadata(),
                connectorCodec(header),
            ),
            AuthenticateReq::class.java,
        )
        require(request.accessToken.isNotBlank()) { "access token is required" }
        val authenticated = channels
            .requestToChannel(SampleNames.ApiChannel, AuthenticatePlayerReq(request.accessToken))
            .timeout(SampleNames.RequestTimeout)
            .submit(AuthenticatePlayerRes::class.java)
            .await()
        val playActor = actors.getOrCreate(authenticated.actorId, SampleNames.PlayActor).await()
        val bound = context.actors().bind(playActor).await()
        context.client()
            .reply(AuthenticateRes(bound.actorId()))
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
