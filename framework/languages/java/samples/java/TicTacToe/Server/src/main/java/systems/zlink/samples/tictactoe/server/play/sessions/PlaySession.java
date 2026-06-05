package systems.zlink.samples.tictactoe.server.play.sessions;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionActor;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkStreamError;
import systems.zlink.framework.streams.ZLinkStreamHeader;
import systems.zlink.samples.tictactoe.server.configuration.SampleNames;
import systems.zlink.samples.tictactoe.shared.contracts.AuthenticatePlayerReq;
import systems.zlink.samples.tictactoe.shared.contracts.AuthenticatePlayerRes;
import systems.zlink.samples.tictactoe.shared.contracts.AuthenticateReq;
import systems.zlink.samples.tictactoe.shared.contracts.AuthenticateRes;
import systems.zlink.stream.connector.ZLinkStreamEncodedPayload;
import systems.zlink.stream.connector.json.ZLinkStreamJson;

public final class PlaySession implements ZLinkSession {
    private final ZLinkSessionContext context;
    private final ZLinkActorManager actors;
    private final ZLinkClient channels;
    private String actorId;
    private ZLinkSessionActor actor;

    public PlaySession(
        ZLinkSessionContext context,
        ZLinkActorManager actors,
        ZLinkClient channels) {
        this.context = context;
        this.actors = actors;
        this.channels = channels;
    }

    @Override
    public ZLinkSessionContext context() {
        return context;
    }

    @Override
    public CompletionStage<Void> onConnectedAsync() {
        return CompletableFuture.completedFuture(null);
    }

    @Override
    public CompletionStage<Void> onDisconnectedAsync() {
        actorId = null;
        actor = null;
        return CompletableFuture.completedFuture(null);
    }

    @Override
    public CompletionStage<Void> onErrorAsync(ZLinkStreamError error) {
        return CompletableFuture.completedFuture(null);
    }

    @Override
    public CompletionStage<Void> onDispatchAsync(ZLinkStreamHeader header, Message payload) {
        try {
            if (header.packetName().equals("AuthenticateReq")) {
                AuthenticateReq request = decode(header, payload, AuthenticateReq.class);
                return channels
                    .requestToChannel(
                        SampleNames.ApiChannel,
                        new AuthenticatePlayerReq(request.accessToken()))
                    .packetName("AuthenticatePlayer")
                    .submitAsync(AuthenticatePlayerRes.class)
                    .thenCompose(authenticated -> {
                        actorId = authenticated.actorId();
                        return actors.getOrCreateAsync(actorId, SampleNames.PlayActor);
                    })
                    .thenCompose(context.actors()::bindAsync)
                    .thenCompose(bound -> {
                        actor = bound;
                        return context.client()
                            .reply(new AuthenticateRes(actorId))
                            .submitAsync();
                    });
            }
            return requireActor().relayAsync(header, payload);
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(ex);
        }
    }

    private ZLinkSessionActor requireActor() {
        if (actorId == null || actorId.isBlank() || actor == null) {
            throw new IllegalStateException("AuthenticateReq is required before play packets.");
        }
        return actor;
    }

    private static <T> T decode(ZLinkStreamHeader header, Message payload, Class<T> type) {
        return ZLinkStreamJson.decode(
            new ZLinkStreamEncodedPayload(header.packetName(), payload, header.metadata()),
            type);
    }
}
