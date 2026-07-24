package systems.zlink.framework.runtime.locations;

import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.locations.ZLinkActorLocation;
import systems.zlink.framework.locations.ZLinkActorLocationKey;
import systems.zlink.framework.locations.ZLinkLocationOptions;
import systems.zlink.framework.locations.ZLinkPageRequest;
import systems.zlink.framework.locations.ZLinkPeerLocation;
import systems.zlink.framework.locations.ZLinkPeerLocationFilter;
import systems.zlink.framework.locations.ZLinkPeerLocationResolver;
import systems.zlink.framework.locations.ZLinkRouteLocation;
import systems.zlink.framework.locations.ZLinkRouteLocationKey;
import systems.zlink.framework.locations.ZLinkSpotLocation;
import systems.zlink.framework.locations.ZLinkSpotLocationFilter;
import systems.zlink.framework.locations.ZLinkSpotLocationKey;
import systems.zlink.framework.spots.ZLinkSpotKind;

public final class ZLinkStoreLocationResolvers
    implements ZLinkPeerLocationResolver {
    private final ZLinkRegisteredLocationStores stores;
    private final ZLinkLiveLocationRows liveRows;

    public ZLinkStoreLocationResolvers(
        ZLinkRegisteredLocationStores stores,
        ZLinkLocationOptions options) {
        this(stores, ZLinkLiveLocationRows.create(stores, options));
    }

    public ZLinkStoreLocationResolvers(
        ZLinkRegisteredLocationStores stores,
        ZLinkLiveLocationRows liveRows) {
        this.stores = Objects.requireNonNull(stores, "stores");
        this.liveRows = Objects.requireNonNull(liveRows, "liveRows");
    }

    @Override
    public CompletionStage<List<ZLinkPeerLocation>> listLivePeers(ZLinkPeerLocationFilter filter) {
        return stores.peerStore().listPeerLocations(filter)
            .thenCompose(liveRows::filterLivePeers);
    }

    CompletionStage<ZLinkSpotLocation> resolveSpotRow(ZLinkSpotLocationKey key) {
        return liveRows.resolveLiveSpot(stores.spotStore().resolveSpot(key));
    }

    CompletionStage<ZLinkSpotLocation> resolveAnySpotRow(
        String spotId) {
        return resolveAnySpotRow(spotId, ZLinkPageRequest.firstPage());
    }

    private CompletionStage<ZLinkSpotLocation> resolveAnySpotRow(
        String spotId,
        ZLinkPageRequest page) {
        return stores.spotStore()
            .listSpotLocations(ZLinkSpotLocationFilter.all(), page)
            .thenCompose(result -> liveRows.filterLiveSpots(result.items())
                .thenCompose(live -> {
                    ZLinkSpotLocation found = live.stream()
                        .filter(row -> row.spotId().equals(spotId))
                        .findFirst()
                        .orElse(null);
                    if (found != null || result.continuationToken() == null
                        || result.continuationToken().isBlank()) {
                        return CompletableFuture.completedFuture(found);
                    }
                    return resolveAnySpotRow(
                        spotId,
                        new ZLinkPageRequest(
                            page.pageSize(),
                            result.continuationToken()));
                }));
    }

    public CompletionStage<ZLinkActorLocation> resolveActorRow(ZLinkActorLocationKey key) {
        if (stores.authorityStore() == null) {
            return legacyActorRow(key);
        }
        return stores.authorityStore()
            .read(ZLinkAuthorityKeyCodec.actor(key.actorId()), () -> false)
            .thenCompose(read -> {
                if (read instanceof systems.zlink.framework.locations
                    .ZLinkAuthoritySnapshot snapshot) {
                    var authority =
                        new ZLinkActorAuthorityPayloadCodec()
                            .decode(snapshot.payload())
                            .orElse(null);
                    if (authority != null
                        && authority.state()
                            == ZLinkActorAuthorityPayloadCodec.State.READY) {
                        return CompletableFuture.completedFuture(
                            new ZLinkActorLocation(
                                authority.actorId(),
                                authority.stableType(),
                                new systems.zlink.framework.actors.ActorRef(
                                    authority.nodeRid(),
                                    authority.actorId(),
                                    snapshot.objectGeneration()),
                                authority.nodeRid(),
                                authority.currentSpotKind() == 1
                                    ? systems.zlink.framework.spots
                                        .ZLinkSpotKind.ENTRY
                                    : systems.zlink.framework.spots
                                        .ZLinkSpotKind.USER,
                                authority.meshName(),
                                authority.currentSpotId(),
                                snapshot.ownerId(),
                                snapshot.authorityOwnerGeneration(),
                                snapshot.storeNow()));
                    }
                }
                return legacyActorRow(key);
            });
    }

    private CompletionStage<ZLinkActorLocation> legacyActorRow(
        ZLinkActorLocationKey key) {
        return liveRows.resolveLiveActor(
                stores.actorStore().resolveActor(key))
            .thenApply(row ->
                row == null || row.actorRef() == null ? null : row);
    }

    CompletionStage<ZLinkRouteLocation> resolveRouteRow(ZLinkRouteLocationKey key) {
        return liveRows.resolveLiveRoute(stores.routeStore().resolveRoute(key));
    }

    public static final class AddressResolvers {
        private final List<String> meshNames;
        private final Map<String, String> spotRouterChannels;
        private final ZLinkStoreLocationResolvers rows;

        public AddressResolvers(
            List<String> meshNames,
            ZLinkStoreLocationResolvers rows) {
            this(meshNames, Map.of(), rows);
        }

        public AddressResolvers(
            List<String> meshNames,
            Map<String, String> spotRouterChannels,
            ZLinkStoreLocationResolvers rows) {
            this.meshNames = List.copyOf(Objects.requireNonNull(meshNames, "meshNames"));
            this.spotRouterChannels = Map.copyOf(Objects.requireNonNull(spotRouterChannels, "spotRouterChannels"));
            this.rows = Objects.requireNonNull(rows, "rows");
        }

        public CompletionStage<ZLinkActorLocation> resolveActorSpotRow(String actorId) {
            return rows.resolveActorRow(new ZLinkActorLocationKey(actorId));
        }

        public CompletionStage<ZLinkSpotLocation> resolveSpotRow(
            String meshName,
            String spotId) {
            return rows.resolveSpotRow(new ZLinkSpotLocationKey(spotId));
        }

        public CompletionStage<ZLinkSpotLocation> resolveAnySpotRow(
            String spotId) {
            CompletionStage<ZLinkSpotLocation> result = CompletableFuture.completedFuture(null);
            for (String meshName : meshNames) {
                result = result.thenCompose(found -> found != null
                    ? CompletableFuture.completedFuture(found)
                    : rows.resolveSpotRow(new ZLinkSpotLocationKey(spotId)));
            }
            return result.thenCompose(found -> found != null
                ? CompletableFuture.completedFuture(found)
                : rows.resolveAnySpotRow(spotId));
        }

        public String routerChannelId(String meshName) {
            return spotRouterChannels.getOrDefault(meshName, meshName);
        }
    }
}
