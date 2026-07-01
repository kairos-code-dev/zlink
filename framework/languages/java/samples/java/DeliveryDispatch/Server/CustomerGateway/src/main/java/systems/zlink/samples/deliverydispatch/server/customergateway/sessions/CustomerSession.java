package systems.zlink.samples.deliverydispatch.server.customergateway.sessions;

import static systems.zlink.framework.ZLinkAwait.await;

import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionActor;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.framework.streams.ZLinkSessionPacketDispatcher;
import systems.zlink.framework.streams.ZLinkStreamError;

public final class CustomerSession implements ZLinkSession {
    private final ZLinkSessionContext context;
    private final ZLinkSessionPacketDispatcher<ZLinkSessionContext> handlers;

    public CustomerSession(
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
    public void onDispatch(ZLinkSessionDispatchContext dispatch, ZLinkMessage payload) {
        boolean handled = await(handlers.tryHandleAsync(context, dispatch, payload));
        if (handled) {
            return;
        }
        ZLinkSessionActor actor = switch (context.actors().bound().size()) {
            case 1 -> context.actors().bound().get(0);
            case 0 -> throw new IllegalStateException(
                "Client must subscribe before relaying packet '" + dispatch.packetName() + "'");
            default -> throw new IllegalStateException(
                "Exactly one customer actor must be bound before relaying packet '" + dispatch.packetName() + "'");
        };
        await(actor.relay(payload));
    }
}
