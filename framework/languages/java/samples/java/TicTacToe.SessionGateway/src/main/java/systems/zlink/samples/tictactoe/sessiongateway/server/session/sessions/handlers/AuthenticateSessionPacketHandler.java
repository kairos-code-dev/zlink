package systems.zlink.samples.tictactoe.sessiongateway.server.session.sessions.handlers;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.actors.ZLinkActorRef;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionPacketHandler;
import systems.zlink.framework.streams.ZLinkStreamHeader;
import systems.zlink.samples.tictactoe.sessiongateway.shared.configuration.SampleNames;

public final class AuthenticateSessionPacketHandler
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
        ZLinkActorRef actorRef = new ZLinkActorRef(
            RoutingId.from(SampleNames.SessionRelayNode),
            actorId.trim(),
            1);
        return context.actors()
            .bindAsync(actorRef)
            .thenCompose(ignored -> context.client()
                .reply(actorId.trim())
                .submitAsync());
    }
}
