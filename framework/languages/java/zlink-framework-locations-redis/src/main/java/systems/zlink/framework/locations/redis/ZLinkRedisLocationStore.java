package systems.zlink.framework.locations.redis;

import java.time.Duration;
import java.util.List;
import java.util.Objects;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.locations.ZLinkActorLocation;
import systems.zlink.framework.locations.ZLinkActorLocationFilter;
import systems.zlink.framework.locations.ZLinkActorLocationKey;
import systems.zlink.framework.locations.ZLinkLocationChangeStampScope;
import systems.zlink.framework.locations.ZLinkLocationChangeStampStore;
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

public final class ZLinkRedisLocationStore implements
    ZLinkLocationStore,
    ZLinkLocationChangeStampStore,
    AutoCloseable {

    private final ZLinkRedisLocationConnection connection;
    private final ZLinkRedisLocationScriptsClient scripts;
    private final ZLinkRedisLocationRows rows;
    private final ZLinkRedisLocationStampReader stamps;

    public ZLinkRedisLocationStore(ZLinkRedisLocationOptions options) {
        ZLinkRedisLocationOptions validated = Objects.requireNonNull(options, "options");
        validated.validate();
        ZLinkRedisLocationKeys keys = new ZLinkRedisLocationKeys(validated.keyPrefix());
        this.connection = new ZLinkRedisLocationConnection(validated);
        this.scripts = new ZLinkRedisLocationScriptsClient(connection, keys);
        this.rows = new ZLinkRedisLocationRows(connection, keys);
        this.stamps = new ZLinkRedisLocationStampReader(connection, keys);
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> updatePeerAsync(
        ZLinkPeerLocation peer,
        ZLinkLocationWriteIntent intent) {
        return scripts.write(
            "peer",
            ZLinkRedisLocationKeyCodec.encodePeerKey(new ZLinkPeerLocationKey(
                peer.autoConnectType(), peer.meshName(), peer.role(), peer.nodeRid(), peer.endpoint())),
            peer.meshName(),
            peer.ownerId(),
            peer.generation(),
            ZLinkRedisLocationRowJson.serializePeer(peer),
            intent);
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> removePeerAsync(
        ZLinkPeerLocationKey key,
        systems.zlink.framework.locations.ZLinkLocationOwnerToken owner) {
        return scripts.remove("peer", ZLinkRedisLocationKeyCodec.encodePeerKey(key), key.meshName(), owner);
    }

    @Override
    public CompletionStage<List<ZLinkPeerLocation>> listPeerLocationsAsync(ZLinkPeerLocationFilter filter) {
        return rows.listRows("peer", ZLinkRedisLocationRowJson::deserializePeer)
            .thenApply(items -> items.stream()
                .filter(row -> ZLinkRedisLocationFilters.matches(row, filter))
                .toList());
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> updateSpotAsync(
        ZLinkSpotLocation spot,
        ZLinkLocationWriteIntent intent) {
        return scripts.write(
            "spot",
            ZLinkRedisLocationKeyCodec.encodeSpotKey(new ZLinkSpotLocationKey(spot.meshName(), spot.spotRid())),
            spot.meshName(),
            spot.ownerId(),
            spot.generation(),
            ZLinkRedisLocationRowJson.serializeSpot(spot),
            intent);
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> removeSpotAsync(
        ZLinkSpotLocationKey key,
        systems.zlink.framework.locations.ZLinkLocationOwnerToken owner) {
        return scripts.remove("spot", ZLinkRedisLocationKeyCodec.encodeSpotKey(key), key.meshName(), owner);
    }

    @Override
    public CompletionStage<ZLinkSpotLocation> resolveSpotAsync(ZLinkSpotLocationKey key) {
        return rows.resolve(
            "spot",
            ZLinkRedisLocationKeyCodec.encodeSpotKey(key),
            ZLinkRedisLocationRowJson::deserializeSpot);
    }

    @Override
    public CompletionStage<ZLinkLocationPage<ZLinkSpotLocation>> listSpotLocationsAsync(
        ZLinkSpotLocationFilter filter,
        ZLinkPageRequest page) {
        return rows.listPage(
            "spot",
            ZLinkRedisLocationRowJson::deserializeSpot,
            row -> ZLinkRedisLocationFilters.matches(row, filter),
            page);
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> updateActorAsync(
        ZLinkActorLocation actor,
        ZLinkLocationWriteIntent intent) {
        return scripts.write(
            "actor",
            ZLinkRedisLocationKeyCodec.encodeActorKey(new ZLinkActorLocationKey(actor.actorId())),
            null,
            actor.ownerId(),
            actor.generation(),
            ZLinkRedisLocationRowJson.serializeActor(actor),
            intent);
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> removeActorAsync(
        ZLinkActorLocationKey key,
        systems.zlink.framework.locations.ZLinkLocationOwnerToken owner) {
        return scripts.remove("actor", ZLinkRedisLocationKeyCodec.encodeActorKey(key), null, owner);
    }

    @Override
    public CompletionStage<ZLinkActorLocation> resolveActorAsync(ZLinkActorLocationKey key) {
        return rows.resolve(
            "actor",
            ZLinkRedisLocationKeyCodec.encodeActorKey(key),
            ZLinkRedisLocationRowJson::deserializeActor);
    }

    @Override
    public CompletionStage<ZLinkLocationPage<ZLinkActorLocation>> listActorLocationsAsync(
        ZLinkActorLocationFilter filter,
        ZLinkPageRequest page) {
        return rows.listPage(
            "actor",
            ZLinkRedisLocationRowJson::deserializeActor,
            row -> ZLinkRedisLocationFilters.matches(row, filter),
            page);
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> updateRouteAsync(
        ZLinkRouteLocation route,
        ZLinkLocationWriteIntent intent) {
        return scripts.write(
            "route",
            ZLinkRedisLocationKeyCodec.encodeRouteKey(new ZLinkRouteLocationKey(route.routeKind(), route.routeKey())),
            null,
            route.ownerId(),
            route.generation(),
            ZLinkRedisLocationRowJson.serializeRoute(route),
            intent);
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> removeRouteAsync(
        ZLinkRouteLocationKey key,
        systems.zlink.framework.locations.ZLinkLocationOwnerToken owner) {
        return scripts.remove("route", ZLinkRedisLocationKeyCodec.encodeRouteKey(key), null, owner);
    }

    @Override
    public CompletionStage<ZLinkRouteLocation> resolveRouteAsync(ZLinkRouteLocationKey key) {
        return rows.resolve(
            "route",
            ZLinkRedisLocationKeyCodec.encodeRouteKey(key),
            ZLinkRedisLocationRowJson::deserializeRoute);
    }

    @Override
    public CompletionStage<ZLinkLocationPage<ZLinkRouteLocation>> listRouteLocationsAsync(
        ZLinkRouteLocationFilter filter,
        ZLinkPageRequest page) {
        return rows.listPage(
            "route",
            ZLinkRedisLocationRowJson::deserializeRoute,
            row -> ZLinkRedisLocationFilters.matches(row, filter),
            page);
    }

    @Override
    public CompletionStage<ZLinkOwnerLeaseRenewal> renewOwnerLeaseAsync(
        String ownerId,
        RoutingId nodeRid,
        Duration leaseTtl) {
        return scripts.renewOwnerLeaseAsync(ownerId, nodeRid, leaseTtl);
    }

    @Override
    public CompletionStage<Boolean> removeOwnerLeaseAsync(String ownerId) {
        return scripts.removeOwnerLeaseAsync(ownerId);
    }

    @Override
    public CompletionStage<Long> removeAllByOwnerAsync(String ownerId) {
        return scripts.removeAllByOwnerAsync(ownerId);
    }

    @Override
    public CompletionStage<ZLinkOwnerLeaseSnapshot> listOwnerLeasesAsync() {
        return scripts.listOwnerLeasesAsync();
    }

    @Override
    public CompletionStage<Long> getChangeStampAsync(ZLinkLocationChangeStampScope scope) {
        return stamps.getChangeStampAsync(scope);
    }

    public CompletionStage<Void> closeAsync() {
        return connection.closeAsync();
    }

    @Override
    public void close() {
        closeAsync();
    }
}
