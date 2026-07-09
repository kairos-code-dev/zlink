package systems.zlink.e2e.discoveryregistryha.consumer;

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
    public CompletionStage<ZLinkLocationWriteResult> updatePeerAsync(
        ZLinkPeerLocation peer,
        ZLinkLocationWriteIntent intent) {
        return delayed(() -> inner.updatePeerAsync(peer, intent));
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> removePeerAsync(
        ZLinkPeerLocationKey key,
        ZLinkLocationOwnerToken owner) {
        return delayed(() -> inner.removePeerAsync(key, owner));
    }

    @Override
    public CompletionStage<List<ZLinkPeerLocation>> listPeerLocationsAsync(ZLinkPeerLocationFilter filter) {
        return delayed(() -> inner.listPeerLocationsAsync(filter));
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> updateSpotAsync(
        ZLinkSpotLocation spot,
        ZLinkLocationWriteIntent intent) {
        return delayed(() -> inner.updateSpotAsync(spot, intent));
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> removeSpotAsync(
        ZLinkSpotLocationKey key,
        ZLinkLocationOwnerToken owner) {
        return delayed(() -> inner.removeSpotAsync(key, owner));
    }

    @Override
    public CompletionStage<ZLinkSpotLocation> resolveSpotAsync(ZLinkSpotLocationKey key) {
        return delayed(() -> inner.resolveSpotAsync(key));
    }

    @Override
    public CompletionStage<ZLinkLocationPage<ZLinkSpotLocation>> listSpotLocationsAsync(
        ZLinkSpotLocationFilter filter,
        ZLinkPageRequest page) {
        return delayed(() -> inner.listSpotLocationsAsync(filter, page));
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> updateActorAsync(
        ZLinkActorLocation actor,
        ZLinkLocationWriteIntent intent) {
        return delayed(() -> inner.updateActorAsync(actor, intent));
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> removeActorAsync(
        ZLinkActorLocationKey key,
        ZLinkLocationOwnerToken owner) {
        return delayed(() -> inner.removeActorAsync(key, owner));
    }

    @Override
    public CompletionStage<ZLinkActorLocation> resolveActorAsync(ZLinkActorLocationKey key) {
        return delayed(() -> inner.resolveActorAsync(key));
    }

    @Override
    public CompletionStage<ZLinkLocationPage<ZLinkActorLocation>> listActorLocationsAsync(
        ZLinkActorLocationFilter filter,
        ZLinkPageRequest page) {
        return delayed(() -> inner.listActorLocationsAsync(filter, page));
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> updateRouteAsync(
        ZLinkRouteLocation route,
        ZLinkLocationWriteIntent intent) {
        return delayed(() -> inner.updateRouteAsync(route, intent));
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> removeRouteAsync(
        ZLinkRouteLocationKey key,
        ZLinkLocationOwnerToken owner) {
        return delayed(() -> inner.removeRouteAsync(key, owner));
    }

    @Override
    public CompletionStage<Long> removeAllByOwnerAsync(String ownerId) {
        return delayed(() -> inner.removeAllByOwnerAsync(ownerId));
    }

    @Override
    public CompletionStage<ZLinkRouteLocation> resolveRouteAsync(ZLinkRouteLocationKey key) {
        return delayed(() -> inner.resolveRouteAsync(key));
    }

    @Override
    public CompletionStage<ZLinkLocationPage<ZLinkRouteLocation>> listRouteLocationsAsync(
        ZLinkRouteLocationFilter filter,
        ZLinkPageRequest page) {
        return delayed(() -> inner.listRouteLocationsAsync(filter, page));
    }

    @Override
    public CompletionStage<ZLinkOwnerLeaseRenewal> renewOwnerLeaseAsync(
        String ownerId,
        RoutingId nodeRid,
        Duration leaseTtl) {
        return delayed(() -> inner.renewOwnerLeaseAsync(ownerId, nodeRid, leaseTtl));
    }

    @Override
    public CompletionStage<Boolean> removeOwnerLeaseAsync(String ownerId) {
        return delayed(() -> inner.removeOwnerLeaseAsync(ownerId));
    }

    @Override
    public CompletionStage<ZLinkOwnerLeaseSnapshot> listOwnerLeasesAsync() {
        return delayed(inner::listOwnerLeasesAsync);
    }
}
