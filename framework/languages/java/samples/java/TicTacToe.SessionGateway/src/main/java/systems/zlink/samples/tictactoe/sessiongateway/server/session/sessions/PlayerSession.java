package systems.zlink.samples.tictactoe.sessiongateway.server.session.sessions;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkStreamError;

public final class PlayerSession implements ZLinkSession {
    private final RecordingSessionActors actors;

    public PlayerSession() {
        this(new RecordingSessionActors());
    }

    public PlayerSession(RecordingSessionActors actors) {
        this.actors = actors;
    }

    @Override
    public ZLinkSessionContext context() {
        return new RecordingSessionContext(actors);
    }

    @Override
    public CompletionStage<Void> onConnectedAsync() {
        return CompletableFuture.completedFuture(null);
    }

    @Override
    public CompletionStage<Void> onDisconnectedAsync() {
        return CompletableFuture.completedFuture(null);
    }

    @Override
    public CompletionStage<Void> onErrorAsync(ZLinkStreamError error) {
        return CompletableFuture.completedFuture(null);
    }
}
