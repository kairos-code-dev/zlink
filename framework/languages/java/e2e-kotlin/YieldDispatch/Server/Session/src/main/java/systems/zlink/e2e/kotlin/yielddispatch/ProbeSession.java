package systems.zlink.e2e.kotlin.yielddispatch;

import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionActor;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.framework.streams.ZLinkSessionPacketDispatcher;
import systems.zlink.framework.streams.ZLinkStreamError;

public final class ProbeSession implements ZLinkSession {
    private final ZLinkSessionContext context;
    private final ZLinkSessionPacketDispatcher<ZLinkSessionContext> handlers;

    public ProbeSession(
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
    }

    @Override
    public void onError(ZLinkStreamError error) {
        throw new IllegalStateException("stream error: " + error.error());
    }

    @Override
    public void onDispatch(
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload) {
        boolean handled = handlers.tryHandleAsync(context, dispatch, payload)
            .toCompletableFuture()
            .join();
        if (handled) {
            return;
        }
        requireActor(dispatch).relay(payload).toCompletableFuture().join();
    }

    private ZLinkSessionActor requireActor(ZLinkSessionDispatchContext dispatch) {
        String actorId = dispatch.metadata().get("actor-id");
        if (actorId != null && !actorId.isBlank()) {
            return context.actors().find(actorId)
                .orElseThrow(() -> new IllegalStateException("actor is not bound: " + actorId));
        }
        if (context.actors().bound().size() == 1) {
            return context.actors().bound().get(0);
        }
        throw new IllegalStateException("actor-id metadata is required");
    }
}
