package systems.zlink.e2e.kotlin.discoveryregistryha.consumer;

import java.time.Duration;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.TimeUnit;
import java.util.function.Supplier;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.locations.*;

public final class DelayableLocationStore implements ZLinkLocationStore {
    private final ZLinkLocationStore inner;
    private final LocationStoreDelayState delayState;

    public DelayableLocationStore(ZLinkLocationStore inner, LocationStoreDelayState delayState) {
        this.inner = inner;
        this.delayState = delayState;
    }

    private <T> CompletionStage<T> delayed(Supplier<CompletionStage<T>> action) {
        int delay = delayState.delayMilliseconds();
        if (delay <= 0) return action.get();
        return CompletableFuture.runAsync(
                () -> { },
                CompletableFuture.delayedExecutor(delay, TimeUnit.MILLISECONDS))
            .thenCompose(ignored -> action.get());
    }

    @Override public CompletionStage<ZLinkLocationWriteResult> updatePeer(ZLinkPeerLocation value, ZLinkLocationWriteIntent intent) { return delayed(() -> inner.updatePeer(value, intent)); }
    @Override public CompletionStage<ZLinkLocationWriteResult> removePeer(ZLinkPeerLocationKey key, ZLinkLocationOwnerToken owner) { return delayed(() -> inner.removePeer(key, owner)); }
    @Override public CompletionStage<List<ZLinkPeerLocation>> listPeerLocations(ZLinkPeerLocationFilter filter) { return delayed(() -> inner.listPeerLocations(filter)); }
    @Override public CompletionStage<ZLinkLocationWriteResult> updateSpot(ZLinkSpotLocation value, ZLinkLocationWriteIntent intent) { return delayed(() -> inner.updateSpot(value, intent)); }
    @Override public CompletionStage<ZLinkLocationWriteResult> removeSpot(ZLinkSpotLocationKey key, ZLinkLocationOwnerToken owner) { return delayed(() -> inner.removeSpot(key, owner)); }
    @Override public CompletionStage<ZLinkSpotLocation> resolveSpot(ZLinkSpotLocationKey key) { return delayed(() -> inner.resolveSpot(key)); }
    @Override public CompletionStage<ZLinkLocationPage<ZLinkSpotLocation>> listSpotLocations(ZLinkSpotLocationFilter filter, ZLinkPageRequest page) { return delayed(() -> inner.listSpotLocations(filter, page)); }
    @Override public CompletionStage<ZLinkLocationWriteResult> updateActor(ZLinkActorLocation value, ZLinkLocationWriteIntent intent) { return delayed(() -> inner.updateActor(value, intent)); }
    @Override public CompletionStage<ZLinkLocationWriteResult> removeActor(ZLinkActorLocationKey key, ZLinkLocationOwnerToken owner) { return delayed(() -> inner.removeActor(key, owner)); }
    @Override public CompletionStage<ZLinkActorLocation> resolveActor(ZLinkActorLocationKey key) { return delayed(() -> inner.resolveActor(key)); }
    @Override public CompletionStage<ZLinkLocationPage<ZLinkActorLocation>> listActorLocations(ZLinkActorLocationFilter filter, ZLinkPageRequest page) { return delayed(() -> inner.listActorLocations(filter, page)); }
    @Override public CompletionStage<ZLinkLocationWriteResult> updateRoute(ZLinkRouteLocation value, ZLinkLocationWriteIntent intent) { return delayed(() -> inner.updateRoute(value, intent)); }
    @Override public CompletionStage<ZLinkLocationWriteResult> removeRoute(ZLinkRouteLocationKey key, ZLinkLocationOwnerToken owner) { return delayed(() -> inner.removeRoute(key, owner)); }
    @Override public CompletionStage<Long> removeAllByOwner(String ownerId) { return delayed(() -> inner.removeAllByOwner(ownerId)); }
    @Override public CompletionStage<ZLinkRouteLocation> resolveRoute(ZLinkRouteLocationKey key) { return delayed(() -> inner.resolveRoute(key)); }
    @Override public CompletionStage<ZLinkLocationPage<ZLinkRouteLocation>> listRouteLocations(ZLinkRouteLocationFilter filter, ZLinkPageRequest page) { return delayed(() -> inner.listRouteLocations(filter, page)); }
    @Override public CompletionStage<ZLinkOwnerLeaseRenewal> renewOwnerLease(String ownerId, RoutingId nodeRid, Duration leaseTtl) { return delayed(() -> inner.renewOwnerLease(ownerId, nodeRid, leaseTtl)); }
    @Override public CompletionStage<Boolean> removeOwnerLease(String ownerId) { return delayed(() -> inner.removeOwnerLease(ownerId)); }
    @Override public CompletionStage<ZLinkOwnerLeaseSnapshot> listOwnerLeases() { return delayed(inner::listOwnerLeases); }
}
