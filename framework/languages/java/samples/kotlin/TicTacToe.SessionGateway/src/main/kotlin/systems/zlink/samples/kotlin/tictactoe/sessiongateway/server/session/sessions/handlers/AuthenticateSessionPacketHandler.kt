package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session.sessions.handlers

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.contracts.core.RoutingId
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.actors.ZLinkActorRef
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.framework.streams.ZLinkSessionPacketHandler
import systems.zlink.framework.streams.ZLinkStreamHeader
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration.SampleNames

class AuthenticateSessionPacketHandler : ZLinkSessionPacketHandler<ZLinkSessionContext> {
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
        val actorRef = ZLinkActorRef(
            RoutingId.from(SampleNames.SessionRelayNode),
            actorId,
            1,
        )
        return context.actors()
            .bindAsync(actorRef)
            .thenCompose {
                context.client()
                    .reply(actorId)
                    .submitAsync()
            }
    }
}
