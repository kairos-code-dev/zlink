package systems.zlink.samples.deliverydispatch.server.session.sessions;

import static systems.zlink.framework.ZLinkAwait.await;

import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionActor;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionPacketDispatcher;
import systems.zlink.framework.streams.ZLinkStreamError;
import systems.zlink.framework.streams.ZLinkStreamHeader;

public final class CustomerSession implements ZLinkSession {
    private final ZLinkSessionContext context;
    private final ZLinkSessionPacketDispatcher<ZLinkSessionContext> handlers;
    private final CustomerSessionDirectory sessions;

    public CustomerSession(
        ZLinkSessionContext context,
        ZLinkSessionPacketDispatcher<ZLinkSessionContext> handlers,
        CustomerSessionDirectory sessions) {
        this.context = context;
        this.handlers = handlers;
        this.sessions = sessions;
    }

    @Override
    public ZLinkSessionContext context() {
        return context;
    }

    @Override
    public void onConnected() {
        sessions.add(context);
    }

    @Override
    public void onDisconnected() {
        sessions.remove(context);
    }

    @Override
    public void onError(ZLinkStreamError error) {
    }

    @Override
    public void onDispatch(ZLinkStreamHeader header, Message payload) {
        boolean handled = await(handlers.tryHandleAsync(context, header, payload));
        if (handled) {
            return;
        }
        ZLinkSessionActor actor = requireSingleBoundActor(header.packetName());
        await(actor.relay(header, payload));
    }

    private ZLinkSessionActor requireSingleBoundActor(String packetName) {
        return switch (context.actors().bound().size()) {
            case 1 -> context.actors().bound().get(0);
            case 0 -> throw new IllegalStateException(
                "Client must subscribe before relaying packet '" + packetName + "'");
            default -> throw new IllegalStateException(
                "Exactly one actor must be bound before relaying packet '" + packetName + "'");
        };
    }
}
