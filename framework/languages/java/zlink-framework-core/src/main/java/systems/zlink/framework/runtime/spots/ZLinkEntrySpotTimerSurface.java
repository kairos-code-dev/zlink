package systems.zlink.framework.runtime.spots;

import java.time.Duration;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.spots.ZLinkSpotOutbound;
import systems.zlink.framework.spots.ZLinkTimer;
import systems.zlink.framework.spots.ZLinkTimerOptions;

final class ZLinkEntrySpotTimerSurface implements ZLinkSpot<ZLinkActor> {
    private final ZLinkSpotContext context;

    ZLinkEntrySpotTimerSurface(ZLinkEntrySpotContext entryContext) {
        this.context = new ZLinkEntrySpotBackedContext(entryContext);
    }

    @Override
    public ZLinkSpotContext context() {
        return context;
    }
}

record ZLinkEntrySpotBackedContext(
    ZLinkEntrySpotContext entryContext) implements ZLinkSpotContext {

    @Override
    public RoutingId spotRid() {
        return entryContext.spotRid();
    }

    @Override
    public RoutingId nodeRid() {
        return entryContext.nodeRid();
    }

    @Override
    public ZLinkSpotOutbound outbound() {
        return entryContext.outbound();
    }

    @Override
    public CompletionStage<Void> leaveActor(ZLinkActor actor) {
        return systems.zlink.framework.ZLinkSubmitStage.completed();
    }

    @Override
    public CompletionStage<Boolean> close() {
        return CompletableFuture.completedFuture(false);
    }

    @Override
    public CompletionStage<ZLinkTimer> addTimer(
        String name,
        Duration period,
        Class<?> handlerType,
        ZLinkTimerOptions options) {
        return entryContext.addTimer(name, period, handlerType, options);
    }
}
