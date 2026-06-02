package systems.zlink.samples.tictactoe.server.play.sessions;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkStreamError;

public final class PlaySession implements ZLinkSession {
    @Override
    public ZLinkSessionContext context() {
        return null;
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
