package systems.zlink.framework.runtime.spots;

import java.time.Duration;
import java.time.Instant;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.locations.ZLinkLocationStore;
import systems.zlink.framework.locations.ZLinkLocationOptions;
import systems.zlink.framework.locations.ZLinkLocationStore;
import systems.zlink.framework.locations.ZLinkRelocationStore;
import systems.zlink.framework.locations.ZLinkStoreCancellation;
import systems.zlink.framework.runtime.actors.ZLinkActorRuntime;
import systems.zlink.framework.runtime.actors.ZLinkSessionRelocationPeerClient;
import systems.zlink.framework.runtime.internal.backend.ZLinkInternalMeshNode;
import systems.zlink.framework.runtime.internal.locations
    .ZLinkAggregateRelocationCoordinator;
import systems.zlink.framework.runtime.internal.locations
    .ZLinkRelocationPermitPool;
import systems.zlink.framework.runtime.internal.relocation
    .ZLinkRelocationAdapterRegistry;
import systems.zlink.framework.runtime.mesh.MeshNodeRegistration;

/** Process-wide production bridge from host Retire to per-Mesh Spot units. */
public final class ZLinkUserSpotRetireRuntime {
    private static final Duration CONTROL_TIMEOUT = Duration.ofSeconds(5);

    private final ZLinkSpotRuntime spots;
    private final ZLinkActorRuntime actors;
    private final Map<String, Lane> lanes;

    public ZLinkUserSpotRetireRuntime(
        ZLinkSpotRuntime spots,
        ZLinkActorRuntime actors,
        List<MeshNodeRegistration> registrations,
        Map<String, ZLinkInternalMeshNode> nodes,
        ZLinkLocationStore locations,
        ZLinkLocationStore authorities,
        ZLinkRelocationStore relocationStore,
        ZLinkLocationOptions options,
        ZLinkRelocationAdapterRegistry adapters) {
        this.spots = Objects.requireNonNull(spots, "spots");
        this.actors = Objects.requireNonNull(actors, "actors");
        Objects.requireNonNull(registrations, "registrations");
        Objects.requireNonNull(nodes, "nodes");
        var coordinator = new ZLinkAggregateRelocationCoordinator(
            Objects.requireNonNull(authorities, "authorities"),
            Objects.requireNonNull(relocationStore, "relocationStore"));
        var permits = new ZLinkRelocationPermitPool(
            Objects.requireNonNull(options, "options"));
        LinkedHashMap<String, Lane> configured = new LinkedHashMap<>();
        for (MeshNodeRegistration registration : registrations) {
            ZLinkInternalMeshNode node = nodes.get(registration.meshName());
            if (node == null || registration.relocatableSpotFactories().isEmpty()) {
                continue;
            }
            var staging = new ZLinkUserSpotAggregateStagingOwner(
                spots,
                adapters);
            var peerClient = new ZLinkSessionRelocationPeerClient(node);
            var relocationClient = ZLinkSpotRetireControl.client(node);
            var target = new ZLinkUserSpotRetireTargetEndpoint(
                node.status().routingId(),
                node.status().lifecycleGeneration(),
                coordinator,
                staging,
                stableType -> registration.relocatableSpotFactories()
                    .containsKey(stableType)
                        ? registration.relocatableSpotFactories()
                            .get(stableType).spotType()
                        : null,
                (lane, record) -> CompletableFuture.completedFuture(null),
                peerClient,
                CONTROL_TIMEOUT,
                request -> coordinator.normalizeCompletedAggregate(
                    request.participants().stream()
                        .map(value -> new ZLinkAggregateRelocationCoordinator
                            .ExpectedParticipant(
                                value.authorityKey(),
                                value.objectGeneration(),
                                value.sourceAuthorityOwnerGeneration()))
                        .toList(),
                    new systems.zlink.framework.locations.ZLinkAggregateFence(
                        request.fence().aggregateId(),
                        request.fence().aggregateGeneration()),
                    new systems.zlink.framework.locations
                        .ZLinkLocationOwnerToken(
                            request.targetOwnerId(),
                            request.targetOwnerLeaseGeneration()),
                    () -> false),
                spots,
                relocationClient,
                locations);
            ZLinkSpotRetireControl.install(node, target);
            node.setRelocationReplyRelayHandler(
                target::relayCanonicalReply);
            configured.put(registration.meshName(), new Lane(
                new ZLinkUserSpotRetireSourceBuilder(
                    registration.meshName(),
                    node.status().routingId(),
                    node.status().lifecycleGeneration(),
                    locations,
                    coordinator,
                    permits,
                    spots.spotLifecycle(),
                    spots.actorSessions(),
                    adapters,
                    registration.relocatableSpotFactories(),
                    registration.relocatableActorFactories(),
                    spots),
                new ZLinkUserSpotRetireScheduler(coordinator, staging),
                relocationClient));
        }
        lanes = Map.copyOf(configured);
    }

    public boolean supportsActiveInventory() {
        java.util.HashSet<String> aggregateActors = new java.util.HashSet<>();
        for (String spotId : spots.activeUserSpotIds()) {
            aggregateActors.addAll(spots.actorSessions().actorIdsInSpot(spotId));
            if (!lanes.containsKey(spots.userSpotMeshName(spotId))) {
                return false;
            }
        }
        return aggregateActors.containsAll(actors.activeActorIds());
    }

    public CompletionStage<Void> relocateAll(Instant deadline) {
        Objects.requireNonNull(deadline, "deadline");
        ZLinkStoreCancellation cancellation = () ->
            !Instant.now().isBefore(deadline);
        CompletionStage<Void> chain = CompletableFuture.completedFuture(null);
        for (String spotId : spots.activeUserSpotIds()) {
            chain = chain.thenCompose(ignored -> relocateOne(
                spotId,
                deadline,
                cancellation));
        }
        return chain;
    }

    private CompletionStage<Void> relocateOne(
        String spotId,
        Instant deadline,
        ZLinkStoreCancellation cancellation) {
        String meshName = spots.userSpotMeshName(spotId);
        Lane lane = lanes.get(meshName);
        if (lane == null) {
            return CompletableFuture.failedFuture(new IllegalStateException(
                "User Spot Retire lane is unavailable: " + meshName));
        }
        Duration timeout = Duration.between(Instant.now(), deadline);
        if (timeout.isZero() || timeout.isNegative()) {
            return CompletableFuture.failedFuture(new java.util.concurrent
                .TimeoutException("User Spot Retire deadline elapsed"));
        }
        return lane.source().prepare(spotId, cancellation)
            .thenCompose(source -> lane.scheduler().executeRemote(
                new ZLinkUserSpotRetireScheduler.RemoteRequest(
                    source,
                    lane.client(),
                    timeout.compareTo(CONTROL_TIMEOUT) < 0
                        ? timeout : CONTROL_TIMEOUT,
                    () -> source.cleanupLocal(deadline),
                    source.stagedRoot().request().root()),
                cancellation))
            .thenApply(ignored -> null);
    }

    private record Lane(
        ZLinkUserSpotRetireSourceBuilder source,
        ZLinkUserSpotRetireScheduler scheduler,
        ZLinkSpotRetireControl.Client client) {
    }
}
