package systems.zlink.framework.runtime.spots;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ThreadLocalRandom;
import java.util.concurrent.atomic.AtomicReference;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkRelocationCancellation;
import systems.zlink.framework.actors.ZLinkRelocationPolicy;
import systems.zlink.framework.locations.*;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntimeState;
import systems.zlink.framework.runtime.internal.locations
    .ZLinkAggregateRelocationCoordinator;
import systems.zlink.framework.runtime.internal.locations
    .ZLinkRelocationPermitPool;
import systems.zlink.framework.runtime.internal.relocation
    .ZLinkRelocationAdapterRegistry;
import systems.zlink.framework.runtime.locations.ZLinkAuthorityKeyCodec;
import systems.zlink.framework.runtime.locations
    .ZLinkServiceAuthorityPayloadCodec;
import systems.zlink.framework.runtime.mesh.MeshNodeRegistration;
import systems.zlink.framework.spots.ZLinkSpot;

/**
 * Builds the reversible source half of one User Spot Retire transaction from
 * live runtime objects and exact Location authority snapshots.
 */
final class ZLinkUserSpotRetireSourceBuilder {
    private static final long SNAPSHOT_RESERVATION_BYTES =
        64L * 1024 * 1024;
    private static final long ENVELOPE_RESERVATION_BYTES = 64L * 1024;
    private static final int PAGE_SIZE = 1000;
    private static final int MAX_DESCRIPTORS = 65_536;
    private static final byte[] SOURCE_CLEANUP_PENDING =
        "zlink.spot.source.pending.v1".getBytes(
            java.nio.charset.StandardCharsets.UTF_8);

    private final String meshName;
    private final RoutingId localNodeRid;
    private final long localNodeGeneration;
    private final ZLinkLocationStore locations;
    private final ZLinkAggregateRelocationCoordinator coordinator;
    private final ZLinkRelocationPermitPool permits;
    private final ZLinkSpotLifecycle spots;
    private final ZLinkActorSessionCoordinator actors;
    private final ZLinkRelocationAdapterRegistry adapters;
    private final Map<String, MeshNodeRegistration.RelocatableSpotFactory<?>>
        spotFactories;
    private final Map<String, MeshNodeRegistration.RelocatableActorFactory<?>>
        actorFactories;
    private final ZLinkServiceAuthorityPayloadCodec spotAuthorities =
        new ZLinkServiceAuthorityPayloadCodec();
    private final List<UnresolvedPreparation> unresolved =
        java.util.Collections.synchronizedList(new ArrayList<>());

    ZLinkUserSpotRetireSourceBuilder(
        String meshName,
        RoutingId localNodeRid,
        long localNodeGeneration,
        ZLinkLocationStore locations,
        ZLinkAggregateRelocationCoordinator coordinator,
        ZLinkRelocationPermitPool permits,
        ZLinkSpotLifecycle spots,
        ZLinkActorSessionCoordinator actors,
        ZLinkRelocationAdapterRegistry adapters,
        Map<String, MeshNodeRegistration.RelocatableSpotFactory<?>>
            spotFactories,
        Map<String, MeshNodeRegistration.RelocatableActorFactory<?>>
            actorFactories) {
        if (meshName == null || meshName.isBlank()) {
            throw new IllegalArgumentException("meshName is required");
        }
        if (localNodeGeneration <= 0) {
            throw new IllegalArgumentException(
                "local node generation must be positive");
        }
        this.meshName = meshName;
        this.localNodeRid = Objects.requireNonNull(
            localNodeRid, "localNodeRid");
        this.localNodeGeneration = localNodeGeneration;
        this.locations = Objects.requireNonNull(locations, "locations");
        this.coordinator = Objects.requireNonNull(coordinator, "coordinator");
        this.permits = Objects.requireNonNull(permits, "permits");
        this.spots = Objects.requireNonNull(spots, "spots");
        this.actors = Objects.requireNonNull(actors, "actors");
        this.adapters = Objects.requireNonNull(adapters, "adapters");
        this.spotFactories = Map.copyOf(
            Objects.requireNonNull(spotFactories, "spotFactories"));
        this.actorFactories = Map.copyOf(
            Objects.requireNonNull(actorFactories, "actorFactories"));
    }

    CompletionStage<PreparedSource> prepare(
        String spotId,
        ZLinkStoreCancellation cancellation) {
        Objects.requireNonNull(cancellation, "cancellation");
        if (cancellation.isCancellationRequested()) {
            return cancelled();
        }
        ZLinkSpot<?> spot = spots.spotFor(spotId);
        if (spot == null) {
            return failed(new IllegalStateException(
                "User Spot is not active locally: " + spotId));
        }
        List<String> actorIds = actors.actorIdsInSpot(spotId);
        if (actorIds.stream().anyMatch(actors::hasBoundSession)) {
            return failed(new IllegalStateException(
                "bound Session route relocation is not connected yet"));
        }
        return readInventory(spotId, actorIds, cancellation)
            .thenCompose(inventory -> {
                validateLiveInventory(spot, inventory);
                return listDescriptors(cancellation)
                    .thenApply(descriptors -> new Admission(
                        inventory,
                        selectTarget(inventory, descriptors)));
            })
            .thenCompose(admission -> sealAndCapture(
                spot,
                admission,
                cancellation));
    }

    int unresolvedPreparationCount() {
        return unresolved.size();
    }

    private CompletionStage<PreparedSource> sealAndCapture(
        ZLinkSpot<?> spot,
        Admission admission,
        ZLinkStoreCancellation cancellation) {
        AtomicReference<ZLinkRelocationPermitPool.Lease> acquired =
            new AtomicReference<>();
        int captures = snapshotCount(admission.inventory());
        ZLinkUserSpotRelocationBarrier barrier = spots.relocationBarrier(
            admission.inventory().spot().id(), actors);
        Optional<ZLinkUserSpotRelocationBarrier.Seal> sealed =
            barrier.trySeal(preview -> {
                if (!preview.participantActorIds().equals(
                        admission.inventory().actorIds())
                    || cancellation.isCancellationRequested()) {
                    return false;
                }
                long estimate = estimatePayload(preview, captures);
                ZLinkRelocationPermitPool.Lease lease = permits.tryAcquire(
                    ZLinkRelocationPermitPool.Request.outboundAggregate(
                        estimate,
                        captures,
                        true));
                acquired.set(lease);
                return lease != null;
            });
        if (sealed.isEmpty()) {
            close(acquired.get());
            return failed(new IllegalStateException(
                "User Spot relocation seal or permit was unavailable"));
        }
        ZLinkUserSpotRelocationBarrier.Seal seal = sealed.orElseThrow();
        ZLinkRelocationPermitPool.Lease lease = acquired.get();
        if (lease == null) {
            barrier.abort(seal);
            return failed(new IllegalStateException(
                "User Spot relocation permit was not acquired"));
        }
        return readInventory(
                admission.inventory().spot().id(),
                seal.participantActorIds(),
                cancellation)
            .thenCompose(current -> {
                if (!sameInventory(admission.inventory(), current)) {
                    return failed(new IllegalStateException(
                        "User Spot authority changed before Capture"));
                }
                return barrier.runCapture(
                    seal,
                    () -> capture(
                        spot,
                        current,
                        seal,
                        admission.target(),
                        cancellation));
            })
            .thenCompose(captured -> {
                byte[] root = ZLinkUserSpotRelocationEnvelope.encode(
                    captured.staging());
                if (!lease.tryShrinkPayload(root.length)) {
                    return failed(new IllegalStateException(
                        "captured relocation root exceeded its permit"));
                }
                return coordinator.prepare(
                        relocationRequest(
                            captured,
                            root,
                            admission.target()),
                        cancellation)
                    .thenApply(prepared -> new PreparedSource(
                        coordinator,
                        barrier,
                        seal,
                        lease,
                        captured,
                        prepared,
                        stageRequest(captured, prepared, admission.target())));
            })
            .exceptionallyCompose(failure -> {
                Throwable cause = unwrap(failure);
                if (cause instanceof ZLinkAggregateRelocationCoordinator
                    .PreparationOutcomeUnknownException) {
                    unresolved.add(new UnresolvedPreparation(
                        admission.inventory().spot().id(), seal, lease));
                    return failed(cause);
                }
                try {
                    barrier.abort(seal);
                } finally {
                    lease.close();
                }
                return failed(cause);
            });
    }

    private CompletionStage<Captured> capture(
        ZLinkSpot<?> spot,
        Inventory inventory,
        ZLinkUserSpotRelocationBarrier.Seal seal,
        ZLinkMeshNodeDescriptor target,
        ZLinkStoreCancellation cancellation) {
        ZLinkRelocationCancellation relocationCancellation =
            cancellation::isCancellationRequested;
        CompletionStage<byte[]> spotState = captureState(
            inventory.spot().stableType(),
            inventory.spot().policy(),
            () -> adapters.captureSpot(
                inventory.spot().stableType(),
                spot,
                relocationCancellation));
        return spotState.thenCompose(state -> {
            List<ZLinkUserSpotAggregateStagingOwner.ActorParticipant>
                participants = new ArrayList<>();
            CompletionStage<Void> chain = CompletableFuture.completedFuture(null);
            for (Owned actor : inventory.actors()) {
                chain = chain.thenCompose(ignored -> {
                    ZLinkActor live = actors.localActor(actor.id())
                        .orElseThrow(() -> new IllegalStateException(
                            "Actor disappeared during Capture: " + actor.id()));
                    return captureState(
                            actor.stableType(),
                            actor.policy(),
                            () -> adapters.captureActor(
                                actor.stableType(),
                                live,
                                relocationCancellation))
                        .thenAccept(actorState -> participants.add(
                            new ZLinkUserSpotAggregateStagingOwner.ActorParticipant(
                                actor.id(),
                                actor.stableType(),
                                actorState,
                                isSnapshot(actor.policy()),
                                actors.actorRef(actor.id()))));
                });
            }
            return chain.thenApply(ignored -> new Captured(
                inventory,
                new ZLinkUserSpotAggregateStagingOwner.Request(
                    spotFactories.get(inventory.spot().stableType()).spotType(),
                    inventory.spot().stableType(),
                    inventory.spot().id(),
                    inventory.spot().snapshot().objectGeneration(),
                    state,
                    isSnapshot(inventory.spot().policy()),
                    seal.timerEnvelope(),
                    participants,
                    seal.capturedRecords())));
        });
    }

    private static CompletionStage<byte[]> captureState(
        String stableType,
        ZLinkRelocationPolicy<?> policy,
        java.util.function.Supplier<CompletionStage<byte[]>> capture) {
        if (policy instanceof ZLinkRelocationPolicy.Recreate<?>) {
            return CompletableFuture.completedFuture(new byte[0]);
        }
        if (policy instanceof ZLinkRelocationPolicy.Snapshot<?>) {
            return Objects.requireNonNull(
                    capture.get(),
                    "Capture returned null")
                .thenApply(value -> Objects.requireNonNull(
                    value,
                    "Capture returned null state").clone());
        }
        return failed(new IllegalStateException(
            "relocation is disabled for stable type: " + stableType));
    }

    private CompletionStage<Inventory> readInventory(
        String spotId,
        List<String> actorIds,
        ZLinkStoreCancellation cancellation) {
        return readOwned(
                ZLinkAuthorityKeyCodec.spot(spotId),
                spotId,
                ZLinkPlacementObjectKind.USER_SPOT,
                cancellation)
            .thenCompose(spot -> {
                List<Owned> actorRows = new ArrayList<>();
                CompletionStage<Void> chain = CompletableFuture.completedFuture(null);
                for (String actorId : actorIds) {
                    chain = chain.thenCompose(ignored -> readOwned(
                            ZLinkAuthorityKeyCodec.actor(actorId),
                            actorId,
                            ZLinkPlacementObjectKind.ACTOR,
                            cancellation)
                        .thenAccept(actorRows::add));
                }
                return chain.thenApply(ignored -> new Inventory(
                    withPolicy(spot),
                    actorRows.stream().map(this::withPolicy).toList()));
            });
    }

    private CompletionStage<Owned> readOwned(
        String key,
        String id,
        ZLinkPlacementObjectKind kind,
        ZLinkStoreCancellation cancellation) {
        if (cancellation.isCancellationRequested()) {
            return cancelled();
        }
        return locations.read(key, cancellation).thenCompose(read -> {
            if (!(read instanceof ZLinkAuthoritySnapshot snapshot)
                || snapshot.allocation().state()
                    != ZLinkPlacementAllocationState.ACTIVE
                || snapshot.allocation().objectKind() != kind
                || !snapshot.allocation().descriptor().meshName()
                    .equals(meshName)
                || !snapshot.allocation().descriptor().rid()
                    .equals(localNodeRid)
                || snapshot.allocation().descriptorLifecycleGeneration()
                    != localNodeGeneration) {
                return failed(new IllegalStateException(
                    "relocation authority is not Ready on the source: " + key));
            }
            return CompletableFuture.completedFuture(new Owned(
                key,
                id,
                snapshot.allocation().stableType(),
                snapshot,
                null));
        });
    }

    private Owned withPolicy(Owned owned) {
        ZLinkRelocationPolicy<?> policy;
        if (owned.snapshot().allocation().objectKind()
            == ZLinkPlacementObjectKind.USER_SPOT) {
            var factory = spotFactories.get(owned.stableType());
            policy = factory == null ? null : factory.relocationPolicy();
        } else {
            var factory = actorFactories.get(owned.stableType());
            policy = factory == null ? null : factory.relocationPolicy();
        }
        if (policy == null
            || policy instanceof ZLinkRelocationPolicy.Disabled<?>) {
            throw new IllegalStateException(
                "relocation policy is unavailable: " + owned.stableType());
        }
        return new Owned(
            owned.key(),
            owned.id(),
            owned.stableType(),
            owned.snapshot(),
            policy);
    }

    private void validateLiveInventory(ZLinkSpot<?> spot, Inventory inventory) {
        var authority = spotAuthorities.decode(inventory.spot().snapshot().payload())
            .orElseThrow(() -> new IllegalStateException(
                "User Spot authority payload is invalid"));
        if (authority.kind() != ZLinkServiceAuthorityPayloadCodec.Kind.USER
            || authority.state()
                != ZLinkServiceAuthorityPayloadCodec.State.READY
            || !authority.spotId().equals(inventory.spot().id())
            || !authority.stableType().equals(inventory.spot().stableType())
            || !authority.meshName().equals(meshName)
            || !authority.nodeRid().equals(localNodeRid)
            || authority.nodeGeneration() != localNodeGeneration
            || !spotFactories.get(inventory.spot().stableType())
                .spotType().isInstance(spot)) {
            throw new IllegalStateException(
                "live User Spot does not match Location authority");
        }
        for (Owned actor : inventory.actors()) {
            if (!actors.actorType(actor.id()).equals(actor.stableType())
                || actors.actorRef(actor.id()).generation()
                    != actor.snapshot().objectGeneration()
                || !actor.snapshot().ownerId().equals(
                    inventory.spot().snapshot().ownerId())
                || actor.snapshot().ownerLeaseGeneration()
                    != inventory.spot().snapshot().ownerLeaseGeneration()) {
                throw new IllegalStateException(
                    "live Actor does not match aggregate authority: "
                        + actor.id());
            }
        }
    }

    private CompletionStage<List<ZLinkMeshNodeDescriptor>> listDescriptors(
        ZLinkStoreCancellation cancellation) {
        List<ZLinkMeshNodeDescriptor> result = new ArrayList<>();
        return listDescriptorPage(null, result, cancellation);
    }

    private CompletionStage<List<ZLinkMeshNodeDescriptor>> listDescriptorPage(
        String cursor,
        List<ZLinkMeshNodeDescriptor> result,
        ZLinkStoreCancellation cancellation) {
        if (cancellation.isCancellationRequested()) {
            return cancelled();
        }
        return locations.listMeshNodes(
                meshName,
                new ZLinkPageRequest(PAGE_SIZE, cursor))
            .thenCompose(page -> {
                result.addAll(page.items());
                if (result.size() > MAX_DESCRIPTORS) {
                    return failed(new IllegalStateException(
                        "MeshNode descriptor inventory exceeds its bound"));
                }
                String next = page.continuationToken();
                if (next == null || next.isBlank()) {
                    return CompletableFuture.completedFuture(List.copyOf(result));
                }
                if (next.equals(cursor)) {
                    return failed(new IllegalStateException(
                        "MeshNode descriptor cursor did not advance"));
                }
                return listDescriptorPage(next, result, cancellation);
            });
    }

    private ZLinkMeshNodeDescriptor selectTarget(
        Inventory inventory,
        List<ZLinkMeshNodeDescriptor> descriptors) {
        List<ZLinkMeshNodeDescriptor> candidates = descriptors.stream()
            .filter(candidate -> eligible(inventory, candidate))
            .toList();
        if (candidates.isEmpty()) {
            throw new IllegalStateException(
                "No eligible User Spot relocation target is Ready");
        }
        long total = candidates.stream()
            .mapToLong(ZLinkMeshNodeDescriptor::placementWeight)
            .reduce(0L, Math::addExact);
        long selected = ThreadLocalRandom.current().nextLong(total);
        for (ZLinkMeshNodeDescriptor candidate : candidates) {
            selected -= candidate.placementWeight();
            if (selected < 0) {
                return candidate;
            }
        }
        return candidates.getLast();
    }

    private boolean eligible(
        Inventory inventory,
        ZLinkMeshNodeDescriptor candidate) {
        if (!candidate.meshName().equals(meshName)
            || candidate.rid().equals(localNodeRid)
            || candidate.state() != ZLinkFrameworkRuntimeState.SERVING
            || candidate.objectRole() != ZLinkMeshNodeObjectRole.SERVER
            || candidate.placementWeight() <= 0
            || !hasCapacity(candidate.capacity().spots(), 1)
            || !hasCapacity(
                candidate.capacity().actors(),
                inventory.actors().size())
            || !hasTypeCapacity(
                candidate,
                ZLinkPlacementObjectKind.USER_SPOT,
                inventory.spot().stableType(),
                1)) {
            return false;
        }
        if (!hasCapability(candidate, inventory.spot())) {
            return false;
        }
        for (Owned actor : inventory.actors()) {
            if (!hasCapability(candidate, actor)) {
                return false;
            }
        }
        return true;
    }

    private static boolean hasCapability(
        ZLinkMeshNodeDescriptor target,
        Owned participant) {
        ZLinkPlacementObjectKind kind =
            participant.snapshot().allocation().objectKind();
        ZLinkObjectMaintenancePolicyKind policy = policyKind(
            participant.policy());
        return target.objectCapabilities().stream().anyMatch(capability ->
            capability.objectKind() == kind
                && capability.stableType().equals(participant.stableType())
                && capability.policy() == policy
                && capability.hasSnapshotAdapter() == isSnapshot(
                    participant.policy()));
    }

    private static boolean hasCapacity(ZLinkCapacityUsage usage, int required) {
        return usage.limit() == 0
            || (long) usage.active() + usage.reserved() + required
                <= usage.limit();
    }

    private static boolean hasTypeCapacity(
        ZLinkMeshNodeDescriptor target,
        ZLinkPlacementObjectKind kind,
        String stableType,
        int required) {
        return target.capacity().spotTypes().stream()
            .filter(type -> type.objectKind() == kind
                && type.stableType().equals(stableType))
            .findFirst()
            .map(type -> hasCapacity(type.usage(), required))
            .orElse(true);
    }

    private static boolean sameInventory(Inventory expected, Inventory actual) {
        if (!sameOwned(expected.spot(), actual.spot())
            || !expected.actorIds().equals(actual.actorIds())) {
            return false;
        }
        for (int index = 0; index < expected.actors().size(); index++) {
            if (!sameOwned(expected.actors().get(index), actual.actors().get(index))) {
                return false;
            }
        }
        return true;
    }

    private static boolean sameOwned(Owned expected, Owned actual) {
        return expected.key().equals(actual.key())
            && expected.stableType().equals(actual.stableType())
            && expected.snapshot().storeVersion().equals(
                actual.snapshot().storeVersion())
            && expected.snapshot().objectGeneration()
                == actual.snapshot().objectGeneration()
            && expected.snapshot().authorityOwnerGeneration()
                == actual.snapshot().authorityOwnerGeneration()
            && expected.snapshot().ownerId().equals(actual.snapshot().ownerId())
            && expected.snapshot().ownerLeaseGeneration()
                == actual.snapshot().ownerLeaseGeneration()
            && Arrays.equals(
                expected.snapshot().payload(),
                actual.snapshot().payload());
    }

    private static int snapshotCount(Inventory inventory) {
        int count = isSnapshot(inventory.spot().policy()) ? 1 : 0;
        for (Owned actor : inventory.actors()) {
            if (isSnapshot(actor.policy())) {
                count++;
            }
        }
        return count;
    }

    private static long estimatePayload(
        ZLinkUserSpotRelocationBarrier.Preview preview,
        int snapshotCount) {
        long bytes = ENVELOPE_RESERVATION_BYTES;
        bytes = Math.addExact(bytes, preview.timerEnvelope().length);
        for (var records : preview.capturedRecords().values()) {
            for (var record : records) {
                bytes = Math.addExact(bytes, 16L + record.payload().length);
            }
        }
        return Math.addExact(
            bytes,
            Math.multiplyExact(SNAPSHOT_RESERVATION_BYTES, snapshotCount));
    }

    private static ZLinkObjectMaintenancePolicyKind policyKind(
        ZLinkRelocationPolicy<?> policy) {
        if (policy instanceof ZLinkRelocationPolicy.Snapshot<?>) {
            return ZLinkObjectMaintenancePolicyKind.SNAPSHOT;
        }
        if (policy instanceof ZLinkRelocationPolicy.Recreate<?>) {
            return ZLinkObjectMaintenancePolicyKind.RECREATE;
        }
        return ZLinkObjectMaintenancePolicyKind.DISABLED;
    }

    private static boolean isSnapshot(ZLinkRelocationPolicy<?> policy) {
        return policy instanceof ZLinkRelocationPolicy.Snapshot<?>;
    }

    private static ZLinkAggregateRelocationCoordinator.Request
        relocationRequest(
            Captured captured,
            byte[] root,
            ZLinkMeshNodeDescriptor target) {
        UUID aggregateId = UUID.randomUUID();
        List<ZLinkAggregateRelocationCoordinator.Participant> participants =
            new ArrayList<>();
        participants.add(participant(
            captured.inventory().spot(),
            SOURCE_CLEANUP_PENDING));
        for (Owned actor : captured.inventory().actors()) {
            participants.add(participant(actor, new byte[0]));
        }
        return new ZLinkAggregateRelocationCoordinator.Request(
            aggregateId,
            1,
            participants,
            root,
            new ZLinkMeshNodeDescriptorKey(target.meshName(), target.rid()),
            target.lifecycleGeneration(),
            new ZLinkPlacementCapacityBundle(
                captured.inventory().actors().size(),
                1,
                Optional.of(new ZLinkSpotTypeCapacityDelta(
                    ZLinkPlacementObjectKind.USER_SPOT,
                    captured.inventory().spot().stableType(),
                    1))),
            new ZLinkLocationOwnerToken(
                target.ownerId(),
                target.leaseGeneration()));
    }

    private static ZLinkAggregateRelocationCoordinator.Participant participant(
        Owned owned,
        byte[] completion) {
        return new ZLinkAggregateRelocationCoordinator.Participant(
            owned.key(),
            owned.snapshot().allocation().objectKind(),
            owned.snapshot().objectGeneration(),
            owned.snapshot().authorityOwnerGeneration(),
            owned.snapshot().storeVersion(),
            ZLinkAuthorityGenerationTransition.NEW_OWNER,
            owned.snapshot().payload(),
            completion);
    }

    private ZLinkSpotRetireControl.StageRequest stageRequest(
        Captured captured,
        ZLinkAggregateRelocationCoordinator.Prepared prepared,
        ZLinkMeshNodeDescriptor target) {
        ZLinkAuthoritySnapshot source =
            captured.inventory().spot().snapshot();
        return new ZLinkSpotRetireControl.StageRequest(
            new ZLinkSpotRetireControl.Fence(
                prepared.fence().aggregateId(),
                prepared.fence().aggregateGeneration()),
            localNodeRid,
            localNodeGeneration,
            source.ownerId(),
            source.ownerLeaseGeneration(),
            target.rid(),
            target.lifecycleGeneration(),
            target.ownerId(),
            target.leaseGeneration(),
            meshName,
            captured.inventory().spot().id(),
            captured.inventory().spot().stableType(),
            false,
            prepared.stored().reference(),
            prepared.stored().checksumCrc32c());
    }

    static final class PreparedSource {
        private final ZLinkAggregateRelocationCoordinator coordinator;
        private final ZLinkUserSpotRelocationBarrier barrier;
        private final ZLinkUserSpotRelocationBarrier.Seal seal;
        private final ZLinkRelocationPermitPool.Lease permit;
        private final Captured captured;
        private final ZLinkAggregateRelocationCoordinator.Prepared prepared;
        private final ZLinkSpotRetireControl.StageRequest stageRequest;
        private boolean sourceCommitted;
        private boolean terminal;

        private PreparedSource(
            ZLinkAggregateRelocationCoordinator coordinator,
            ZLinkUserSpotRelocationBarrier barrier,
            ZLinkUserSpotRelocationBarrier.Seal seal,
            ZLinkRelocationPermitPool.Lease permit,
            Captured captured,
            ZLinkAggregateRelocationCoordinator.Prepared prepared,
            ZLinkSpotRetireControl.StageRequest stageRequest) {
            this.coordinator = coordinator;
            this.barrier = barrier;
            this.seal = seal;
            this.permit = permit;
            this.captured = captured;
            this.prepared = prepared;
            this.stageRequest = stageRequest;
        }

        ZLinkAggregateRelocationCoordinator.Prepared prepared() {
            return prepared;
        }

        ZLinkSpotRetireControl.StageRequest stageRequest() {
            return stageRequest;
        }

        ZLinkUserSpotAggregateStagingOwner.Request stagingRequest() {
            return captured.staging();
        }

        synchronized ZLinkUserSpotRelocationBarrier.Committed
            commitSourceBarrier() {
            if (terminal || sourceCommitted) {
                throw new IllegalStateException(
                    "source relocation barrier is already terminal");
            }
            ZLinkUserSpotRelocationBarrier.Committed committed =
                barrier.commit(seal).orElseThrow(() ->
                    new IllegalStateException(
                        "source relocation barrier was lost"));
            sourceCommitted = true;
            return committed;
        }

        synchronized CompletionStage<Void> abortPrecommit() {
            if (terminal || sourceCommitted) {
                return failed(new IllegalStateException(
                    "committed source relocation cannot be aborted"));
            }
            return coordinator.abort(prepared).thenRun(() -> {
                synchronized (PreparedSource.this) {
                    if (!barrier.abort(seal)) {
                        throw new IllegalStateException(
                            "source relocation barrier was lost during abort");
                    }
                    terminal = true;
                    permit.close();
                }
            });
        }

        synchronized void releasePermitAfterCompletion() {
            if (!sourceCommitted || terminal) {
                throw new IllegalStateException(
                    "source relocation is not ready to release its permit");
            }
            terminal = true;
            permit.close();
        }
    }

    private record Admission(
        Inventory inventory,
        ZLinkMeshNodeDescriptor target) {
    }

    private record Inventory(Owned spot, List<Owned> actors) {
        private Inventory {
            actors = List.copyOf(actors);
        }

        List<String> actorIds() {
            return actors.stream().map(Owned::id).toList();
        }
    }

    private record Owned(
        String key,
        String id,
        String stableType,
        ZLinkAuthoritySnapshot snapshot,
        ZLinkRelocationPolicy<?> policy) {
    }

    private record Captured(
        Inventory inventory,
        ZLinkUserSpotAggregateStagingOwner.Request staging) {
    }

    private record UnresolvedPreparation(
        String spotId,
        ZLinkUserSpotRelocationBarrier.Seal seal,
        ZLinkRelocationPermitPool.Lease permit) {
    }

    private static void close(AutoCloseable closeable) {
        if (closeable == null) {
            return;
        }
        try {
            closeable.close();
        } catch (Exception ignored) {
        }
    }

    private static Throwable unwrap(Throwable failure) {
        Throwable current = failure;
        while (current instanceof CompletionException
            && current.getCause() != null) {
            current = current.getCause();
        }
        return current;
    }

    private static <T> CompletionStage<T> cancelled() {
        return failed(new java.util.concurrent.CancellationException());
    }

    private static <T> CompletionStage<T> failed(Throwable failure) {
        return CompletableFuture.failedFuture(failure);
    }
}
