package systems.zlink.framework.runtime.spots;

import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ConcurrentHashMap;
import java.util.function.Function;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkRelocationCancellation;
import systems.zlink.framework.locations.ZLinkAggregateFence;
import systems.zlink.framework.locations.ZLinkLocationOwnerToken;
import systems.zlink.framework.locations.ZLinkStoreCancellation;
import systems.zlink.framework.runtime.internal.locations
    .ZLinkAggregateRelocationCoordinator;
import systems.zlink.framework.runtime.locations.ZLinkAuthorityKeyCodec;
import systems.zlink.framework.spots.ZLinkSpot;

/**
 * Target-side owner for relocation stage, authority-gated publication and
 * pre-commit discard. A staged aggregate stays outside live registries until
 * the Location Store exposes the exact root and target-owner fence.
 */
final class ZLinkUserSpotRetireTargetEndpoint
    implements ZLinkSpotRetireControl.TargetEndpoint {
    private static final ZLinkStoreCancellation OPEN = () -> false;
    private static final ZLinkRelocationCancellation NOT_CANCELLED =
        () -> false;

    private final RoutingId localNodeRid;
    private final long localNodeGeneration;
    private final ZLinkAggregateRelocationCoordinator coordinator;
    private final ZLinkUserSpotAggregateStagingOwner staging;
    private final Function<String, Class<? extends ZLinkSpot<?>>> spotTypes;
    private final ZLinkUserSpotAggregateStagingOwner.JournalReplayer replayer;
    private final ConcurrentHashMap<ZLinkSpotRetireControl.Fence, TargetStage>
        stages = new ConcurrentHashMap<>();

    ZLinkUserSpotRetireTargetEndpoint(
        RoutingId localNodeRid,
        long localNodeGeneration,
        ZLinkAggregateRelocationCoordinator coordinator,
        ZLinkUserSpotAggregateStagingOwner staging,
        Function<String, Class<? extends ZLinkSpot<?>>> spotTypes,
        ZLinkUserSpotAggregateStagingOwner.JournalReplayer replayer) {
        this.localNodeRid = Objects.requireNonNull(
            localNodeRid, "localNodeRid");
        if (localNodeGeneration <= 0) {
            throw new IllegalArgumentException(
                "local node generation must be positive");
        }
        this.localNodeGeneration = localNodeGeneration;
        this.coordinator = Objects.requireNonNull(coordinator, "coordinator");
        this.staging = Objects.requireNonNull(staging, "staging");
        this.spotTypes = Objects.requireNonNull(spotTypes, "spotTypes");
        this.replayer = Objects.requireNonNull(replayer, "replayer");
    }

    @Override
    public CompletionStage<Void> stage(
        ZLinkSpotRetireControl.StageRequest request) {
        validateTarget(request);
        return coordinator.readRoot(
                request.relocationReference(),
                request.relocationChecksum(),
                OPEN)
            .thenCompose(root -> {
                var decoded = ZLinkUserSpotRelocationEnvelope.decode(
                    root.payload(),
                    localNodeRid,
                    spotTypes);
                if (!decoded.spotId().equals(request.spotId())
                    || !decoded.spotStableType().equals(request.stableType())) {
                    return CompletableFuture.failedFuture(
                        new IllegalArgumentException(
                            "relocation root does not match the target Spot"));
                }
                return staging.stage(decoded, NOT_CANCELLED)
                    .thenAccept(value -> {
                        TargetStage previous = stages.putIfAbsent(
                            request.fence(),
                            new TargetStage(request, root.inventoryDigest(), value));
                        if (previous != null) {
                            staging.discard(value);
                            throw new IllegalStateException(
                                "relocation target stage already exists");
                        }
                    });
            });
    }

    @Override
    public CompletionStage<Void> publish(
        ZLinkSpotRetireControl.StageRequest request) {
        TargetStage target = requireStage(request);
        return coordinator.verifyPublishedRoot(
                ZLinkAuthorityKeyCodec.spot(request.spotId()),
                new ZLinkAggregateFence(
                    request.fence().aggregateId(),
                    request.fence().aggregateGeneration()),
                request.relocationReference(),
                request.relocationChecksum(),
                new ZLinkLocationOwnerToken(
                    request.targetOwnerId(),
                    request.targetOwnerLeaseGeneration()),
                target.inventoryDigest(),
                OPEN)
            .thenCompose(ignored ->
                staging.publishAndReplay(target.staged(), replayer))
            .thenRun(() -> stages.remove(request.fence(), target));
    }

    @Override
    public CompletionStage<Void> abort(
        ZLinkSpotRetireControl.StageRequest request) {
        TargetStage target = requireStage(request);
        return staging.discard(target.staged())
            .thenRun(() -> stages.remove(request.fence(), target));
    }

    private void validateTarget(ZLinkSpotRetireControl.StageRequest request) {
        if (!request.targetNodeRid().equals(localNodeRid)
            || request.targetNodeGeneration() != localNodeGeneration) {
            throw new IllegalArgumentException(
                "relocation target node fence is stale");
        }
    }

    private TargetStage requireStage(
        ZLinkSpotRetireControl.StageRequest request) {
        validateTarget(request);
        TargetStage target = stages.get(request.fence());
        if (target == null || !target.request().equals(request)) {
            throw new IllegalStateException(
                "relocation target stage is unavailable");
        }
        return target;
    }

    private record TargetStage(
        ZLinkSpotRetireControl.StageRequest request,
        byte[] inventoryDigest,
        ZLinkUserSpotAggregateStagingOwner.Staged staged) {
        private TargetStage {
            inventoryDigest = Objects.requireNonNull(
                inventoryDigest,
                "inventoryDigest").clone();
        }

        @Override public byte[] inventoryDigest() {
            return inventoryDigest.clone();
        }
    }
}
