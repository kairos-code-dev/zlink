package systems.zlink.framework.runtime.spots;

import java.time.Instant;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.Supplier;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendActorLifecycleEvent;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendActorRef;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendReceived;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendRecvMode;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendSpot;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendSpotDispatchEvent;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendSpotDispatchInfo;
import systems.zlink.framework.spots.ZLinkInstanceSpot;
import systems.zlink.framework.spots.ZLinkSpotCloseReason;
import systems.zlink.framework.spots.ZLinkSpotClosingContext;

final class ZLinkInstanceSpotActivation
    extends SpotActivationBase<DefaultInstanceSpotContext> {
    private final ZLinkInstanceSpot spot;
    private final AtomicBoolean resourcesClosed = new AtomicBoolean();
    private CompletionStage<Boolean> explicitClose;

    ZLinkInstanceSpotActivation(
        ZLinkSpotRuntime host,
        ZLinkSpotHandlerInvoker handlerInvoker,
        ZLinkInstanceSpot spot,
        ZLinkBackendSpot backendSpot,
        DefaultInstanceSpotContext context) {
        super(host, handlerInvoker, spot, backendSpot, context);
        this.spot = spot;
    }

    CompletionStage<Void> handleDispatchEvent(ZLinkBackendSpotDispatchInfo info) {
        if (host.isClosing()
            || info.event() != ZLinkBackendSpotDispatchEvent.ROUTED_READABLE) {
            return CompletableFuture.completedFuture(null);
        }
        return context.enqueueDispatch(this::drainRoutes);
    }

    private CompletionStage<Void> drainRoutes() {
        CompletionStage<Void> tail = CompletableFuture.completedFuture(null);
        while (true) {
            ZLinkBackendReceived received =
                backendSpot.recvRoute(ZLinkBackendRecvMode.DONT_WAIT);
            if (received == null) {
                return tail;
            }
            trackRouteReceived(received);
            ParsedPacket packet;
            try {
                packet = ZLinkSpotRuntime.parsePacket(received.parts());
            } catch (RuntimeException invalid) {
                closeRouteReceived(received);
                continue;
            }
            tail = tail.thenCompose(
                ignored -> dispatchSpotRouteHandler(received, packet));
        }
    }

    @Override
    CompletionStage<Void> appendSpotHandler(
        CompletionStage<Void> tail,
        Supplier<CompletionStage<Void>> operation) {
        return tail.thenCompose(ignored -> operation.get());
    }

    @Override
    CompletionStage<Void> appendActorLifecycle(
        CompletionStage<Void> tail,
        ZLinkBackendActorLifecycleEvent event,
        ZLinkBackendActorRef actorRef,
        ZLinkActor actor) {
        return CompletableFuture.failedFuture(new IllegalStateException(
            "Instance Spot does not own Actor lifecycle"));
    }

    void close(ZLinkSpotCloseReason reason, Instant deadline) {
        try {
            host.awaitClosing(context.runLifecycle(() -> spot.onClosing(
                new ZLinkSpotClosingContext(reason, deadline))));
        } finally {
            closeResources();
        }
    }

    synchronized CompletionStage<Boolean> closeExplicit() {
        if (explicitClose != null) {
            return explicitClose;
        }
        explicitClose = context.runLifecycleExecution(() -> spot.onClosing(
                new ZLinkSpotClosingContext(
                    ZLinkSpotCloseReason.EXPLICIT_CLOSE,
                    Instant.now())))
            .thenCompose(ignored -> host.completeInstanceSpotClose(this));
        return explicitClose;
    }

    void closeResources() {
        if (!resourcesClosed.compareAndSet(false, true)) {
            return;
        }
        backendSpot.closeInstanceSpot();
        closeActiveRouteReceives();
        context.closeResources();
    }

    @Override
    public void close() {
        close(ZLinkSpotCloseReason.EXPLICIT_CLOSE, Instant.now());
    }
}
