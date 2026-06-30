package systems.zlink.e2e.yielddispatch.shared;

import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionActor;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.framework.streams.ZLinkSessionPacketDispatcher;
import systems.zlink.framework.streams.ZLinkStreamError;

public final class YieldSession implements ZLinkSession {
    private final ZLinkSessionContext context;
    private final ZLinkSessionPacketDispatcher<ZLinkSessionContext> handlers;
    private final EvidenceStore evidence;

    public YieldSession(
        ZLinkSessionContext context,
        ZLinkSessionPacketDispatcher<ZLinkSessionContext> handlers,
        EvidenceStore evidence) {
        this.context = context;
        this.handlers = handlers;
        this.evidence = evidence;
    }

    @Override
    public ZLinkSessionContext context() {
        return context;
    }

    @Override
    public void onConnected() {
        evidence.record("stream-connected", "session", context.sessionId());
    }

    @Override
    public void onDisconnected() {
        evidence.record("stream-disconnected", "session", context.sessionId());
    }

    @Override
    public void onError(ZLinkStreamError error) {
        evidence.record("stream-error", "session", error.error().name());
    }

    @Override
    public void onDispatch(
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload) {
        evidence.record("stream-inbound", "session", dispatch.packetName());
        boolean handled = handlers.tryHandleAsync(context, dispatch, payload)
            .toCompletableFuture()
            .join();
        if (handled) {
            return;
        }
        requireActor(dispatch).relay(payload).toCompletableFuture().join();
    }

    private ZLinkSessionActor requireActor(ZLinkSessionDispatchContext dispatch) {
        String actorId = dispatch.metadata().get(Contracts.ACTOR_ID_METADATA);
        if (actorId != null && !actorId.isBlank()) {
            return context.actors().find(actorId)
                .orElseThrow(() -> new IllegalStateException("actor is not bound: " + actorId));
        }
        return switch (context.actors().bound().size()) {
            case 1 -> context.actors().bound().get(0);
            case 0 -> throw new IllegalStateException("BindActorsReq is required before actor packet");
            default -> throw new IllegalStateException("actor-id metadata is required for multiple bound actors");
        };
    }
}
