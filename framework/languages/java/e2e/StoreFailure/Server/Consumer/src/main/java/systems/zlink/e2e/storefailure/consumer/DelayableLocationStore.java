package systems.zlink.e2e.storefailure.consumer;

import java.time.Duration;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.TimeUnit;
import java.util.function.Supplier;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.locations.ZLinkActorLocation;
import systems.zlink.framework.locations.ZLinkActorLocationFilter;
import systems.zlink.framework.locations.ZLinkActorLocationKey;
import systems.zlink.framework.locations.ZLinkLocationOwnerToken;
import systems.zlink.framework.locations.ZLinkLocationPage;
import systems.zlink.framework.locations.ZLinkLocationStore;
import systems.zlink.framework.locations.ZLinkLocationWriteIntent;
import systems.zlink.framework.locations.ZLinkLocationWriteResult;
import systems.zlink.framework.locations.ZLinkOwnerLeaseRenewal;
import systems.zlink.framework.locations.ZLinkOwnerLeaseSnapshot;
import systems.zlink.framework.locations.ZLinkPageRequest;
import systems.zlink.framework.locations.ZLinkPeerLocation;
import systems.zlink.framework.locations.ZLinkPeerLocationFilter;
import systems.zlink.framework.locations.ZLinkPeerLocationKey;
import systems.zlink.framework.locations.ZLinkRouteLocation;
import systems.zlink.framework.locations.ZLinkRouteLocationFilter;
import systems.zlink.framework.locations.ZLinkRouteLocationKey;
import systems.zlink.framework.locations.ZLinkSpotLocation;
import systems.zlink.framework.locations.ZLinkSpotLocationFilter;
import systems.zlink.framework.locations.ZLinkSpotLocationKey;

final class DelayableLocationStore implements ZLinkLocationStore {
    private final ZLinkLocationStore inner;
    private final LocationStoreDelayState delayState;

    DelayableLocationStore(ZLinkLocationStore inner, LocationStoreDelayState delayState) {
        this.inner = inner;
        this.delayState = delayState;
    }

    private <T> CompletionStage<T> delayed(Supplier<CompletionStage<T>> action) {
        int delay = delayState.delayMilliseconds();
        if (delay <= 0) {
            return action.get();
        }
        return CompletableFuture.runAsync(
                () -> { },
                CompletableFuture.delayedExecutor(delay, TimeUnit.MILLISECONDS))
            .thenCompose(ignored -> action.get());
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> updatePeer(
        ZLinkPeerLocation peer,
        ZLinkLocationWriteIntent intent) {
        return delayed(() -> inner.updatePeer(peer, intent));
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> removePeer(
        ZLinkPeerLocationKey key,
        ZLinkLocationOwnerToken owner) {
        return delayed(() -> inner.removePeer(key, owner));
    }

    @Override
    public CompletionStage<List<ZLinkPeerLocation>> listPeerLocations(ZLinkPeerLocationFilter filter) {
        return delayed(() -> inner.listPeerLocations(filter));
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> updateSpot(
        ZLinkSpotLocation spot,
        ZLinkLocationWriteIntent intent) {
        return delayed(() -> inner.updateSpot(spot, intent));
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> removeSpot(
        ZLinkSpotLocationKey key,
        ZLinkLocationOwnerToken owner) {
        return delayed(() -> inner.removeSpot(key, owner));
    }

    @Override
    public CompletionStage<ZLinkSpotLocation> resolveSpot(ZLinkSpotLocationKey key) {
        return delayed(() -> inner.resolveSpot(key));
    }

    @Override
    public CompletionStage<ZLinkLocationPage<ZLinkSpotLocation>> listSpotLocations(
        ZLinkSpotLocationFilter filter,
        ZLinkPageRequest page) {
        return delayed(() -> inner.listSpotLocations(filter, page));
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> updateActor(
        ZLinkActorLocation actor,
        ZLinkLocationWriteIntent intent) {
        return delayed(() -> inner.updateActor(actor, intent));
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> removeActor(
        ZLinkActorLocationKey key,
        ZLinkLocationOwnerToken owner) {
        return delayed(() -> inner.removeActor(key, owner));
    }

    @Override
    public CompletionStage<ZLinkActorLocation> resolveActor(ZLinkActorLocationKey key) {
        return delayed(() -> inner.resolveActor(key));
    }

    @Override
    public CompletionStage<ZLinkLocationPage<ZLinkActorLocation>> listActorLocations(
        ZLinkActorLocationFilter filter,
        ZLinkPageRequest page) {
        return delayed(() -> inner.listActorLocations(filter, page));
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> updateRoute(
        ZLinkRouteLocation route,
        ZLinkLocationWriteIntent intent) {
        return delayed(() -> inner.updateRoute(route, intent));
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> removeRoute(
        ZLinkRouteLocationKey key,
        ZLinkLocationOwnerToken owner) {
        return delayed(() -> inner.removeRoute(key, owner));
    }

    @Override
    public CompletionStage<Long> removeAllByOwner(String ownerId) {
        return delayed(() -> inner.removeAllByOwner(ownerId));
    }

    @Override
    public CompletionStage<ZLinkRouteLocation> resolveRoute(ZLinkRouteLocationKey key) {
        return delayed(() -> inner.resolveRoute(key));
    }

    @Override
    public CompletionStage<ZLinkLocationPage<ZLinkRouteLocation>> listRouteLocations(
        ZLinkRouteLocationFilter filter,
        ZLinkPageRequest page) {
        return delayed(() -> inner.listRouteLocations(filter, page));
    }

    @Override
    public CompletionStage<ZLinkOwnerLeaseRenewal> renewOwnerLease(
        String ownerId,
        RoutingId nodeRid,
        Duration leaseTtl) {
        return delayed(() -> inner.renewOwnerLease(ownerId, nodeRid, leaseTtl));
    }

    @Override
    public CompletionStage<Boolean> removeOwnerLease(String ownerId) {
        return delayed(() -> inner.removeOwnerLease(ownerId));
    }

    @Override
    public CompletionStage<ZLinkOwnerLeaseSnapshot> listOwnerLeases() {
        return delayed(inner::listOwnerLeases);
    }
}
