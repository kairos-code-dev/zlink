package systems.zlink.framework.runtime.spots;

import java.time.Duration;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse;
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

    @Override
    public CompletionStage<ZLinkSpotActorJoinResponse> onActorJoin(
        String actorId,
        ZLinkMessage request) {
        return CompletableFuture.completedFuture(ZLinkSpotActorJoinResponse.reject());
    }

    @Override
    public CompletionStage<Void> onJoinedActor(ZLinkActor actor) {
        return CompletableFuture.completedFuture(null);
    }

    @Override
    public CompletionStage<Void> onLeaveActor(ZLinkActor actor) {
        return CompletableFuture.completedFuture(null);
    }

    @Override
    public CompletionStage<Void> onDisconnectActor(ZLinkActor actor) {
        return CompletableFuture.completedFuture(null);
    }
}

record ZLinkEntrySpotBackedContext(
    ZLinkEntrySpotContext entryContext) implements ZLinkSpotContext {

    @Override
    public String spotId() {
        return entryContext.spotId();
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
        return java.util.concurrent.CompletableFuture.completedFuture(null);
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
