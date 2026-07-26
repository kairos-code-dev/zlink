package systems.zlink.framework.runtime.locations;

import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.function.Function;
import java.util.function.Predicate;
import systems.zlink.framework.locations.ZLinkActorLocation;
import systems.zlink.framework.locations.ZLinkLocationOptions;
import systems.zlink.framework.locations.ZLinkPeerLocation;
import systems.zlink.framework.locations.ZLinkRouteLocation;
import systems.zlink.framework.locations.ZLinkSpotLocation;

public final class ZLinkLiveLocationRows {
    private final ZLinkOwnerLeaseTracker leaseTracker;
    private final ZLinkObservedLocationGenerations observed;
    private final java.time.Duration routeCacheMaxAge;

    public static ZLinkLiveLocationRows create(
        ZLinkRegisteredLocationStores stores,
        ZLinkLocationOptions options) {
        Objects.requireNonNull(stores, "stores");
        Objects.requireNonNull(options, "options");
        return new ZLinkLiveLocationRows(
            new ZLinkOwnerLeaseTracker(
                stores.ownerLeaseStore(),
                options.pollingInterval()),
            new ZLinkObservedLocationGenerations(),
            options.routeCacheMaxAge());
    }

    ZLinkLiveLocationRows(
        ZLinkOwnerLeaseTracker leaseTracker,
        ZLinkObservedLocationGenerations observed) {
        this(
            leaseTracker,
            observed,
            java.time.Duration.ofSeconds(15));
    }

    ZLinkLiveLocationRows(
        ZLinkOwnerLeaseTracker leaseTracker,
        ZLinkObservedLocationGenerations observed,
        java.time.Duration routeCacheMaxAge) {
        this.leaseTracker = Objects.requireNonNull(leaseTracker, "leaseTracker");
        this.observed = Objects.requireNonNull(observed, "observed");
        this.routeCacheMaxAge = Objects.requireNonNull(
            routeCacheMaxAge,
            "routeCacheMaxAge");
    }

    CompletionStage<List<ZLinkPeerLocation>> filterLivePeers(List<ZLinkPeerLocation> rows) {
        return filterLive(rows, ZLinkPeerLocation::ownerId, observed::acceptPeer);
    }

    CompletionStage<List<ZLinkSpotLocation>> filterLiveSpots(List<ZLinkSpotLocation> rows) {
        return filterLive(rows, ZLinkSpotLocation::ownerId, observed::acceptSpot);
    }

    CompletionStage<List<ZLinkActorLocation>> filterLiveActors(List<ZLinkActorLocation> rows) {
        return filterLive(rows, ZLinkActorLocation::ownerId, observed::acceptActor);
    }

    CompletionStage<List<ZLinkRouteLocation>> filterLiveRoutes(List<ZLinkRouteLocation> rows) {
        return filterLive(rows, ZLinkRouteLocation::ownerId, observed::acceptRoute);
    }

    CompletionStage<ZLinkSpotLocation> resolveLiveSpot(CompletionStage<ZLinkSpotLocation> rowStage) {
        return resolveLive(rowStage, ZLinkSpotLocation::ownerId, observed::acceptSpot);
    }

    CompletionStage<ZLinkActorLocation> resolveLiveActor(CompletionStage<ZLinkActorLocation> rowStage) {
        return resolveLive(rowStage, ZLinkActorLocation::ownerId, observed::acceptActor);
    }

    CompletionStage<ZLinkRouteLocation> resolveLiveRoute(CompletionStage<ZLinkRouteLocation> rowStage) {
        return resolveLive(rowStage, ZLinkRouteLocation::ownerId, observed::acceptRoute);
    }

    CompletionStage<java.time.Duration> ownerLeaseRemaining(
        String ownerId,
        long ownerLeaseGeneration) {
        return leaseTracker.remainingAdmissionLifetime(
            ownerId,
            ownerLeaseGeneration);
    }

    java.time.Duration routeCacheMaxAge() {
        return routeCacheMaxAge;
    }

    private <T> CompletionStage<List<T>> filterLive(
        List<T> rows,
        Function<T, String> ownerId,
        Predicate<T> acceptObserved) {
        CompletionStage<List<T>> result = CompletableFuture.completedFuture(new ArrayList<>());
        for (T row : rows) {
            result = result.thenCompose(live -> {
                if (!acceptObserved.test(row)) {
                    return CompletableFuture.completedFuture(live);
                }
                return leaseTracker.isOwnerLive(ownerId.apply(row)).thenApply(ownerLive -> {
                    if (ownerLive) {
                        live.add(row);
                    }
                    return live;
                });
            });
        }
        return result.thenApply(List::copyOf);
    }

    private <T> CompletionStage<T> resolveLive(
        CompletionStage<T> rowStage,
        Function<T, String> ownerId,
        Predicate<T> acceptObserved) {
        return rowStage.thenCompose(row -> {
            if (row == null || !acceptObserved.test(row)) {
                return CompletableFuture.completedFuture(null);
            }
            return leaseTracker.isOwnerLive(ownerId.apply(row))
                .thenApply(live -> live ? row : null);
        });
    }
}
