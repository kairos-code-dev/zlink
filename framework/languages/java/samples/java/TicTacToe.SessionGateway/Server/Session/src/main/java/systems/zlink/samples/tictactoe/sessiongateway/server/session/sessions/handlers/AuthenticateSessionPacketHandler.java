package systems.zlink.samples.tictactoe.sessiongateway.server.session.sessions.handlers;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.actors.ZLinkActorRef;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionPacketHandler;
import systems.zlink.framework.streams.ZLinkStreamHeader;
import systems.zlink.samples.tictactoe.sessiongateway.server.session.sessions.PlayerSessionDirectory;
import systems.zlink.samples.tictactoe.sessiongateway.shared.configuration.SampleNames;

public final class AuthenticateSessionPacketHandler
    implements ZLinkSessionPacketHandler<ZLinkSessionContext> {
    private final ZLinkClient channels;
    private final PlayerSessionDirectory sessions;

    public AuthenticateSessionPacketHandler(
        ZLinkClient channels,
        PlayerSessionDirectory sessions) {
        this.channels = channels;
        this.sessions = sessions;
    }

    @Override
    public String packetName() {
        return "AuthenticateReq";
    }

    @Override
    public CompletionStage<Void> handleAsync(
        ZLinkSessionContext context,
        ZLinkStreamHeader header,
        Message payload) {
        String requestedActorId = payload.toUtf8String().trim();
        if (requestedActorId.isBlank()) {
            return CompletableFuture.failedFuture(
                new IllegalArgumentException("actor id is required"));
        }
        return channels.requestToChannel(SampleNames.ApiChannel, requestedActorId)
            .packetName("AuthenticateActor")
            .submitAsync(String.class)
            .thenCompose(authenticatedActorId ->
                channels.requestToChannel(SampleNames.PlayChannel, authenticatedActorId)
                    .packetName("EnsurePlayerActor")
                    .submitAsync(String.class))
            .thenCompose(snapshot -> context.actors().bindAsync(toActorRef(snapshot)))
            .thenCompose(ignored -> {
                sessions.bind(requestedActorId, context);
                return context.client()
                    .reply(requestedActorId)
                    .submitAsync();
            });
    }

    private static ZLinkActorRef toActorRef(String snapshot) {
        String[] parts = snapshot.split("\\|", -1);
        if (parts.length != 3) {
            throw new IllegalArgumentException("Invalid actor snapshot: " + snapshot);
        }
        return new ZLinkActorRef(
            RoutingId.fromHex(parts[0]),
            parts[1],
            Long.parseLong(parts[2]));
    }
}
