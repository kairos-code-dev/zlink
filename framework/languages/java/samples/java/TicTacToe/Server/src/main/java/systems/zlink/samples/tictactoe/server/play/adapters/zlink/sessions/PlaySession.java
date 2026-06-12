package systems.zlink.samples.tictactoe.server.play.adapters.zlink.sessions;

import static systems.zlink.framework.ZLinkAwait.await;

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
    public void onConnected() {
    }

    @Override
    public void onDisconnected() {
        context.actors().bound()
            .forEach(actor -> await(actor.notifyDisconnected()));
    }

    @Override
    public void onError(ZLinkStreamError error) {
    }

    @Override
    public void onDispatch(ZLinkStreamHeader header, Message payload) {
        boolean handled = await(handlers.tryHandleAsync(context, header, payload));
        if (!handled) {
            await(requireActor(header.packetName()).relay(header, payload));
        }
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
