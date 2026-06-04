package systems.zlink.samples.kotlin.bingo.server.session.sessions.handlers

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.framework.streams.ZLinkSessionPacketHandler
import systems.zlink.framework.streams.ZLinkStreamHeader

class AuthenticateSessionHandler : ZLinkSessionPacketHandler<ZLinkSessionContext> {
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
        return context.client()
            .reply(actorId)
            .submitAsync()
    }
}
