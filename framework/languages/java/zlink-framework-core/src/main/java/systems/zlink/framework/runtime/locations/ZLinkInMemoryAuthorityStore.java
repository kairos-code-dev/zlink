package systems.zlink.framework.runtime.locations;

import java.nio.charset.StandardCharsets;
import java.time.Clock;
import java.time.Instant;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.Arrays;
import java.util.function.Predicate;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.locations.*;

final class ZLinkInMemoryAuthorityStore implements ZLinkAuthorityStore {
    private final Object gate = new Object();
    private final Clock clock;
    private final Predicate<ZLinkLocationOwnerToken> ownerLeaseIsLive;
    private final DescriptorLookup descriptorLookup;
    private final Map<String, Row> rows = new HashMap<>();
    private final Map<String, ReservationState> reservations = new HashMap<>();
    private final Map<String, CapacityState> capacityReservations =
        new HashMap<>();
    private final Map<UUID, AggregateState> aggregates = new HashMap<>();
    private final Map<String, byte[]> membershipMutations =
        new HashMap<>();
    private final Map<AllocationCounterKey, CapacityCounter>
        nodeAllocationCounters = new HashMap<>();
    private final Map<TypeAllocationCounterKey, CapacityCounter>
        typeAllocationCounters = new HashMap<>();
    private long revision;
    private long objectGeneration;
    private long authorityOwnerGeneration;

    ZLinkInMemoryAuthorityStore(
        Clock clock,
        Predicate<ZLinkLocationOwnerToken> ownerLeaseIsLive) {
        this(clock, ownerLeaseIsLive, (key, generation, owner) -> null);
    }

    ZLinkInMemoryAuthorityStore(
        Clock clock,
        Predicate<ZLinkLocationOwnerToken> ownerLeaseIsLive,
        DescriptorLookup descriptorLookup) {
        this.clock = clock;
        this.ownerLeaseIsLive = ownerLeaseIsLive;
        this.descriptorLookup = descriptorLookup;
    }

    @Override
    public CompletionStage<ZLinkAuthorityReadResult> read(
        String key,
        ZLinkStoreCancellation cancellation) {
        synchronized (gate) {
            Instant now = clock.instant();
            Row row = rows.get(key);
            return completed(row == null
                ? new ZLinkAuthorityMissing(now)
                : snapshot(row, now));
        }
    }

    @Override
    public CompletionStage<ZLinkAuthorityWriteResult> compareExchange(
        String key,
        ZLinkAuthorityExpectation expectation,
        ZLinkAuthorityMutation mutation,
        ZLinkStoreCancellation cancellation) {
        synchronized (gate) {
            Instant now = clock.instant();
            Row current = rows.get(key);
            if (!matches(current, expectation)) {
                return completed(new ZLinkAuthorityConflict(
                    current == null
                        ? new ZLinkAuthorityMissing(now)
                        : snapshot(current, now)));
            }
            if (mutation instanceof ZLinkAuthorityDelete) {
                if (current == null
                    || current.allocation.state()
                        != ZLinkPlacementAllocationState.ACTIVE) {
                    return completed(new ZLinkAuthorityConflict(
                        current == null
                            ? new ZLinkAuthorityMissing(now)
                            : snapshot(current, now)));
                }
                if (!ownerLeaseIsLive.test(current.owner)) {
                    return completed(new ZLinkAuthorityConflict(
                        snapshot(current, now)));
                }
                adjustActive(current.allocation, -current.allocation.capacityDelta());
                rows.remove(key);
                return completed(new ZLinkAuthorityDeleted(
                    nextVersion(),
                    now));
            }
            ZLinkAuthorityPut put = (ZLinkAuthorityPut) mutation;
            if (current == null
                || current.allocation.state()
                    != ZLinkPlacementAllocationState.ACTIVE) {
                return completed(new ZLinkAuthorityConflict(
                    current == null
                        ? new ZLinkAuthorityMissing(now)
                        : snapshot(current, now)));
            }
            ZLinkLocationOwnerToken targetOwner = put.targetOwner()
                .orElse(current.owner);
            if (targetOwner == null
                || !ownerLeaseIsLive.test(targetOwner)) {
                return completed(new ZLinkAuthorityConflict(
                    snapshot(current, now)));
            }
            boolean changesOwner = put.generationTransition()
                == ZLinkAuthorityGenerationTransition.NEW_OWNER;
            boolean relocatesOwner = put.generationTransition()
                == ZLinkAuthorityGenerationTransition.NEW_OWNER;
            CapacityState capacity = put.relocationCapacityFence()
                .map(fence -> capacityReservations.get(fence.value()))
                .orElse(null);
            if (relocatesOwner
                && (capacity == null
                    || capacity.state != State.RESERVED
                    || capacity.boundAggregateId != null
                    || !capacity.request.authorityKey().equals(key)
                    || !capacity.request.expectedStoreVersion().equals(
                        current.storeVersion)
                    || !capacity.request.sourceOwner().equals(current.owner)
                    || !sourceAllocationMatches(
                        current.allocation,
                        capacity.request)
                    || !targetDescriptorIsCurrent(
                        capacity.request)
                    || !capacity.request.targetOwner().equals(targetOwner))) {
                return completed(new ZLinkAuthorityConflict(
                    snapshot(current, now)));
            }
            if ((changesOwner
                    && authorityOwnerGeneration == Long.MAX_VALUE)
                || revision == Long.MAX_VALUE) {
                return completed(new ZLinkAuthorityGenerationExhausted());
            }
            long storedOwnerGeneration = changesOwner
                ? ++authorityOwnerGeneration
                : current.authorityOwnerGeneration;
            ZLinkPlacementAllocation storedAllocation = capacity == null
                ? current.allocation
                : activeTargetAllocation(capacity.request);
            Row stored = new Row(
                nextVersion(),
                put.payload(),
                current.objectGeneration,
                storedOwnerGeneration,
                targetOwner,
                storedAllocation);
            rows.put(key, stored);
            if (capacity != null) {
                relocateAllocation(
                    current.allocation,
                    storedAllocation);
                capacity.state = State.COMMITTED;
            }
            return completed(stored(stored, now));
        }
    }

    @Override
    public CompletionStage<ZLinkAuthorityScanResult> list(
        String prefix,
        Optional<ZLinkAuthorityScanCursor> cursor,
        int limit,
        ZLinkStoreCancellation cancellation) {
        synchronized (gate) {
            if (limit <= 0) {
                throw new IllegalArgumentException(
                    "authority scan limit must be positive");
            }
            int offset = cursor.map(value -> Integer.parseInt(value.encoded()))
                .orElse(0);
            List<Map.Entry<String, Row>> ordered = rows.entrySet().stream()
                .filter(entry -> entry.getKey().startsWith(prefix))
                .sorted(Map.Entry.comparingByKey())
                .toList();
            List<ZLinkAuthorityEntry> items = new ArrayList<>();
            Instant now = clock.instant();
            for (int index = offset;
                 index < ordered.size() && items.size() < limit;
                 index++) {
                var entry = ordered.get(index);
                items.add(new ZLinkAuthorityEntry(
                    entry.getKey(),
                    snapshot(entry.getValue(), now)));
            }
            int next = offset + items.size();
            return completed(new ZLinkAuthorityPage(
                items,
                next < ordered.size()
                    ? Optional.of(new ZLinkAuthorityScanCursor(
                        Integer.toString(next)))
                    : Optional.empty()));
        }
    }

    @Override
    public CompletionStage<ZLinkObjectReserveResult> reserve(
        ZLinkObjectReservationRequest request,
        ZLinkStoreCancellation cancellation) {
        synchronized (gate) {
            Instant now = clock.instant();
            Row current = rows.get(request.authorityKey());
            if (current != null) {
                return completed(new ZLinkObjectAlreadyExists(
                    snapshot(current, now)));
            }
            if (!ownerLeaseIsLive.test(request.targetOwner())) {
                return completed(new ZLinkObjectConflict(
                    new ZLinkAuthorityMissing(now)));
            }
            DescriptorAdmission admission = descriptorAdmission(
                request.targetDescriptor(),
                request.targetDescriptorLifecycleGeneration(),
                request.targetOwner(),
                request.objectKind(),
                request.stableType(),
                request.placementProfile(),
                request.pendingCapacityDelta());
            if (admission == DescriptorAdmission.UNAVAILABLE) {
                return completed(new ZLinkObjectConflict(
                    new ZLinkAuthorityMissing(now)));
            }
            if (admission == DescriptorAdmission.CAPACITY_EXHAUSTED) {
                return completed(new ZLinkPlacementCapacityExhausted());
            }
            if (objectGeneration == Long.MAX_VALUE
                || authorityOwnerGeneration == Long.MAX_VALUE
                || revision == Long.MAX_VALUE) {
                return completed(new ZLinkObjectGenerationExhausted());
            }
            long nextGeneration = ++objectGeneration;
            long nextOwnerGeneration = ++authorityOwnerGeneration;
            String storeVersion = nextVersion();
            String reservationVersion = UUID.randomUUID().toString();
            ZLinkObjectReservation reservation = new ZLinkObjectReservation(
                request.authorityKey(),
                storeVersion,
                nextGeneration,
                nextOwnerGeneration,
                reservationVersion,
                request.targetDescriptor(),
                request.targetDescriptorLifecycleGeneration(),
                request.targetOwner());
            rows.put(
                request.authorityKey(),
                new Row(
                    storeVersion,
                    request.creatingPayload(),
                    nextGeneration,
                    nextOwnerGeneration,
                    request.targetOwner(),
                    new ZLinkPlacementAllocation(
                        ZLinkPlacementAllocationState.PENDING,
                        request.objectKind(),
                        request.stableType(),
                        request.targetDescriptor(),
                        request.targetDescriptorLifecycleGeneration(),
                        request.pendingCapacityDelta())));
            adjustPending(
                rows.get(request.authorityKey()).allocation,
                request.pendingCapacityDelta());
            reservations.put(
                request.authorityKey(),
                new ReservationState(
                    reservation,
                    request,
                    State.PREPARED));
            return completed(new ZLinkObjectReserved(reservation));
        }
    }

    @Override
    public CompletionStage<ZLinkObjectCommitResult> commit(
        ZLinkObjectReservation reservation,
        byte[] readyPayload,
        ZLinkStoreCancellation cancellation) {
        synchronized (gate) {
            ReservationState state = reservations.get(
                reservation.authorityKey());
            if (!sameReservation(state, reservation)) {
                return completed(ZLinkObjectCommitResult.STALE);
            }
            if (state.state == State.COMMITTED) {
                return completed(ZLinkObjectCommitResult.ALREADY_COMMITTED);
            }
            if (state.state == State.ABORTED) {
                return completed(ZLinkObjectCommitResult.STALE);
            }
            if (!ownerLeaseIsLive.test(reservation.targetOwner())) {
                return completed(ZLinkObjectCommitResult.STALE);
            }
            Row current = rows.get(reservation.authorityKey());
            if (!pendingReservationMatches(current, state.reservation)) {
                return completed(ZLinkObjectCommitResult.STALE);
            }
            if (descriptorAdmission(
                    reservation.targetDescriptor(),
                    reservation.targetDescriptorLifecycleGeneration(),
                    reservation.targetOwner(),
                    current.allocation.objectKind(),
                    current.allocation.stableType(),
                    state.request.placementProfile(),
                    0)
                != DescriptorAdmission.ACCEPTED) {
                return completed(ZLinkObjectCommitResult.STALE);
            }
            if (!hasCounterRoom(revision, 1)) {
                return completed(
                    ZLinkObjectCommitResult.GENERATION_EXHAUSTED);
            }
            rows.put(
                reservation.authorityKey(),
                current.withPayloadAndAllocation(
                    nextVersion(),
                    readyPayload,
                    withAllocationState(
                        current.allocation,
                        ZLinkPlacementAllocationState.ACTIVE)));
            activateAllocation(current.allocation);
            state.state = State.COMMITTED;
            return completed(ZLinkObjectCommitResult.COMMITTED);
        }
    }

    @Override
    public CompletionStage<ZLinkObjectAbortResult> abort(
        ZLinkObjectReservation reservation,
        ZLinkStoreCancellation cancellation) {
        synchronized (gate) {
            ReservationState state = reservations.get(
                reservation.authorityKey());
            if (!sameReservation(state, reservation)) {
                return completed(ZLinkObjectAbortResult.STALE);
            }
            if (state.state == State.ABORTED) {
                return completed(ZLinkObjectAbortResult.ALREADY_ABORTED);
            }
            if (state.state == State.COMMITTED) {
                return completed(ZLinkObjectAbortResult.STALE);
            }
            Row current = rows.get(reservation.authorityKey());
            if (!pendingReservationMatches(current, state.reservation)) {
                return completed(ZLinkObjectAbortResult.STALE);
            }
            rows.remove(reservation.authorityKey());
            adjustPending(
                current.allocation,
                -current.allocation.capacityDelta());
            state.state = State.ABORTED;
            return completed(ZLinkObjectAbortResult.ABORTED);
        }
    }

    @Override
    public CompletionStage<ZLinkRelocationCapacityReserveResult>
        reserveRelocationCapacity(
            ZLinkRelocationCapacityReservationRequest request,
            ZLinkStoreCancellation cancellation) {
        synchronized (gate) {
            Instant now = clock.instant();
            Row current = rows.get(request.authorityKey());
            String fenceValue = request.reservationId().toString();
            CapacityState existing = capacityReservations.get(fenceValue);
            if (existing != null) {
                return completed(
                    existing.request.equals(request)
                        ? new ZLinkRelocationCapacityAlreadyReserved(
                            new ZLinkRelocationCapacityFence(fenceValue))
                        : new ZLinkRelocationCapacityConflict(
                            current == null
                                ? new ZLinkAuthorityMissing(now)
                                : snapshot(current, now)));
            }
            if (current == null
                || !current.storeVersion.equals(
                    request.expectedStoreVersion())) {
                return completed(new ZLinkRelocationCapacityConflict(
                    current == null
                        ? new ZLinkAuthorityMissing(now)
                        : snapshot(current, now)));
            }
            if (current.allocation.state()
                    != ZLinkPlacementAllocationState.ACTIVE
                || !current.owner.equals(request.sourceOwner())
                || !sourceAllocationMatches(
                    current.allocation,
                    request)) {
                return completed(new ZLinkRelocationCapacityConflict(
                    snapshot(current, now)));
            }
            if (!ownerLeaseIsLive.test(request.targetOwner())) {
                return completed(
                    new ZLinkRelocationCapacityTargetUnavailable());
            }
            DescriptorAdmission admission = descriptorAdmission(
                request.targetDescriptor(),
                request.targetDescriptorLifecycleGeneration(),
                request.targetOwner(),
                request.objectKind(),
                request.stableType(),
                Optional.empty(),
                request.capacityDelta());
            if (admission == DescriptorAdmission.UNAVAILABLE) {
                return completed(
                    new ZLinkRelocationCapacityTargetUnavailable());
            }
            if (admission == DescriptorAdmission.CAPACITY_EXHAUSTED) {
                return completed(
                    new ZLinkRelocationCapacityExhausted());
            }
            capacityReservations.put(
                fenceValue,
                new CapacityState(request, State.RESERVED));
            adjustPending(
                activeTargetAllocation(request),
                request.capacityDelta());
            return completed(new ZLinkRelocationCapacityReserved(
                new ZLinkRelocationCapacityFence(fenceValue)));
        }
    }

    @Override
    public CompletionStage<ZLinkRelocationCapacityAbortResult>
        abortRelocationCapacity(
            ZLinkRelocationCapacityFence fence,
            ZLinkStoreCancellation cancellation) {
        synchronized (gate) {
            CapacityState state = capacityReservations.get(fence.value());
            if (state == null) {
                return completed(
                    ZLinkRelocationCapacityAbortResult.STALE);
            }
            if (state.boundAggregateId != null) {
                return completed(
                    ZLinkRelocationCapacityAbortResult.STALE);
            }
            if (state.state == State.COMMITTED) {
                return completed(
                    ZLinkRelocationCapacityAbortResult.ALREADY_COMMITTED);
            }
            if (state.state == State.ABORTED) {
                return completed(
                    ZLinkRelocationCapacityAbortResult.ALREADY_ABORTED);
            }
            adjustPending(
                activeTargetAllocation(state.request),
                -state.request.capacityDelta());
            state.state = State.ABORTED;
            return completed(
                ZLinkRelocationCapacityAbortResult.ABORTED);
        }
    }

    @Override
    public CompletionStage<ZLinkAggregatePrepareResult> prepareAggregate(
        ZLinkAggregatePrepareRequest request,
        ZLinkStoreCancellation cancellation) {
        synchronized (gate) {
            AggregateState existing = aggregates.get(request.aggregateId());
            ZLinkAggregateFence fence = new ZLinkAggregateFence(
                request.aggregateId(),
                request.aggregateGeneration());
            if (existing != null) {
                return completed(
                    exactAggregateRequest(
                        existing.request,
                        request)
                        ? new ZLinkAggregateAlreadyPrepared(fence)
                        : existing.request.aggregateGeneration()
                            == request.aggregateGeneration()
                            ? new ZLinkAggregateConflict()
                            : new ZLinkAggregateStale());
            }
            if (!ownerLeaseIsLive.test(request.targetOwner())) {
                return completed(new ZLinkAggregateConflict());
            }
            if (request.participants().isEmpty()
                || request.participants().size() > 1024
                || request.targetReservations().size() > 1024
                || request.inventoryDigest().length != 32
                || !aggregateParticipantsAreCanonical(
                    request.participants())) {
                return completed(new ZLinkAggregateConflict());
            }
            Set<String> newOwnerKeys = new HashSet<>();
            for (ZLinkAggregateParticipant participant :
                request.participants()) {
                if (participant.ownerTransition()
                        == ZLinkAuthorityGenerationTransition.NEW_OWNER
                    && !newOwnerKeys.add(participant.authorityKey())) {
                    return completed(new ZLinkAggregateConflict());
                }
            }
            Set<String> reservedKeys = new HashSet<>();
            for (ZLinkRelocationCapacityFence capacityFence :
                request.targetReservations()) {
                CapacityState capacity =
                    capacityReservations.get(capacityFence.value());
                Row current = capacity == null
                    ? null
                    : rows.get(capacity.request.authorityKey());
                if (capacity == null
                    || capacity.state != State.RESERVED
                    || (capacity.boundAggregateId != null
                        && (!capacity.boundAggregateId.equals(
                                request.aggregateId())
                            || capacity.boundAggregateGeneration
                                != request.aggregateGeneration()))
                    || current == null
                    || !current.storeVersion.equals(
                        capacity.request.expectedStoreVersion())
                    || !current.owner.equals(
                        capacity.request.sourceOwner())
                    || !sourceAllocationMatches(
                        current.allocation,
                        capacity.request)
                    || !targetDescriptorIsCurrent(
                        capacity.request)
                    || !capacity.request.targetOwner()
                        .equals(request.targetOwner())
                    || !reservedKeys.add(
                        capacity.request.authorityKey())) {
                    return completed(new ZLinkAggregateConflict());
                }
            }
            if (!newOwnerKeys.equals(reservedKeys)) {
                return completed(new ZLinkAggregateConflict());
            }
            int ownerGenerationCount = Math.toIntExact(
                request.participants().stream()
                    .filter(participant ->
                        participant.ownerTransition()
                            == ZLinkAuthorityGenerationTransition.NEW_OWNER)
                    .count());
            if (!hasCounterRoom(
                    authorityOwnerGeneration,
                    ownerGenerationCount)
                || !hasCounterRoom(
                    revision,
                    request.participants().size())) {
                return completed(
                    new ZLinkAggregateGenerationExhausted());
            }
            for (ZLinkAggregateParticipant participant :
                request.participants()) {
                Row row = rows.get(participant.authorityKey());
                if (row == null
                    || row.allocation.state()
                        != ZLinkPlacementAllocationState.ACTIVE
                    || !row.storeVersion.equals(
                        participant.expectedStoreVersion())) {
                    return completed(new ZLinkAggregateConflict());
                }
            }
            for (ZLinkRelocationCapacityFence capacityFence :
                request.targetReservations()) {
                CapacityState capacity =
                    capacityReservations.get(capacityFence.value());
                capacity.boundAggregateId = request.aggregateId();
                capacity.boundAggregateGeneration =
                    request.aggregateGeneration();
                capacity.state = State.PREPARED;
            }
            aggregates.put(
                request.aggregateId(),
                new AggregateState(request, State.PREPARED));
            return completed(new ZLinkAggregatePrepared(fence));
        }
    }

    @Override
    public CompletionStage<ZLinkAggregateCommitResult> commitAggregate(
        ZLinkAggregateFence fence,
        ZLinkStoreCancellation cancellation) {
        synchronized (gate) {
            AggregateState state = aggregates.get(fence.aggregateId());
            if (!sameAggregate(state, fence)) {
                return completed(ZLinkAggregateCommitResult.STALE);
            }
            if (state.state == State.COMMITTED) {
                return completed(
                    ZLinkAggregateCommitResult.ALREADY_COMMITTED);
            }
            if (state.state == State.ABORTED) {
                return completed(ZLinkAggregateCommitResult.STALE);
            }
            if (!ownerLeaseIsLive.test(state.request.targetOwner())) {
                return completed(ZLinkAggregateCommitResult.STALE);
            }
            if (!aggregateStateIsCurrent(state.request, fence)) {
                return completed(ZLinkAggregateCommitResult.STALE);
            }
            int ownerGenerationCount = Math.toIntExact(
                state.request.participants().stream()
                    .filter(participant ->
                        participant.ownerTransition()
                            == ZLinkAuthorityGenerationTransition.NEW_OWNER)
                    .count());
            if (!hasCounterRoom(
                    authorityOwnerGeneration,
                    ownerGenerationCount)
                || !hasCounterRoom(
                    revision,
                    state.request.participants().size())) {
                return completed(
                    ZLinkAggregateCommitResult.GENERATION_EXHAUSTED);
            }
            for (ZLinkAggregateParticipant participant :
                state.request.participants()) {
                Row current = rows.get(participant.authorityKey());
                boolean changesOwner = participant.ownerTransition()
                    == ZLinkAuthorityGenerationTransition.NEW_OWNER;
                long ownerGeneration = changesOwner
                    ? ++authorityOwnerGeneration
                    : current.authorityOwnerGeneration;
                CapacityState capacity = changesOwner
                    ? capacityForAuthority(
                        state.request,
                        participant.authorityKey())
                    : null;
                rows.put(
                    participant.authorityKey(),
                    new Row(
                        nextVersion(),
                        participant.authorityPayload(),
                        current.objectGeneration,
                        ownerGeneration,
                        changesOwner
                            ? state.request.targetOwner()
                            : current.owner,
                        changesOwner
                            ? activeTargetAllocation(
                                capacity.request)
                            : current.allocation));
                membershipMutations.put(
                    participant.authorityKey(),
                    participant.membershipMutation());
                if (changesOwner) {
                    relocateAllocation(
                        current.allocation,
                        activeTargetAllocation(capacity.request));
                }
            }
            state.state = State.COMMITTED;
            for (ZLinkRelocationCapacityFence capacityFence :
                state.request.targetReservations()) {
                capacityReservations.get(capacityFence.value()).state =
                    State.COMMITTED;
            }
            return completed(ZLinkAggregateCommitResult.COMMITTED);
        }
    }

    @Override
    public CompletionStage<ZLinkAggregateAbortResult> abortAggregate(
        ZLinkAggregateFence fence,
        ZLinkStoreCancellation cancellation) {
        synchronized (gate) {
            AggregateState state = aggregates.get(fence.aggregateId());
            if (!sameAggregate(state, fence)) {
                return completed(ZLinkAggregateAbortResult.STALE);
            }
            if (state.state == State.ABORTED) {
                return completed(
                    ZLinkAggregateAbortResult.ALREADY_ABORTED);
            }
            if (state.state == State.COMMITTED) {
                return completed(ZLinkAggregateAbortResult.STALE);
            }
            state.state = State.ABORTED;
            for (ZLinkRelocationCapacityFence capacityFence :
                state.request.targetReservations()) {
                CapacityState capacity =
                    capacityReservations.get(capacityFence.value());
                if (capacity != null
                    && capacity.state == State.PREPARED
                    && capacity.isBoundTo(fence)) {
                    adjustPending(
                        activeTargetAllocation(capacity.request),
                        -capacity.request.capacityDelta());
                    capacity.state = State.ABORTED;
                }
            }
            return completed(ZLinkAggregateAbortResult.ABORTED);
        }
    }

    private boolean matches(
        Row current,
        ZLinkAuthorityExpectation expectation) {
        return current != null
            && current.storeVersion.equals(
                ((ZLinkAuthorityExpectFound) expectation).storeVersion());
    }

    private String nextVersion() {
        if (revision == Long.MAX_VALUE) {
            throw new ZLinkConfigurationException(
                "authority Store revision is exhausted");
        }
        return Long.toString(++revision);
    }

    private static boolean sameReservation(
        ReservationState state,
        ZLinkObjectReservation reservation) {
        return state != null
            && state.reservation.equals(reservation);
    }

    private boolean targetDescriptorIsCurrent(
        ZLinkRelocationCapacityReservationRequest request) {
        return descriptorAdmission(
                request.targetDescriptor(),
                request.targetDescriptorLifecycleGeneration(),
                request.targetOwner(),
                request.objectKind(),
                request.stableType(),
                Optional.empty(),
                0)
            == DescriptorAdmission.ACCEPTED;
    }

    private DescriptorAdmission descriptorAdmission(
        ZLinkMeshNodeDescriptorKey descriptorKey,
        long lifecycleGeneration,
        ZLinkLocationOwnerToken owner,
        ZLinkPlacementObjectKind objectKind,
        String stableType,
        Optional<String> placementProfile,
        int capacityDelta) {
        ZLinkMeshNodeDescriptor descriptor = descriptorLookup.find(
            descriptorKey,
            lifecycleGeneration,
            owner);
        if (descriptor == null
            || !descriptor.meshName().equals(
                descriptorKey.meshName())
            || !descriptor.rid().equals(descriptorKey.rid())
            || descriptor.lifecycleGeneration()
                != lifecycleGeneration
            || !descriptor.ownerId().equals(owner.ownerId())
            || descriptor.leaseGeneration()
                != owner.leaseGeneration()
            || descriptor.state()
                != systems.zlink.framework.runtime.host
                    .ZLinkFrameworkRuntimeState.SERVING
            || descriptor.objectRole()
                != ZLinkMeshNodeObjectRole.SERVER
            || descriptor.placementWeight() <= 0) {
            return DescriptorAdmission.UNAVAILABLE;
        }
        ZLinkObjectCapability capability =
            descriptor.objectCapabilities().stream()
                .filter(candidate ->
                    candidate.objectKind() == objectKind
                        && candidate.stableType().equals(stableType))
                .findFirst()
                .orElse(null);
        if (capability == null
            || placementProfile.isPresent()
                && !capability.placementProfiles().contains(
                    placementProfile.orElseThrow())) {
            return DescriptorAdmission.UNAVAILABLE;
        }
        return canReserve(
            descriptor,
            capability,
            objectKind,
            stableType,
            capacityDelta)
                ? DescriptorAdmission.ACCEPTED
                : DescriptorAdmission.CAPACITY_EXHAUSTED;
    }

    private boolean canReserve(
        ZLinkMeshNodeDescriptor descriptor,
        ZLinkObjectCapability capability,
        ZLinkPlacementObjectKind objectKind,
        String stableType,
        int delta) {
        AllocationCounterKey nodeKey = new AllocationCounterKey(
            new ZLinkMeshNodeDescriptorKey(
                descriptor.meshName(),
                descriptor.rid()),
            descriptor.lifecycleGeneration());
        TypeAllocationCounterKey typeKey =
            new TypeAllocationCounterKey(
                nodeKey.descriptor(),
                nodeKey.lifecycleGeneration(),
                objectKind,
                stableType);
        CapacityCounter node = nodeAllocationCounters.get(nodeKey);
        CapacityCounter type = typeAllocationCounters.get(typeKey);
        long nodeActive = node == null ? 0 : node.active;
        long nodePending = node == null ? 0 : node.pending;
        long typeActive = type == null ? 0 : type.active;
        long typePending = type == null ? 0 : type.pending;
        long nodeActiveLimit =
            descriptor.capacity().activeLimit();
        long nodePendingLimit =
            descriptor.capacity().pendingLimit();
        long typeActiveLimit = capability.activeLimit() == null
            ? nodeActiveLimit
            : Math.min(
                capability.activeLimit(),
                nodeActiveLimit);
        long typePendingLimit = capability.pendingLimit() == null
            ? nodePendingLimit
            : Math.min(
                capability.pendingLimit(),
                nodePendingLimit);
        return delta >= 0
            && nodePending + delta <= nodePendingLimit
            && nodeActive + nodePending + delta
                <= nodeActiveLimit
            && typePending + delta <= typePendingLimit
            && typeActive + typePending + delta
                <= typeActiveLimit;
    }

    private static boolean sourceAllocationMatches(
        ZLinkPlacementAllocation allocation,
        ZLinkRelocationCapacityReservationRequest request) {
        return allocation.state()
                == ZLinkPlacementAllocationState.ACTIVE
            && allocation.objectKind() == request.objectKind()
            && allocation.stableType().equals(request.stableType())
            && allocation.descriptor().equals(
                request.sourceDescriptor())
            && allocation.descriptorLifecycleGeneration()
                == request.sourceDescriptorLifecycleGeneration()
            && allocation.capacityDelta() == request.capacityDelta();
    }

    private static ZLinkPlacementAllocation activeTargetAllocation(
        ZLinkRelocationCapacityReservationRequest request) {
        return new ZLinkPlacementAllocation(
            ZLinkPlacementAllocationState.ACTIVE,
            request.objectKind(),
            request.stableType(),
            request.targetDescriptor(),
            request.targetDescriptorLifecycleGeneration(),
            request.capacityDelta());
    }

    private CapacityState capacityForAuthority(
        ZLinkAggregatePrepareRequest request,
        String authorityKey) {
        for (ZLinkRelocationCapacityFence fence :
            request.targetReservations()) {
            CapacityState capacity =
                capacityReservations.get(fence.value());
            if (capacity != null
                && capacity.request.authorityKey().equals(authorityKey)) {
                return capacity;
            }
        }
        throw new IllegalStateException(
            "prepared aggregate capacity is missing");
    }

    private static boolean pendingReservationMatches(
        Row row,
        ZLinkObjectReservation reservation) {
        return row != null
            && row.storeVersion.equals(reservation.storeVersion())
            && row.objectGeneration == reservation.objectGeneration()
            && row.authorityOwnerGeneration
                == reservation.authorityOwnerGeneration()
            && row.owner.equals(reservation.targetOwner())
            && row.allocation.state()
                == ZLinkPlacementAllocationState.PENDING
            && row.allocation.descriptor().equals(
                reservation.targetDescriptor())
            && row.allocation.descriptorLifecycleGeneration()
                == reservation.targetDescriptorLifecycleGeneration();
    }

    private static ZLinkPlacementAllocation withAllocationState(
        ZLinkPlacementAllocation allocation,
        ZLinkPlacementAllocationState state) {
        return new ZLinkPlacementAllocation(
            state,
            allocation.objectKind(),
            allocation.stableType(),
            allocation.descriptor(),
            allocation.descriptorLifecycleGeneration(),
            allocation.capacityDelta());
    }

    private void activateAllocation(ZLinkPlacementAllocation allocation) {
        adjustPending(allocation, -allocation.capacityDelta());
        adjustActive(allocation, allocation.capacityDelta());
    }

    private void relocateAllocation(
        ZLinkPlacementAllocation source,
        ZLinkPlacementAllocation target) {
        adjustActive(source, -source.capacityDelta());
        adjustPending(target, -target.capacityDelta());
        adjustActive(target, target.capacityDelta());
    }

    private void adjustPending(
        ZLinkPlacementAllocation allocation,
        long delta) {
        CapacityCounter node = nodeAllocationCounter(allocation);
        CapacityCounter type = typeAllocationCounter(allocation);
        long nodeValue = Math.addExact(node.pending, delta);
        long typeValue = Math.addExact(type.pending, delta);
        if (nodeValue < 0 || typeValue < 0) {
            throw new IllegalStateException(
                "pending placement capacity became negative");
        }
        node.pending = nodeValue;
        type.pending = typeValue;
    }

    private void adjustActive(
        ZLinkPlacementAllocation allocation,
        long delta) {
        CapacityCounter node = nodeAllocationCounter(allocation);
        CapacityCounter type = typeAllocationCounter(allocation);
        long nodeValue = Math.addExact(node.active, delta);
        long typeValue = Math.addExact(type.active, delta);
        if (nodeValue < 0 || typeValue < 0) {
            throw new IllegalStateException(
                "active placement capacity became negative");
        }
        node.active = nodeValue;
        type.active = typeValue;
    }

    private CapacityCounter nodeAllocationCounter(
        ZLinkPlacementAllocation allocation) {
        return nodeAllocationCounters.computeIfAbsent(
            new AllocationCounterKey(
                allocation.descriptor(),
                allocation.descriptorLifecycleGeneration()),
            ignored -> new CapacityCounter());
    }

    private CapacityCounter typeAllocationCounter(
        ZLinkPlacementAllocation allocation) {
        return typeAllocationCounters.computeIfAbsent(
            new TypeAllocationCounterKey(
                allocation.descriptor(),
                allocation.descriptorLifecycleGeneration(),
                allocation.objectKind(),
                allocation.stableType()),
            ignored -> new CapacityCounter());
    }

    long activeCapacity(
        ZLinkMeshNodeDescriptorKey descriptor,
        long lifecycleGeneration) {
        synchronized (gate) {
            CapacityCounter counter = nodeAllocationCounters.get(
                new AllocationCounterKey(
                    descriptor,
                    lifecycleGeneration));
            return counter == null ? 0 : counter.active;
        }
    }

    long pendingCapacity(
        ZLinkMeshNodeDescriptorKey descriptor,
        long lifecycleGeneration) {
        synchronized (gate) {
            CapacityCounter counter = nodeAllocationCounters.get(
                new AllocationCounterKey(
                    descriptor,
                    lifecycleGeneration));
            return counter == null ? 0 : counter.pending;
        }
    }

    byte[] membershipMutation(String authorityKey) {
        synchronized (gate) {
            byte[] value = membershipMutations.get(authorityKey);
            return value == null ? null : value.clone();
        }
    }

    private static boolean exactAggregateRequest(
        ZLinkAggregatePrepareRequest left,
        ZLinkAggregatePrepareRequest right) {
        if (!left.aggregateId().equals(right.aggregateId())
            || left.aggregateGeneration()
                != right.aggregateGeneration()
            || !left.targetOwner().equals(right.targetOwner())
            || !left.targetReservations().equals(
                right.targetReservations())
            || !Arrays.equals(
                left.inventoryDigest(),
                right.inventoryDigest())
            || left.participants().size()
                != right.participants().size()) {
            return false;
        }
        for (int index = 0;
             index < left.participants().size();
             index++) {
            ZLinkAggregateParticipant first =
                left.participants().get(index);
            ZLinkAggregateParticipant second =
                right.participants().get(index);
            if (!first.authorityKey().equals(second.authorityKey())
                || !first.expectedStoreVersion().equals(
                    second.expectedStoreVersion())
                || first.ownerTransition()
                    != second.ownerTransition()
                || !Arrays.equals(
                    first.authorityPayload(),
                    second.authorityPayload())
                || !Arrays.equals(
                    first.membershipMutation(),
                    second.membershipMutation())) {
                return false;
            }
        }
        return true;
    }

    private static boolean aggregateParticipantsAreCanonical(
        List<ZLinkAggregateParticipant> participants) {
        byte[] previous = null;
        for (ZLinkAggregateParticipant participant : participants) {
            byte[] current = participant.authorityKey()
                .getBytes(StandardCharsets.UTF_8);
            if (previous != null
                && Arrays.compareUnsigned(previous, current) >= 0) {
                return false;
            }
            previous = current;
        }
        return true;
    }

    private static boolean hasCounterRoom(long current, int increments) {
        return increments >= 0
            && current <= Long.MAX_VALUE - increments;
    }

    private boolean aggregateStateIsCurrent(
        ZLinkAggregatePrepareRequest request,
        ZLinkAggregateFence aggregateFence) {
        Map<String, CapacityState> capacitiesByAuthority =
            new HashMap<>();
        for (ZLinkRelocationCapacityFence fence :
            request.targetReservations()) {
            CapacityState capacity =
                capacityReservations.get(fence.value());
            if (capacity == null
                || capacity.state != State.PREPARED
                || !capacity.isBoundTo(aggregateFence)
                || !targetDescriptorIsCurrent(
                    capacity.request)
                || capacitiesByAuthority.put(
                    capacity.request.authorityKey(),
                    capacity) != null) {
                return false;
            }
        }
        for (ZLinkAggregateParticipant participant :
            request.participants()) {
            Row current = rows.get(participant.authorityKey());
            if (current == null
                || !current.storeVersion.equals(
                    participant.expectedStoreVersion())) {
                return false;
            }
            if (participant.ownerTransition()
                != ZLinkAuthorityGenerationTransition.NEW_OWNER) {
                continue;
            }
            CapacityState capacity =
                capacitiesByAuthority.remove(
                    participant.authorityKey());
            if (capacity == null
                || !capacity.request.expectedStoreVersion().equals(
                    participant.expectedStoreVersion())
                || !capacity.request.sourceOwner().equals(current.owner)
                || !sourceAllocationMatches(
                    current.allocation,
                    capacity.request)
                || !capacity.request.targetOwner().equals(
                    request.targetOwner())
                || !ownerLeaseIsLive.test(
                    capacity.request.targetOwner())) {
                return false;
            }
        }
        return capacitiesByAuthority.isEmpty();
    }

    private static boolean sameAggregate(
        AggregateState state,
        ZLinkAggregateFence fence) {
        return state != null
            && state.request.aggregateGeneration()
                == fence.aggregateGeneration();
    }

    private static ZLinkAuthoritySnapshot snapshot(Row row, Instant now) {
        return new ZLinkAuthoritySnapshot(
            row.storeVersion,
            row.payload,
            row.objectGeneration,
            row.authorityOwnerGeneration,
            row.owner.ownerId(),
            row.owner.leaseGeneration(),
            row.allocation,
            now);
    }

    private static ZLinkAuthorityStored stored(Row row, Instant now) {
        return new ZLinkAuthorityStored(
            row.storeVersion,
            row.payload,
            row.objectGeneration,
            row.authorityOwnerGeneration,
            row.owner.ownerId(),
            row.owner.leaseGeneration(),
            row.allocation,
            now);
    }

    private static <T> CompletionStage<T> completed(T value) {
        return CompletableFuture.completedFuture(value);
    }

    private record Row(
        String storeVersion,
        byte[] payload,
        long objectGeneration,
        long authorityOwnerGeneration,
        ZLinkLocationOwnerToken owner,
        ZLinkPlacementAllocation allocation) {
        private Row {
            payload = payload.clone();
        }

        private Row withPayloadAndAllocation(
            String version,
            byte[] value,
            ZLinkPlacementAllocation nextAllocation) {
            return new Row(
                version,
                value,
                objectGeneration,
                authorityOwnerGeneration,
                owner,
                nextAllocation);
        }
    }

    private record AllocationCounterKey(
        ZLinkMeshNodeDescriptorKey descriptor,
        long lifecycleGeneration) {
    }

    private record TypeAllocationCounterKey(
        ZLinkMeshNodeDescriptorKey descriptor,
        long lifecycleGeneration,
        ZLinkPlacementObjectKind objectKind,
        String stableType) {
    }

    private static final class CapacityCounter {
        private long active;
        private long pending;
    }

    private enum DescriptorAdmission {
        ACCEPTED,
        UNAVAILABLE,
        CAPACITY_EXHAUSTED
    }

    private enum State {
        RESERVED,
        PREPARED,
        COMMITTED,
        ABORTED
    }

    private static final class ReservationState {
        private final ZLinkObjectReservation reservation;
        private final ZLinkObjectReservationRequest request;
        private State state;

        private ReservationState(
            ZLinkObjectReservation reservation,
            ZLinkObjectReservationRequest request,
            State state) {
            this.reservation = reservation;
            this.request = request;
            this.state = state;
        }
    }

    private static final class AggregateState {
        private final ZLinkAggregatePrepareRequest request;
        private State state;

        private AggregateState(
            ZLinkAggregatePrepareRequest request,
            State state) {
            this.request = request;
            this.state = state;
        }
    }

    private static final class CapacityState {
        private final ZLinkRelocationCapacityReservationRequest request;
        private State state;
        private UUID boundAggregateId;
        private long boundAggregateGeneration;

        private CapacityState(
            ZLinkRelocationCapacityReservationRequest request,
            State state) {
            this.request = request;
            this.state = state;
        }

        private boolean isBoundTo(ZLinkAggregateFence fence) {
            return boundAggregateId != null
                && boundAggregateId.equals(fence.aggregateId())
                && boundAggregateGeneration
                    == fence.aggregateGeneration();
        }
    }

    @FunctionalInterface
    interface DescriptorLookup {
        ZLinkMeshNodeDescriptor find(
            ZLinkMeshNodeDescriptorKey descriptor,
            long lifecycleGeneration,
            ZLinkLocationOwnerToken owner);
    }
}
