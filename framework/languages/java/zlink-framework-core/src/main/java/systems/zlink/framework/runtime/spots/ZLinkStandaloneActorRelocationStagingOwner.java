package systems.zlink.framework.runtime.spots;

import java.util.Objects;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkRelocationCancellation;
import systems.zlink.framework.runtime.actors.ZLinkActorRuntime;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendActorRef;
import systems.zlink.framework.runtime.internal.relocation
    .ZLinkRelocationAdapterRegistry;

/**
 * Owns one independent Actor target attempt. Factory and Restore finish while
 * the Actor is absent from the live registry. Publication and admission are
 * separate so the caller can keep the target closed through durable replay.
 */
final class ZLinkStandaloneActorRelocationStagingOwner {
    private static final ZLinkRelocationCancellation NOT_CANCELLED =
        () -> false;

    private final Backend backend;

    ZLinkStandaloneActorRelocationStagingOwner(
        RoutingId targetNodeRid,
        ZLinkActorSessionCoordinator actors,
        ZLinkRelocationAdapterRegistry adapters,
        ZLinkSpotRuntime spots) {
        backend = new ProductionBackend(
            Objects.requireNonNull(targetNodeRid, "targetNodeRid"),
            Objects.requireNonNull(actors, "actors"),
            Objects.requireNonNull(adapters, "adapters"),
            Objects.requireNonNull(spots, "spots"));
    }

    ZLinkStandaloneActorRelocationStagingOwner(Backend backend) {
        this.backend = Objects.requireNonNull(backend, "backend");
    }

    CompletionStage<Staged> stage(Request request, byte[] root) {
        Objects.requireNonNull(request, "request");
        var decoded = ZLinkCanonicalActorRelocationEnvelope.decode(
            root,
            request.relocationId(),
            request.actorId(),
            request.restoreSnapshot());
        if (decoded.objectGeneration() != request.objectGeneration()
            || decoded.expectedAuthorityOwnerGeneration()
                != request.sourceAuthorityOwnerGeneration()) {
            return CompletableFuture.failedFuture(
                new IllegalArgumentException(
                    "Actor relocation root differs from the authority fence"));
        }
        return backend.prepare(request, decoded.state(), NOT_CANCELLED)
            .thenApply(prepared -> new Staged(
                request, decoded, prepared));
    }

    CompletionStage<Void> replayHidden(Staged staged) {
        return replayHidden(staged, staged.decoded());
    }

    CompletionStage<Void> publishAndReplayHidden(
        Staged staged,
        byte[] finalRoot) {
        requireActive(staged);
        var decoded = ZLinkCanonicalActorRelocationEnvelope.decode(
            finalRoot,
            staged.request().relocationId(),
            staged.request().actorId(),
            staged.request().restoreSnapshot());
        requireStagingPrefix(staged, decoded);
        return replayHidden(staged, decoded)
            .thenRun(() -> publish(staged));
    }

    private CompletionStage<Void> replayHidden(
        Staged staged,
        ZLinkCanonicalActorRelocationEnvelope.Decoded decoded) {
        requireActive(staged);
        CompletionStage<Void> chain =
            CompletableFuture.completedFuture(null);
        for (var queued : decoded.journal()) {
            chain = chain.thenCompose(ignored -> {
                ZLinkActorAcceptedJournal.Record record =
                    ZLinkActorAcceptedJournal.decode(queued.payload());
                if (!record.actorId().equals(staged.request().actorId())) {
                    return CompletableFuture.failedFuture(
                        new IllegalArgumentException(
                            "accepted journal references another Actor"));
                }
                return backend.replay(
                        staged.actor(), staged.request(), record)
                    .thenApply(reply -> null);
            });
        }
        return chain.thenRun(() -> staged.replayed = true);
    }

    private static void requireStagingPrefix(
        Staged staged,
        ZLinkCanonicalActorRelocationEnvelope.Decoded decoded) {
        var initial = staged.decoded();
        if (decoded.objectGeneration() != initial.objectGeneration()
            || decoded.expectedAuthorityOwnerGeneration()
                != initial.expectedAuthorityOwnerGeneration()
            || !java.util.Arrays.equals(decoded.state(), initial.state())) {
            throw new IllegalArgumentException(
                "authority-selected Actor root differs from staged state");
        }
        if (decoded.journal().size() < initial.journal().size()) {
            throw new IllegalArgumentException(
                "authority-selected Actor journal removed staged records");
        }
        for (int index = 0; index < initial.journal().size(); index++) {
            var left = initial.journal().get(index);
            var right = decoded.journal().get(index);
            if (left.sequence() != right.sequence()
                || !java.util.Arrays.equals(left.payload(), right.payload())) {
                throw new IllegalArgumentException(
                    "authority-selected Actor journal changed its prefix");
            }
        }
    }

    void publish(Staged staged) {
        requireActive(staged);
        if (!staged.replayed) {
            throw new IllegalStateException(
                "Actor accepted journal has not been replayed");
        }
        backend.publish(staged.actor());
        staged.published = true;
    }

    void openAdmission(Staged staged) {
        requireActive(staged);
        if (!staged.published) {
            throw new IllegalStateException(
                "Actor relocation is not published");
        }
        backend.openAdmission(staged.actor());
        staged.terminal = true;
    }

    CompletionStage<Void> discard(Staged staged) {
        Objects.requireNonNull(staged, "staged");
        synchronized (staged) {
            if (staged.published) {
                return CompletableFuture.failedFuture(
                    new IllegalStateException(
                        "published Actor relocation cannot be discarded"));
            }
            if (staged.terminal) {
                return CompletableFuture.completedFuture(null);
            }
            staged.terminal = true;
        }
        return backend.discard(staged.actor());
    }

    private static void requireActive(Staged staged) {
        Objects.requireNonNull(staged, "staged");
        if (staged.terminal) {
            throw new IllegalStateException(
                "Actor relocation target attempt is terminal");
        }
    }

    record Request(
        UUID relocationId,
        String actorId,
        String stableType,
        long objectGeneration,
        long sourceAuthorityOwnerGeneration,
        boolean restoreSnapshot,
        String targetSpotId) {
        Request {
            Objects.requireNonNull(relocationId, "relocationId");
            requireText(actorId, "actorId");
            requireText(stableType, "stableType");
            requireText(targetSpotId, "targetSpotId");
            if (relocationId.equals(new UUID(0, 0))
                || objectGeneration <= 0
                || sourceAuthorityOwnerGeneration <= 0) {
                throw new IllegalArgumentException(
                    "Actor relocation identity and generations must be positive");
            }
        }
    }

    static final class Staged {
        private final Request request;
        private final ZLinkCanonicalActorRelocationEnvelope.Decoded decoded;
        private final Object actor;
        private boolean replayed;
        private boolean published;
        private boolean terminal;

        private Staged(
            Request request,
            ZLinkCanonicalActorRelocationEnvelope.Decoded decoded,
            Object actor) {
            this.request = request;
            this.decoded = decoded;
            this.actor = actor;
        }

        Request request() {
            return request;
        }

        ZLinkCanonicalActorRelocationEnvelope.Decoded decoded() {
            return decoded;
        }

        Object actor() {
            return actor;
        }
    }

    interface Backend {
        CompletionStage<Object> prepare(
            Request request,
            byte[] state,
            ZLinkRelocationCancellation cancellation);

        CompletionStage<java.util.Optional<byte[]>> replay(
            Object actor,
            Request request,
            ZLinkActorAcceptedJournal.Record record);

        void publish(Object actor);

        void openAdmission(Object actor);

        CompletionStage<Void> discard(Object actor);
    }

    private static final class ProductionBackend implements Backend {
        private final RoutingId targetNodeRid;
        private final ZLinkActorSessionCoordinator actors;
        private final ZLinkRelocationAdapterRegistry adapters;
        private final ZLinkSpotRuntime spots;

        private ProductionBackend(
            RoutingId targetNodeRid,
            ZLinkActorSessionCoordinator actors,
            ZLinkRelocationAdapterRegistry adapters,
            ZLinkSpotRuntime spots) {
            this.targetNodeRid = targetNodeRid;
            this.actors = actors;
            this.adapters = adapters;
            this.spots = spots;
        }

        @Override
        public CompletionStage<Object> prepare(
            Request request,
            byte[] state,
            ZLinkRelocationCancellation cancellation) {
            return actors.prepareRelocatedActor(
                    request.actorId(),
                    request.stableType(),
                    state,
                    request.restoreSnapshot(),
                    adapters,
                    cancellation,
                    new ZLinkBackendActorRef(
                        targetNodeRid,
                        request.actorId(),
                        request.objectGeneration()))
                .thenApply(value -> value);
        }

        @Override
        public CompletionStage<java.util.Optional<byte[]>> replay(
            Object actor,
            Request request,
            ZLinkActorAcceptedJournal.Record record) {
            return spots.replayPreparedActorAtSpot(
                request.targetSpotId(),
                (ZLinkActorRuntime.PreparedTransferredActor) actor,
                record);
        }

        @Override
        public void publish(Object actor) {
            actors.publishRelocatedActor(
                (ZLinkActorRuntime.PreparedTransferredActor) actor);
        }

        @Override
        public void openAdmission(Object actor) {
            actors.openRelocatedActorAdmission(
                (ZLinkActorRuntime.PreparedTransferredActor) actor);
        }

        @Override
        public CompletionStage<Void> discard(Object actor) {
            return actors.discardRelocatedActor(
                (ZLinkActorRuntime.PreparedTransferredActor) actor);
        }
    }

    private static String requireText(String value, String name) {
        if (value == null || value.isBlank() || value.indexOf('\0') >= 0) {
            throw new IllegalArgumentException(name + " is invalid");
        }
        return value;
    }
}
