package systems.zlink.e2e.discoveryregistryha.consumer;

import java.time.Duration;
import java.util.List;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.locations.ZLinkActorLocation;
import systems.zlink.framework.locations.ZLinkActorLocationFilter;
import systems.zlink.framework.locations.ZLinkActorLocationKey;
import systems.zlink.framework.locations.ZLinkLocationPage;
import systems.zlink.framework.locations.ZLinkLocationStore;
import systems.zlink.framework.locations.ZLinkLocationOwnerToken;
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

final class PollingOnlyLocationStore implements ZLinkLocationStore {
    private final ZLinkLocationStore inner;

    PollingOnlyLocationStore(ZLinkLocationStore inner) {
        this.inner = inner;
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> updatePeerAsync(
        ZLinkPeerLocation peer,
        ZLinkLocationWriteIntent intent) {
        return inner.updatePeerAsync(peer, intent);
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> removePeerAsync(
        ZLinkPeerLocationKey key,
        ZLinkLocationOwnerToken owner) {
        return inner.removePeerAsync(key, owner);
    }

    @Override
    public CompletionStage<List<ZLinkPeerLocation>> listPeerLocationsAsync(ZLinkPeerLocationFilter filter) {
        return inner.listPeerLocationsAsync(filter);
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> updateSpotAsync(
        ZLinkSpotLocation spot,
        ZLinkLocationWriteIntent intent) {
        return inner.updateSpotAsync(spot, intent);
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> removeSpotAsync(
        ZLinkSpotLocationKey key,
        ZLinkLocationOwnerToken owner) {
        return inner.removeSpotAsync(key, owner);
    }

    @Override
    public CompletionStage<ZLinkSpotLocation> resolveSpotAsync(ZLinkSpotLocationKey key) {
        return inner.resolveSpotAsync(key);
    }

    @Override
    public CompletionStage<ZLinkLocationPage<ZLinkSpotLocation>> listSpotLocationsAsync(
        ZLinkSpotLocationFilter filter,
        ZLinkPageRequest page) {
        return inner.listSpotLocationsAsync(filter, page);
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> updateActorAsync(
        ZLinkActorLocation actor,
        ZLinkLocationWriteIntent intent) {
        return inner.updateActorAsync(actor, intent);
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> removeActorAsync(
        ZLinkActorLocationKey key,
        ZLinkLocationOwnerToken owner) {
        return inner.removeActorAsync(key, owner);
    }

    @Override
    public CompletionStage<ZLinkActorLocation> resolveActorAsync(ZLinkActorLocationKey key) {
        return inner.resolveActorAsync(key);
    }

    @Override
    public CompletionStage<ZLinkLocationPage<ZLinkActorLocation>> listActorLocationsAsync(
        ZLinkActorLocationFilter filter,
        ZLinkPageRequest page) {
        return inner.listActorLocationsAsync(filter, page);
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> updateRouteAsync(
        ZLinkRouteLocation route,
        ZLinkLocationWriteIntent intent) {
        return inner.updateRouteAsync(route, intent);
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> removeRouteAsync(
        ZLinkRouteLocationKey key,
        ZLinkLocationOwnerToken owner) {
        return inner.removeRouteAsync(key, owner);
    }

    @Override
    public CompletionStage<Long> removeAllByOwnerAsync(String ownerId) {
        return inner.removeAllByOwnerAsync(ownerId);
    }

    @Override
    public CompletionStage<ZLinkRouteLocation> resolveRouteAsync(ZLinkRouteLocationKey key) {
        return inner.resolveRouteAsync(key);
    }

    @Override
    public CompletionStage<ZLinkLocationPage<ZLinkRouteLocation>> listRouteLocationsAsync(
        ZLinkRouteLocationFilter filter,
        ZLinkPageRequest page) {
        return inner.listRouteLocationsAsync(filter, page);
    }

    @Override
    public CompletionStage<ZLinkOwnerLeaseRenewal> renewOwnerLeaseAsync(
        String ownerId,
        RoutingId nodeRid,
        Duration leaseTtl) {
        return inner.renewOwnerLeaseAsync(ownerId, nodeRid, leaseTtl);
    }

    @Override
    public CompletionStage<Boolean> removeOwnerLeaseAsync(String ownerId) {
        return inner.removeOwnerLeaseAsync(ownerId);
    }

    @Override
    public CompletionStage<ZLinkOwnerLeaseSnapshot> listOwnerLeasesAsync() {
        return inner.listOwnerLeasesAsync();
    }
}
