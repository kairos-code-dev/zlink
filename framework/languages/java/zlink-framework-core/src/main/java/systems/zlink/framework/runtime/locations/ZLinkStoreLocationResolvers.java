package systems.zlink.framework.runtime.locations;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.function.Function;
import systems.zlink.framework.locations.ActorSpotRefResolver;
import systems.zlink.framework.locations.SpotRef;
import systems.zlink.framework.locations.SpotRefResolver;
import systems.zlink.framework.locations.ZLinkActorLocation;
import systems.zlink.framework.locations.ZLinkActorLocationKey;
import systems.zlink.framework.locations.ZLinkLocationOptions;
import systems.zlink.framework.locations.ZLinkPeerLocation;
import systems.zlink.framework.locations.ZLinkPeerLocationFilter;
import systems.zlink.framework.locations.ZLinkPeerLocationResolver;
import systems.zlink.framework.locations.ZLinkRouteLocation;
import systems.zlink.framework.locations.ZLinkRouteLocationKey;
import systems.zlink.framework.locations.ZLinkSpotLocation;
import systems.zlink.framework.locations.ZLinkSpotLocationKey;
import systems.zlink.framework.spots.ZLinkSpotKind;

public final class ZLinkStoreLocationResolvers
    implements ZLinkPeerLocationResolver {
    private final ZLinkRegisteredLocationStores stores;
    private final ZLinkOwnerLeaseTracker leaseTracker;
    private final ZLinkObservedLocationGenerations observed = new ZLinkObservedLocationGenerations();

    public ZLinkStoreLocationResolvers(
        ZLinkRegisteredLocationStores stores,
        ZLinkLocationOptions options) {
        this.stores = Objects.requireNonNull(stores, "stores");
        Objects.requireNonNull(options, "options");
        this.leaseTracker = new ZLinkOwnerLeaseTracker(
            stores.ownerLeaseStore(),
            options.pollingInterval());
    }

    @Override
    public CompletionStage<List<ZLinkPeerLocation>> listLivePeersAsync(ZLinkPeerLocationFilter filter) {
        return stores.peerStore().listPeerLocationsAsync(filter)
            .thenCompose(rows -> filterLivePeers(rows.stream()
                .filter(observed::acceptPeer)
                .toList()));
    }

    CompletionStage<ZLinkSpotLocation> resolveSpotRowAsync(ZLinkSpotLocationKey key) {
        return resolveLiveAsync(
            stores.spotStore().resolveSpotAsync(key),
            ZLinkSpotLocation::ownerId,
            observed::acceptSpot);
    }

    public CompletionStage<ZLinkActorLocation> resolveActorRowAsync(ZLinkActorLocationKey key) {
        return resolveLiveAsync(
            stores.actorStore().resolveActorAsync(key),
            ZLinkActorLocation::ownerId,
            observed::acceptActor);
    }

    CompletionStage<ZLinkRouteLocation> resolveRouteRowAsync(ZLinkRouteLocationKey key) {
        return resolveLiveAsync(
            stores.routeStore().resolveRouteAsync(key),
            ZLinkRouteLocation::ownerId,
            observed::acceptRoute);
    }

    private CompletionStage<List<ZLinkPeerLocation>> filterLivePeers(List<ZLinkPeerLocation> rows) {
        CompletableFuture<List<ZLinkPeerLocation>> result =
            CompletableFuture.completedFuture(new ArrayList<>());
        for (ZLinkPeerLocation row : rows) {
            result = result.thenCompose(live -> leaseTracker.isOwnerLiveAsync(row.ownerId())
                .thenApply(ownerLive -> {
                    if (ownerLive) {
                        live.add(row);
                    }
                    return live;
                }));
        }
        return result.thenApply(List::copyOf);
    }

    private <TRow> CompletionStage<TRow> resolveLiveAsync(
        CompletionStage<TRow> rowStage,
        Function<TRow, String> ownerId,
        Function<TRow, Boolean> acceptObserved) {
        return rowStage.thenCompose(row -> {
            if (row == null || !acceptObserved.apply(row)) {
                return CompletableFuture.completedFuture(null);
            }
            return leaseTracker.isOwnerLiveAsync(ownerId.apply(row))
                .thenApply(live -> live ? row : null);
        });
    }

    public static final class AddressResolvers
        implements SpotRefResolver,
                   ActorSpotRefResolver {
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

        @Override
        public CompletionStage<SpotRef> resolveSpotRefAsync(
            String meshName,
            systems.zlink.contracts.core.RoutingId spotRid) {
            return rows.resolveSpotRowAsync(new ZLinkSpotLocationKey(meshName, spotRid))
                .thenApply(row -> row == null
                    ? null
                    : new SpotRef(row.meshName(), row.nodeRid(), row.spotRid()));
        }

        @Override
        public CompletionStage<SpotRef> resolveActorSpotRefAsync(
            String actorId) {
            return rows.resolveActorRowAsync(new ZLinkActorLocationKey(actorId))
                .thenApply(row -> {
                    if (row == null) {
                        return null;
                    }
                    systems.zlink.contracts.core.RoutingId spotRid =
                        row.locationKind() == ZLinkSpotKind.ENTRY || row.spotRid() == null
                            ? row.nodeRid()
                            : row.spotRid();
                    return new SpotRef(row.spotMeshName(), row.nodeRid(), spotRid);
                });
        }

        CompletionStage<ZLinkSpotLocation> resolveAnySpotRowAsync(
            systems.zlink.contracts.core.RoutingId spotRid) {
            CompletionStage<ZLinkSpotLocation> result = CompletableFuture.completedFuture(null);
            for (String meshName : meshNames) {
                result = result.thenCompose(found -> found != null
                    ? CompletableFuture.completedFuture(found)
                    : rows.resolveSpotRowAsync(new ZLinkSpotLocationKey(meshName, spotRid)));
            }
            return result;
        }

        String routerChannelId(String meshName) {
            return spotRouterChannels.getOrDefault(meshName, meshName);
        }
    }
}
