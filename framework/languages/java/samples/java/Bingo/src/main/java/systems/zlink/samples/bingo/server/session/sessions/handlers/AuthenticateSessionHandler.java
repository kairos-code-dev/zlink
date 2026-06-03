package systems.zlink.samples.bingo.server.session.sessions.handlers;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionPacketHandler;
import systems.zlink.framework.streams.ZLinkStreamHeader;

public final class AuthenticateSessionHandler
    implements ZLinkSessionPacketHandler<ZLinkSessionContext> {
    @Override
    public String packetName() {
        return "AuthenticateReq";
    }

    @Override
    public CompletionStage<Void> handleAsync(
        ZLinkSessionContext context,
        ZLinkStreamHeader header,
        Message payload) {
        String actorId = payload.toUtf8String();
        if (actorId == null || actorId.isBlank()) {
            return CompletableFuture.failedFuture(
                new IllegalArgumentException("actor id is required"));
        }
        return context.client()
            .reply(actorId.trim())
            .submitAsync();
    }
}
