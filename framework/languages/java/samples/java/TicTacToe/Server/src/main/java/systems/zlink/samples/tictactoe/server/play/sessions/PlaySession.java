package systems.zlink.samples.tictactoe.server.play.sessions;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionActor;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionPacketDispatcher;
import systems.zlink.framework.streams.ZLinkStreamError;
import systems.zlink.framework.streams.ZLinkStreamHeader;

public final class PlaySession implements ZLinkSession {
    private final ZLinkSessionContext context;
    private final ZLinkSessionPacketDispatcher<ZLinkSessionContext> handlers;

    public PlaySession(
        ZLinkSessionContext context,
        ZLinkSessionPacketDispatcher<ZLinkSessionContext> handlers) {
        this.context = context;
        this.handlers = handlers;
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
        return CompletableFuture.completedFuture(null);
    }

    @Override
    public CompletionStage<Void> onErrorAsync(ZLinkStreamError error) {
        return CompletableFuture.completedFuture(null);
    }

    @Override
    public CompletionStage<Void> onDispatchAsync(ZLinkStreamHeader header, Message payload) {
        return handlers.tryHandleAsync(context, header, payload)
            .thenCompose(handled -> handled
                ? CompletableFuture.completedFuture(null)
                : requireActor(header.packetName()).relayAsync(header, payload));
    }

    private ZLinkSessionActor requireActor(String packetName) {
        return switch (context.actors().bound().size()) {
            case 1 -> context.actors().bound().get(0);
            case 0 -> throw new IllegalStateException(
                "AuthenticateReq is required before play packet '" + packetName + "'");
            default -> throw new IllegalStateException(
                "Exactly one actor must be bound before play packet '" + packetName + "'");
        };
    }
}
