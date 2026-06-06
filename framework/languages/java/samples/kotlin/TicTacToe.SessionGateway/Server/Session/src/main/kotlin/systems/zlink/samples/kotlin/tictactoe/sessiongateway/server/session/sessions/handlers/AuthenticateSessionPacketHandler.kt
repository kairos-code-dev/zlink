package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session.sessions.handlers

import kotlinx.coroutines.future.await
import systems.zlink.contracts.core.RoutingId
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.actors.ZLinkActorRef
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.kotlin.ZLinkCoroutineSessionPacketHandler
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.framework.streams.ZLinkStreamHeader
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session.sessions.PlayerSessionDirectory
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration.SampleNames

class AuthenticateSessionPacketHandler(
    private val channels: ZLinkClient,
    private val sessions: PlayerSessionDirectory,
    coroutines: ZLinkCoroutineRuntime,
) : ZLinkCoroutineSessionPacketHandler<ZLinkSessionContext>(coroutines, "AuthenticateReq") {
    override suspend fun handle(
        context: ZLinkSessionContext,
        header: ZLinkStreamHeader,
        payload: Message,
    ) {
        val actorId = payload.toUtf8String().trim()
        if (actorId.isBlank()) {
            throw IllegalArgumentException("actor id is required")
        }
        val authenticatedActorId = channels.requestToChannel(SampleNames.ApiChannel, actorId)
            .packetName("AuthenticateActor")
            .submitAsync(String::class.java)
            .await()
        val snapshot = channels.requestToChannel(SampleNames.PlayChannel, authenticatedActorId)
            .packetName("EnsurePlayerActor")
            .submitAsync(String::class.java)
            .await()
        context.actors().bindAsync(toActorRef(snapshot)).await()
        sessions.bind(actorId, context)
        context.client()
            .reply(actorId)
            .submitAsync()
            .await()
    }

    private fun toActorRef(snapshot: String): ZLinkActorRef {
        val parts = snapshot.split("|")
        require(parts.size == 3) { "Invalid actor snapshot: $snapshot" }
        return ZLinkActorRef(
            RoutingId.fromHex(parts[0]),
            parts[1],
            parts[2].toLong(),
        )
    }
}
