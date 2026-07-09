package systems.zlink.framework.spring.sessionfixtures;

import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.framework.streams.ZLinkSessionPacketDispatcher;
import systems.zlink.framework.streams.ZLinkStreamError;

public final class SubpackageDiscoveredPacketSession implements ZLinkSession {
    private final ZLinkSessionContext context;
    private final ZLinkSessionPacketDispatcher<ZLinkSessionContext> dispatcher;

    public SubpackageDiscoveredPacketSession(
        ZLinkSessionContext context,
        ZLinkSessionPacketDispatcher<ZLinkSessionContext> dispatcher) {
        this.context = context;
        this.dispatcher = dispatcher;
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
    }

    @Override
    public void onError(ZLinkStreamError error) {
    }

    @Override
    public void onDispatch(
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload) {
        dispatcher.tryHandleAsync(context, dispatch, payload)
            .toCompletableFuture()
            .join();
    }
}
