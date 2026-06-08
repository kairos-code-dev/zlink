package systems.zlink.samples.kotlin.tictactoe.server.play.sessions

import kotlinx.coroutines.future.await
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.actors.ZLinkActorManager
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.framework.kotlin.ZLinkCoroutineSession
import systems.zlink.framework.streams.ZLinkSessionActor
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
import systems.zlink.stream.connector.json.ZLinkStreamJson

class PlaySession(
    private val context: ZLinkSessionContext,
    private val actors: ZLinkActorManager,
    private val channels: ZLinkClient,
    coroutines: ZLinkCoroutineRuntime,
) : ZLinkCoroutineSession(coroutines) {
    private var actorId: String? = null
    private var actor: ZLinkSessionActor? = null

    override fun context(): ZLinkSessionContext = context

    override suspend fun onDisconnected() {
        actorId = null
        actor = null
    }

    override suspend fun onDispatch(header: ZLinkStreamHeader, payload: Message) {
        if (header.packetName() == "AuthenticateReq") {
            val request = decode(header, payload, AuthenticateReq::class.java)
            val authenticated = channels
                .requestToChannel(
                    SampleNames.ApiChannel,
                    AuthenticatePlayerReq(request.accessToken),
                )
                .submitAsync(AuthenticatePlayerRes::class.java)
                .await()
            val authenticatedActorId = authenticated.actorId
            actorId = authenticatedActorId
            val playActor = actors.getOrCreateAsync(authenticatedActorId, SampleNames.PlayActor).await()
            val bound = context.actors().bindAsync(playActor).await()
            actor = bound
            context.client().reply(AuthenticateRes(bound.actorId())).submitAsync().await()
        } else {
            requireActor().relayAsync(header, payload).await()
        }
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
