package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session.sessions.handlers

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.contracts.core.RoutingId
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.actors.ZLinkActorRef
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.framework.streams.ZLinkSessionPacketHandler
import systems.zlink.framework.streams.ZLinkStreamHeader
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session.sessions.PlayerSessionDirectory
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration.SampleNames

class AuthenticateSessionPacketHandler(
    private val channels: ZLinkClient,
) : ZLinkSessionPacketHandler<ZLinkSessionContext> {
    override fun packetName(): String = "AuthenticateReq"

    override fun handleAsync(
        context: ZLinkSessionContext,
        header: ZLinkStreamHeader,
        payload: Message,
    ): CompletionStage<Void> {
        val actorId = payload.toUtf8String().trim()
        if (actorId.isBlank()) {
            return CompletableFuture.failedFuture(
                IllegalArgumentException("actor id is required"),
            )
        }
        return channels.requestToChannel(SampleNames.ApiChannel, actorId)
            .packetName("AuthenticateActor")
            .submitAsync(String::class.java)
            .thenCompose { authenticatedActorId ->
                channels.requestToChannel(SampleNames.PlayChannel, authenticatedActorId)
                    .packetName("EnsurePlayerActor")
                    .submitAsync(String::class.java)
            }
            .thenCompose { snapshot -> context.actors().bindAsync(toActorRef(snapshot)) }
            .thenCompose {
                PlayerSessionDirectory.bind(actorId, context)
                context.client()
                    .reply(actorId)
                    .submitAsync()
            }
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
