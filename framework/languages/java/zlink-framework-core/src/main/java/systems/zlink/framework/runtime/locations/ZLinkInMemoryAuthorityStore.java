package systems.zlink.framework.runtime.locations;

import java.time.Clock;
import java.time.Instant;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.locations.*;

final class ZLinkInMemoryAuthorityStore implements ZLinkAuthorityStore {
    private final Object gate = new Object();
    private final Clock clock;
    private final Map<String, Row> rows = new HashMap<>();
    private final Map<String, Long> objectGenerations = new HashMap<>();
    private final Map<String, ReservationState> reservations = new HashMap<>();
    private final Map<UUID, AggregateState> aggregates = new HashMap<>();
    private long revision;

    ZLinkInMemoryAuthorityStore(Clock clock) {
        this.clock = clock;
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
                if (current == null) {
                    return completed(new ZLinkAuthorityConflict(
                        new ZLinkAuthorityMissing(now)));
                }
                rows.remove(key);
                return completed(new ZLinkAuthorityDeleted(
                    nextVersion(),
                    now));
            }
            ZLinkAuthorityPut put = (ZLinkAuthorityPut) mutation;
            if (put.generationTransition()
                != ZLinkAuthorityGenerationTransition.PRESERVE) {
                throw new ZLinkConfigurationException(
                    "NEW_OWNER and NEW_OBJECT require an explicit target owner token");
            }
            if (current == null) {
                return completed(new ZLinkAuthorityConflict(
                    new ZLinkAuthorityMissing(now)));
            }
            Row stored = current.withPayload(
                nextVersion(),
                put.payload());
            rows.put(key, stored);
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
            long generation =
                objectGenerations.getOrDefault(request.authorityKey(), 0L);
            if (generation == Long.MAX_VALUE) {
                return completed(new ZLinkObjectGenerationExhausted());
            }
            long nextGeneration = generation + 1L;
            objectGenerations.put(request.authorityKey(), nextGeneration);
            String storeVersion = nextVersion();
            String reservationVersion = UUID.randomUUID().toString();
            ZLinkObjectReservation reservation = new ZLinkObjectReservation(
                request.authorityKey(),
                storeVersion,
                nextGeneration,
                1L,
                reservationVersion,
                request.targetDescriptor(),
                request.targetOwner());
            rows.put(
                request.authorityKey(),
                new Row(
                    storeVersion,
                    request.creationIntentHash(),
                    nextGeneration,
                    1L,
                    request.targetOwner()));
            reservations.put(
                request.authorityKey(),
                new ReservationState(reservation, State.PREPARED));
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
            Row current = rows.get(reservation.authorityKey());
            rows.put(
                reservation.authorityKey(),
                current.withPayload(nextVersion(), readyPayload));
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
            rows.remove(reservation.authorityKey());
            state.state = State.ABORTED;
            return completed(ZLinkObjectAbortResult.ABORTED);
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
                    existing.request.aggregateGeneration()
                        == request.aggregateGeneration()
                        ? new ZLinkAggregateAlreadyPrepared(fence)
                        : new ZLinkAggregateStale());
            }
            for (ZLinkAggregateParticipant participant :
                request.participants()) {
                Row row = rows.get(participant.authorityKey());
                if (row == null
                    || !row.storeVersion.equals(
                        participant.expectedStoreVersion())) {
                    return completed(new ZLinkAggregateConflict());
                }
                boolean changesOwner = participant.ownerTransition()
                    != ZLinkAuthorityGenerationTransition.PRESERVE;
                boolean changesObject = participant.ownerTransition()
                    == ZLinkAuthorityGenerationTransition.NEW_OBJECT;
                if ((changesOwner
                        && row.authorityOwnerGeneration == Long.MAX_VALUE)
                    || (changesObject
                        && row.objectGeneration == Long.MAX_VALUE)) {
                    return completed(
                        new ZLinkAggregateGenerationExhausted());
                }
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
            for (ZLinkAggregateParticipant participant :
                state.request.participants()) {
                Row current = rows.get(participant.authorityKey());
                boolean changesOwner = participant.ownerTransition()
                    != ZLinkAuthorityGenerationTransition.PRESERVE;
                boolean changesObject = participant.ownerTransition()
                    == ZLinkAuthorityGenerationTransition.NEW_OBJECT;
                long ownerGeneration = changesOwner
                    ? current.authorityOwnerGeneration + 1L
                    : current.authorityOwnerGeneration;
                long objectGeneration = changesObject
                    ? current.objectGeneration + 1L
                    : current.objectGeneration;
                rows.put(
                    participant.authorityKey(),
                    new Row(
                        nextVersion(),
                        participant.authorityPayload(),
                        objectGeneration,
                        ownerGeneration,
                        state.request.targetOwner()));
                if (changesObject) {
                    objectGenerations.put(
                        participant.authorityKey(),
                        objectGeneration);
                }
            }
            state.state = State.COMMITTED;
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
            return completed(ZLinkAggregateAbortResult.ABORTED);
        }
    }

    private boolean matches(
        Row current,
        ZLinkAuthorityExpectation expectation) {
        if (expectation instanceof ZLinkAuthorityExpectMissing) {
            return current == null;
        }
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
            && state.reservation.reservationVersion().equals(
                reservation.reservationVersion());
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
            row.owner.generation(),
            now);
    }

    private static ZLinkAuthorityStored stored(Row row, Instant now) {
        return new ZLinkAuthorityStored(
            row.storeVersion,
            row.payload,
            row.objectGeneration,
            row.authorityOwnerGeneration,
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
        ZLinkLocationOwnerToken owner) {
        private Row {
            payload = payload.clone();
        }

        private Row withPayload(String version, byte[] value) {
            return new Row(
                version,
                value,
                objectGeneration,
                authorityOwnerGeneration,
                owner);
        }
    }

    private enum State {
        PREPARED,
        COMMITTED,
        ABORTED
    }

    private static final class ReservationState {
        private final ZLinkObjectReservation reservation;
        private State state;

        private ReservationState(
            ZLinkObjectReservation reservation,
            State state) {
            this.reservation = reservation;
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
}
