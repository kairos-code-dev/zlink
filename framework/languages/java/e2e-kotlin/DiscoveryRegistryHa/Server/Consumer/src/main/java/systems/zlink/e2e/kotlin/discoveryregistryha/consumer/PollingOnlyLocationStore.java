package systems.zlink.e2e.kotlin.discoveryregistryha.consumer;

import java.time.Duration;
import java.util.List;
import java.util.concurrent.CompletionStage;
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

final class PollingOnlyLocationStore implements ZLinkLocationStore {
    private final ZLinkLocationStore inner;

    PollingOnlyLocationStore(ZLinkLocationStore inner) {
        this.inner = inner;
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> updatePeer(
        ZLinkPeerLocation peer,
        ZLinkLocationWriteIntent intent) {
        return inner.updatePeer(peer, intent);
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> removePeer(
        ZLinkPeerLocationKey key,
        ZLinkLocationOwnerToken owner) {
        return inner.removePeer(key, owner);
    }

    @Override
    public CompletionStage<List<ZLinkPeerLocation>> listPeerLocations(ZLinkPeerLocationFilter filter) {
        return inner.listPeerLocations(filter);
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> updateSpot(
        ZLinkSpotLocation spot,
        ZLinkLocationWriteIntent intent) {
        return inner.updateSpot(spot, intent);
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> removeSpot(
        ZLinkSpotLocationKey key,
        ZLinkLocationOwnerToken owner) {
        return inner.removeSpot(key, owner);
    }

    @Override
    public CompletionStage<ZLinkSpotLocation> resolveSpot(ZLinkSpotLocationKey key) {
        return inner.resolveSpot(key);
    }

    @Override
    public CompletionStage<ZLinkLocationPage<ZLinkSpotLocation>> listSpotLocations(
        ZLinkSpotLocationFilter filter,
        ZLinkPageRequest page) {
        return inner.listSpotLocations(filter, page);
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> updateActor(
        ZLinkActorLocation actor,
        ZLinkLocationWriteIntent intent) {
        return inner.updateActor(actor, intent);
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> removeActor(
        ZLinkActorLocationKey key,
        ZLinkLocationOwnerToken owner) {
        return inner.removeActor(key, owner);
    }

    @Override
    public CompletionStage<ZLinkActorLocation> resolveActor(ZLinkActorLocationKey key) {
        return inner.resolveActor(key);
    }

    @Override
    public CompletionStage<ZLinkLocationPage<ZLinkActorLocation>> listActorLocations(
        ZLinkActorLocationFilter filter,
        ZLinkPageRequest page) {
        return inner.listActorLocations(filter, page);
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> updateRoute(
        ZLinkRouteLocation route,
        ZLinkLocationWriteIntent intent) {
        return inner.updateRoute(route, intent);
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> removeRoute(
        ZLinkRouteLocationKey key,
        ZLinkLocationOwnerToken owner) {
        return inner.removeRoute(key, owner);
    }

    @Override
    public CompletionStage<Long> removeAllByOwner(String ownerId) {
        return inner.removeAllByOwner(ownerId);
    }

    @Override
    public CompletionStage<ZLinkRouteLocation> resolveRoute(ZLinkRouteLocationKey key) {
        return inner.resolveRoute(key);
    }

    @Override
    public CompletionStage<ZLinkLocationPage<ZLinkRouteLocation>> listRouteLocations(
        ZLinkRouteLocationFilter filter,
        ZLinkPageRequest page) {
        return inner.listRouteLocations(filter, page);
    }

    @Override
    public CompletionStage<ZLinkOwnerLeaseRenewal> renewOwnerLease(
        String ownerId,
        RoutingId nodeRid,
        Duration leaseTtl) {
        return inner.renewOwnerLease(ownerId, nodeRid, leaseTtl);
    }

    @Override
    public CompletionStage<Boolean> removeOwnerLease(String ownerId) {
        return inner.removeOwnerLease(ownerId);
    }

    @Override
    public CompletionStage<ZLinkOwnerLeaseSnapshot> listOwnerLeases() {
        return inner.listOwnerLeases();
    }
}
