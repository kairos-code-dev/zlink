package systems.zlink.samples.tictactoe.sessiongateway.server.session.sessions;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionActor;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionPacketDispatcher;
import systems.zlink.framework.streams.ZLinkStreamError;
import systems.zlink.framework.streams.ZLinkStreamHeader;

public final class PlayerSession implements ZLinkSession {
    private final ZLinkSessionContext context;
    private final ZLinkSessionPacketDispatcher<ZLinkSessionContext> handlers;

    public PlayerSession(
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
        context.actors().bound()
            .forEach(actor -> PlayerSessionDirectory.unbind(actor.actorId(), context));
        return CompletableFuture.completedFuture(null);
    }

    @Override
    public CompletionStage<Void> onErrorAsync(ZLinkStreamError error) {
        return CompletableFuture.completedFuture(null);
    }

    @Override
    public CompletionStage<Void> onDispatchAsync(
        ZLinkStreamHeader header,
        Message payload) {
        return handlers.tryHandleAsync(context, header, payload)
            .thenCompose(handled -> {
                if (handled) {
                    return CompletableFuture.completedFuture(null);
                }
                ZLinkSessionActor actor = requireSingleBoundActor(header.packetName());
                return actor.relayAsync(header, payload);
            });
    }

    private ZLinkSessionActor requireSingleBoundActor(String packetName) {
        return switch (context.actors().bound().size()) {
            case 1 -> context.actors().bound().get(0);
            case 0 -> throw new IllegalStateException(
                "Client must authenticate before relaying packet '" + packetName + "'");
            default -> throw new IllegalStateException(
                "Exactly one actor must be bound before relaying packet '" + packetName + "'");
        };
    }
}
