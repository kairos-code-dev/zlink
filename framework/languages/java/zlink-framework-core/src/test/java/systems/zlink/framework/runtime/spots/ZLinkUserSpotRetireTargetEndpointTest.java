package systems.zlink.framework.runtime.spots;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.lang.reflect.Proxy;
import java.time.Instant;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ConcurrentHashMap;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkRelocationCancellation;
import systems.zlink.framework.locations.*;
import systems.zlink.framework.runtime.InMemoryRelocationStore;
import systems.zlink.framework.runtime.internal.locations
    .ZLinkAggregateRelocationCoordinator;
import systems.zlink.framework.runtime.locations.ZLinkAuthorityKeyCodec;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;

final class ZLinkUserSpotRetireTargetEndpointTest {
    private static final ZLinkStoreCancellation OPEN = () -> false;

    @Test
    void targetStaysInvisibleUntilExactAuthorityRootIsPublished() {
        RoutingId sourceRid = RoutingId.from("source-node");
        RoutingId targetRid = RoutingId.from("target-node");
        long targetNodeGeneration = 17;
        String spotId = "room-a";
        String authorityKey = ZLinkAuthorityKeyCodec.spot(spotId);
        AuthorityState authority = new AuthorityState();
        ZLinkAuthorityStore authorityStore = authority.proxy();
        var coordinator = new ZLinkAggregateRelocationCoordinator(
            authorityStore,
            new InMemoryRelocationStore());
        var envelope = new ZLinkUserSpotAggregateStagingOwner.Request(
            TestSpot.class,
            "room",
            spotId,
            3,
            new byte[] {1},
            false,
            new byte[0],
            List.of(),
            Map.of());
        UUID aggregateId = UUID.randomUUID();
        var prepared = coordinator.prepare(
                new ZLinkAggregateRelocationCoordinator.Request(
                    aggregateId,
                    1,
                    List.of(new ZLinkAggregateRelocationCoordinator.Participant(
                        authorityKey,
                        ZLinkPlacementObjectKind.USER_SPOT,
                        3,
                        5,
                        "version-1",
                        ZLinkAuthorityGenerationTransition.NEW_OWNER,
                        new byte[] {7},
                        new byte[0])),
                    ZLinkUserSpotRelocationEnvelope.encode(envelope),
                    new ZLinkMeshNodeDescriptorKey("mesh", targetRid),
                    targetNodeGeneration,
                    new ZLinkPlacementCapacityBundle(
                        0,
                        1,
                        Optional.of(new ZLinkSpotTypeCapacityDelta(
                            ZLinkPlacementObjectKind.USER_SPOT,
                            "room",
                            1))),
                    new ZLinkLocationOwnerToken("target-owner", 23)),
                OPEN)
            .toCompletableFuture().join();
        FakeStagingBackend backend = new FakeStagingBackend();
        var staging = new ZLinkUserSpotAggregateStagingOwner(backend);
        var endpoint = new ZLinkUserSpotRetireTargetEndpoint(
            targetRid,
            targetNodeGeneration,
            coordinator,
            staging,
            ignored -> TestSpot.class,
            (lane, record) -> CompletableFuture.completedFuture(null));
        var request = new ZLinkSpotRetireControl.StageRequest(
            new ZLinkSpotRetireControl.Fence(aggregateId, 1),
            sourceRid,
            11,
            "source-owner",
            12,
            targetRid,
            targetNodeGeneration,
            "target-owner",
            23,
            "mesh",
            spotId,
            "room",
            false,
            prepared.stored().reference(),
            prepared.stored().checksumCrc32c());

        endpoint.stage(request).toCompletableFuture().join();
        assertTrue(backend.live.isEmpty());

        coordinator.commit(prepared, OPEN).toCompletableFuture().join();
        endpoint.publish(request).toCompletableFuture().join();

        assertEquals(List.of("spot"), backend.live);
        assertEquals(List.of(
            "prepare", "restore", "publish", "timers"),
            backend.operations);
    }

    private static final class AuthorityState {
        private final Map<String, ZLinkAuthoritySnapshot> rows =
            new ConcurrentHashMap<>();
        private ZLinkAggregatePrepareRequest prepared;

        ZLinkAuthorityStore proxy() {
            return (ZLinkAuthorityStore) Proxy.newProxyInstance(
                ZLinkAuthorityStore.class.getClassLoader(),
                new Class<?>[] {ZLinkAuthorityStore.class},
                (proxy, method, arguments) -> switch (method.getName()) {
                    case "prepareAggregate" -> prepare(
                        (ZLinkAggregatePrepareRequest) arguments[0]);
                    case "commitAggregate" -> commit(
                        (ZLinkAggregateFence) arguments[0]);
                    case "abortAggregate" -> CompletableFuture.completedFuture(
                        ZLinkAggregateAbortResult.ABORTED);
                    case "read" -> CompletableFuture.completedFuture(
                        rows.getOrDefault(
                            (String) arguments[0],
                            null) == null
                            ? new ZLinkAuthorityMissing(Instant.now())
                            : rows.get((String) arguments[0]));
                    default -> throw new UnsupportedOperationException(
                        method.getName());
                });
        }

        private CompletionStage<ZLinkAggregatePrepareResult> prepare(
            ZLinkAggregatePrepareRequest request) {
            prepared = request;
            return CompletableFuture.completedFuture(
                new ZLinkAggregatePrepared(new ZLinkAggregateFence(
                    request.aggregateId(),
                    request.aggregateGeneration())));
        }

        private CompletionStage<ZLinkAggregateCommitResult> commit(
            ZLinkAggregateFence fence) {
            if (prepared == null
                || !prepared.aggregateId().equals(fence.aggregateId())
                || prepared.aggregateGeneration()
                    != fence.aggregateGeneration()) {
                return CompletableFuture.completedFuture(
                    ZLinkAggregateCommitResult.STALE);
            }
            for (var participant : prepared.participants()) {
                rows.put(participant.authorityKey(), new ZLinkAuthoritySnapshot(
                    "version-2",
                    participant.authorityPayload(),
                    3,
                    6,
                    prepared.targetOwner().ownerId(),
                    prepared.targetOwner().leaseGeneration(),
                    new ZLinkPlacementAllocation(
                        ZLinkPlacementAllocationState.ACTIVE,
                        ZLinkPlacementObjectKind.USER_SPOT,
                        "room",
                        prepared.targetDescriptor(),
                        prepared.targetDescriptorLifecycleGeneration(),
                        prepared.capacityBundle()),
                    Instant.now()));
            }
            return CompletableFuture.completedFuture(
                ZLinkAggregateCommitResult.COMMITTED);
        }
    }

    private static final class FakeStagingBackend
        implements ZLinkUserSpotAggregateStagingOwner.StagingBackend {
        private final java.util.ArrayList<String> operations =
            new java.util.ArrayList<>();
        private final java.util.ArrayList<String> live =
            new java.util.ArrayList<>();

        @Override public CompletionStage<Object> prepareSpot(
            ZLinkUserSpotAggregateStagingOwner.Request request) {
            operations.add("prepare");
            return CompletableFuture.completedFuture("spot");
        }

        @Override public CompletionStage<Void> restoreSpot(
            Object prepared,
            ZLinkUserSpotAggregateStagingOwner.Request request,
            ZLinkRelocationCancellation cancellation) {
            operations.add("restore");
            return CompletableFuture.completedFuture(null);
        }

        @Override public CompletionStage<Object> prepareActor(
            ZLinkUserSpotAggregateStagingOwner.ActorParticipant participant,
            ZLinkRelocationCancellation cancellation) {
            return CompletableFuture.completedFuture(participant.actorId());
        }

        @Override public void publishSpot(Object prepared) {
            operations.add("publish");
            live.add((String) prepared);
        }

        @Override public void publishActor(Object prepared) { }
        @Override public void completeActor(Object prepared) { }
        @Override public void publishTimers(Object prepared) {
            operations.add("timers");
        }
        @Override public CompletionStage<Void> discardActor(Object prepared) {
            return CompletableFuture.completedFuture(null);
        }
        @Override public void discardSpot(Object prepared) { }
    }

    private static final class TestSpot implements ZLinkSpot<ZLinkActor> {
        @Override public ZLinkSpotContext context() { return null; }
        @Override public CompletionStage<Void> onJoinedActor(ZLinkActor actor) {
            return CompletableFuture.completedFuture(null);
        }
        @Override public CompletionStage<Void> onLeaveActor(ZLinkActor actor) {
            return CompletableFuture.completedFuture(null);
        }
    }
}
