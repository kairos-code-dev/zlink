package systems.zlink.framework.runtime.spots;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkRelocationCancellation;
import systems.zlink.framework.execution.ZLinkAsyncSerialQueue;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.runtime.actors.ZLinkActorRuntime;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorRef;
import systems.zlink.framework.runtime.internal.relocation
    .ZLinkRelocationAdapterRegistry;
import systems.zlink.framework.spots.ZLinkSpot;

/**
 * Owns target factory and Restore staging for one User Spot aggregate.
 * Prepared objects stay outside live registries until every participant,
 * timer and accepted-journal record has been validated.
 */
final class ZLinkUserSpotAggregateStagingOwner {
    private final StagingBackend backend;

    ZLinkUserSpotAggregateStagingOwner(
        ZLinkSpotLifecycle spots,
        ZLinkActorSessionCoordinator actorSessions,
        ZLinkRelocationAdapterRegistry adapters) {
        backend = new ProductionBackend(
            Objects.requireNonNull(spots, "spots"),
            Objects.requireNonNull(actorSessions, "actorSessions").runtime(),
            Objects.requireNonNull(adapters, "adapters"));
    }

    ZLinkUserSpotAggregateStagingOwner(StagingBackend backend) {
        this.backend = Objects.requireNonNull(backend, "backend");
    }

    CompletionStage<Staged> stage(
        Request request,
        ZLinkRelocationCancellation cancellation) {
        Objects.requireNonNull(request, "request");
        Objects.requireNonNull(cancellation, "cancellation");
        List<Object> preparedActors = new ArrayList<>();
        return backend.prepareSpot(request)
            .thenCompose(preparedSpot -> backend.restoreSpot(
                    preparedSpot,
                    request,
                    cancellation)
                .thenCompose(ignored -> prepareActors(
                    request,
                    cancellation,
                    preparedActors))
                .thenApply(ignored -> new Staged(
                    this,
                    request,
                    preparedSpot,
                    preparedActors))
                .exceptionallyCompose(failure -> discardPartial(
                        preparedSpot,
                        preparedActors)
                    .thenCompose(ignored -> CompletableFuture.failedFuture(
                        unwrap(failure)))));
    }

    private CompletionStage<Void> prepareActors(
        Request request,
        ZLinkRelocationCancellation cancellation,
        List<Object> prepared) {
        CompletionStage<Void> chain = CompletableFuture.completedFuture(null);
        for (ActorParticipant participant : request.actors()) {
            chain = chain.thenCompose(ignored -> backend.prepareActor(
                    participant,
                    cancellation)
                .thenAccept(prepared::add));
        }
        return chain;
    }

    private CompletionStage<Void> discardPartial(
        Object spot,
        List<Object> actorsToDiscard) {
        CompletionStage<Void> chain = CompletableFuture.completedFuture(null);
        for (int index = actorsToDiscard.size() - 1; index >= 0; index--) {
            var actor = actorsToDiscard.get(index);
            chain = chain.thenCompose(ignored ->
                backend.discardActor(actor));
        }
        return chain.whenComplete((ignored, failure) -> backend.discardSpot(spot));
    }

    CompletionStage<Void> publishAndReplay(
        Staged staged,
        JournalReplayer replayer) {
        requireActive(staged);
        Objects.requireNonNull(replayer, "replayer");
        // The caller invokes this only after Location aggregate commit. The
        // host structural barrier prevents create/membership races here.
        backend.publishSpot(staged.spot);
        for (var actor : staged.actors) {
            backend.publishActor(actor);
        }
        staged.published = true;
        CompletionStage<Void> replay = CompletableFuture.completedFuture(null);
        for (Map.Entry<String, List<ZLinkAsyncSerialQueue.QueuedRecord>> lane
            : staged.request.acceptedJournal().entrySet()) {
            for (ZLinkAsyncSerialQueue.QueuedRecord record : lane.getValue()) {
                replay = replay.thenCompose(ignored -> replayer.replay(
                    lane.getKey(),
                    record));
            }
        }
        return replay.thenRun(() -> {
            for (var actor : staged.actors) {
                backend.completeActor(actor);
            }
            backend.publishTimers(staged.spot);
            staged.terminal = true;
        });
    }

    CompletionStage<Void> discard(Staged staged) {
        requireActive(staged);
        if (staged.published) {
            return CompletableFuture.failedFuture(new IllegalStateException(
                "committed aggregate staging cannot roll back to source"));
        }
        staged.terminal = true;
        return discardPartial(staged.spot, staged.actors);
    }

    private void requireActive(Staged staged) {
        if (staged == null || staged.owner != this || staged.terminal) {
            throw new IllegalStateException(
                "aggregate staging fence is not active");
        }
    }

    private static void validateJournal(
        Map<String, List<ZLinkAsyncSerialQueue.QueuedRecord>> journal) {
        for (Map.Entry<String, List<ZLinkAsyncSerialQueue.QueuedRecord>> lane
            : journal.entrySet()) {
            if (lane.getKey() == null || lane.getKey().isBlank()) {
                throw new IllegalArgumentException(
                    "accepted journal lane id is required");
            }
            long previous = 0;
            for (ZLinkAsyncSerialQueue.QueuedRecord record : lane.getValue()) {
                if (record.sequence() <= previous) {
                    throw new IllegalArgumentException(
                        "accepted journal sequence must be strictly increasing");
                }
                previous = record.sequence();
            }
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

    @FunctionalInterface
    interface JournalReplayer {
        CompletionStage<Void> replay(
            String laneId,
            ZLinkAsyncSerialQueue.QueuedRecord record);
    }

    interface StagingBackend {
        CompletionStage<Object> prepareSpot(Request request);

        CompletionStage<Void> restoreSpot(
            Object preparedSpot,
            Request request,
            ZLinkRelocationCancellation cancellation);

        CompletionStage<Object> prepareActor(
            ActorParticipant participant,
            ZLinkRelocationCancellation cancellation);

        void publishSpot(Object preparedSpot);

        void publishActor(Object preparedActor);

        void completeActor(Object preparedActor);

        void publishTimers(Object preparedSpot);

        CompletionStage<Void> discardActor(Object preparedActor);

        void discardSpot(Object preparedSpot);
    }

    record ActorParticipant(
        String actorId,
        String actorType,
        byte[] state,
        boolean restoreSnapshot,
        ZLinkBackendActorRef preparedActorRef) {
        ActorParticipant {
            if (actorId == null || actorId.isBlank()
                || actorType == null || actorType.isBlank()) {
                throw new IllegalArgumentException(
                    "Actor id and stable type are required");
            }
            state = Objects.requireNonNull(state, "state").clone();
        }

        @Override public byte[] state() { return state.clone(); }
    }

    record Request(
        Class<? extends ZLinkSpot<?>> spotType,
        String spotStableType,
        String spotId,
        long objectGeneration,
        byte[] spotState,
        boolean restoreSpotSnapshot,
        byte[] timerEnvelope,
        List<ActorParticipant> actors,
        Map<String, List<ZLinkAsyncSerialQueue.QueuedRecord>> acceptedJournal) {
        Request {
            Objects.requireNonNull(spotType, "spotType");
            if (spotStableType == null || spotStableType.isBlank()
                || spotId == null || spotId.isBlank()
                || objectGeneration <= 0) {
                throw new IllegalArgumentException(
                    "Spot stable type, id and generation are required");
            }
            spotState = Objects.requireNonNull(spotState, "spotState").clone();
            timerEnvelope = Objects.requireNonNull(
                timerEnvelope,
                "timerEnvelope").clone();
            actors = List.copyOf(Objects.requireNonNull(actors, "actors"));
            java.util.LinkedHashMap<String, List<
                ZLinkAsyncSerialQueue.QueuedRecord>> journalCopy =
                    new java.util.LinkedHashMap<>();
            Objects.requireNonNull(acceptedJournal, "acceptedJournal")
                .forEach((lane, records) -> journalCopy.put(
                    lane,
                    List.copyOf(records)));
            acceptedJournal = java.util.Collections.unmodifiableMap(journalCopy);
            validateJournal(acceptedJournal);
        }

        @Override public byte[] spotState() { return spotState.clone(); }
        @Override public byte[] timerEnvelope() { return timerEnvelope.clone(); }
    }

    static final class Staged {
        private final ZLinkUserSpotAggregateStagingOwner owner;
        private final Request request;
        private final Object spot;
        private final List<Object> actors;
        private boolean published;
        private boolean terminal;

        private Staged(
            ZLinkUserSpotAggregateStagingOwner owner,
            Request request,
            Object spot,
            List<Object> actors) {
            this.owner = owner;
            this.request = request;
            this.spot = spot;
            this.actors = List.copyOf(actors);
        }
    }

    private static final class ProductionBackend implements StagingBackend {
        private final ZLinkSpotLifecycle spots;
        private final ZLinkActorRuntime actors;
        private final ZLinkRelocationAdapterRegistry adapters;

        private ProductionBackend(
            ZLinkSpotLifecycle spots,
            ZLinkActorRuntime actors,
            ZLinkRelocationAdapterRegistry adapters) {
            this.spots = spots;
            this.actors = actors;
            this.adapters = adapters;
        }

        @Override
        public CompletionStage<Object> prepareSpot(Request request) {
            return spots.prepareReserved(
                    request.spotType(),
                    request.spotId(),
                    request.objectGeneration(),
                    ZLinkMessage.empty())
                .thenApply(value -> value);
        }

        @Override
        public CompletionStage<Void> restoreSpot(
            Object value,
            Request request,
            ZLinkRelocationCancellation cancellation) {
            var prepared = (ZLinkSpotLifecycle.PreparedUserSpot) value;
            Object spot = spots.preparedSpot(prepared);
            CompletionStage<Void> restore = request.restoreSpotSnapshot()
                ? adapters.restoreSpot(
                    request.spotStableType(),
                    spot,
                    request.spotState(),
                    cancellation)
                : CompletableFuture.completedFuture(null);
            return restore.thenRun(() -> {
                validateJournal(request.acceptedJournal());
                spots.stageReservedTimers(prepared, request.timerEnvelope());
            });
        }

        @Override
        public CompletionStage<Object> prepareActor(
            ActorParticipant participant,
            ZLinkRelocationCancellation cancellation) {
            return actors.prepareRelocatedActor(
                    participant.actorId(),
                    participant.actorType(),
                    participant.state(),
                    participant.restoreSnapshot(),
                    adapters,
                    cancellation,
                    participant.preparedActorRef())
                .thenApply(value -> value);
        }

        @Override public void publishSpot(Object value) {
            spots.publishReserved((ZLinkSpotLifecycle.PreparedUserSpot) value);
        }

        @Override public void publishActor(Object value) {
            actors.publishPreparedTransferredActor(
                (ZLinkActorRuntime.PreparedTransferredActor) value);
        }

        @Override public void completeActor(Object value) {
            actors.completePreparedTransferredActor(
                (ZLinkActorRuntime.PreparedTransferredActor) value);
        }

        @Override public void publishTimers(Object value) {
            spots.publishReservedTimers(
                (ZLinkSpotLifecycle.PreparedUserSpot) value);
        }

        @Override public CompletionStage<Void> discardActor(Object value) {
            return actors.discardPreparedTransferredActor(
                (ZLinkActorRuntime.PreparedTransferredActor) value);
        }

        @Override public void discardSpot(Object value) {
            spots.discardReserved((ZLinkSpotLifecycle.PreparedUserSpot) value);
        }
    }
}
