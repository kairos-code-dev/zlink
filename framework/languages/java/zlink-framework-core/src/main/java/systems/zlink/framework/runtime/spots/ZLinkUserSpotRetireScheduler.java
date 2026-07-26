package systems.zlink.framework.runtime.spots;

import java.util.List;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.locations.ZLinkStoreCancellation;
import systems.zlink.framework.runtime.internal.locations
    .ZLinkAggregateRelocationCoordinator;

/**
 * Owns the committed User Spot relocation completion order. Authority commit
 * is the rollback boundary; target admission remains closed through journal
 * replay, source cleanup, Completed publication and every binding-route ACK.
 */
final class ZLinkUserSpotRetireScheduler {
    private final Backend backend;

    ZLinkUserSpotRetireScheduler(
        ZLinkAggregateRelocationCoordinator coordinator,
        ZLinkUserSpotAggregateStagingOwner target) {
        backend = new ProductionBackend(
            Objects.requireNonNull(coordinator, "coordinator"),
            Objects.requireNonNull(target, "target"));
    }

    ZLinkUserSpotRetireScheduler(Backend backend) {
        this.backend = Objects.requireNonNull(backend, "backend");
    }

    CompletionStage<Result> execute(
        Request request,
        ZLinkStoreCancellation cancellation) {
        Objects.requireNonNull(request, "request");
        Objects.requireNonNull(cancellation, "cancellation");
        return backend.commit(request.prepared(), cancellation)
            .handle((published, failure) -> new CommitAttempt(
                published,
                failure == null ? null : unwrap(failure)))
            .thenCompose(attempt -> attempt.failure() == null
                ? finishCommitted(request, attempt.published(), cancellation)
                : abortPrecommit(request, attempt.failure()));
    }

    private CompletionStage<Result> finishCommitted(
        Request request,
        ZLinkAggregateRelocationCoordinator.Published published,
        ZLinkStoreCancellation cancellation) {
        return backend.publishAndReplay(request.staged(), request.replayer())
            .thenCompose(ignored -> request.sourceCleanup().cleanup())
            .thenCompose(ignored -> backend.completeSourceCleanup(
                published,
                request.completedRoot(),
                cancellation))
            .thenCompose(completed -> switchBindingRoutes(
                    request.bindingRoutes())
                .thenCompose(ignored -> request.normalizer().normalize())
                .thenRun(request.admission()::open)
                .thenApply(ignored -> new Result(published, completed)));
    }

    private CompletionStage<Void> switchBindingRoutes(
        List<BindingRouteSwitch> routes) {
        CompletionStage<Void> chain = CompletableFuture.completedFuture(null);
        for (BindingRouteSwitch route : routes) {
            chain = chain.thenCompose(ignored -> route.switchAndAwaitAck());
        }
        return chain;
    }

    private CompletionStage<Result> abortPrecommit(
        Request request,
        Throwable original) {
        return backend.abort(request.prepared())
            .thenCompose(ignored -> backend.discard(request.staged()))
            .thenCompose(ignored -> request.sourceSeal().resume())
            .handle((ignored, cleanupFailure) -> {
                if (cleanupFailure != null) {
                    original.addSuppressed(unwrap(cleanupFailure));
                }
                throw new CompletionException(original);
            });
    }

    private static Throwable unwrap(Throwable failure) {
        Throwable current = failure;
        while (current instanceof CompletionException
            && current.getCause() != null) {
            current = current.getCause();
        }
        return current;
    }

    record Request(
        ZLinkAggregateRelocationCoordinator.Prepared prepared,
        ZLinkUserSpotAggregateStagingOwner.Staged staged,
        ZLinkUserSpotAggregateStagingOwner.JournalReplayer replayer,
        SourceCleanup sourceCleanup,
        byte[] completedRoot,
        List<BindingRouteSwitch> bindingRoutes,
        SteadyNormalizer normalizer,
        Admission admission,
        SourceSeal sourceSeal) {
        Request {
            Objects.requireNonNull(prepared, "prepared");
            Objects.requireNonNull(staged, "staged");
            Objects.requireNonNull(replayer, "replayer");
            Objects.requireNonNull(sourceCleanup, "sourceCleanup");
            completedRoot = Objects.requireNonNull(
                completedRoot,
                "completedRoot").clone();
            bindingRoutes = List.copyOf(Objects.requireNonNull(
                bindingRoutes,
                "bindingRoutes"));
            Objects.requireNonNull(normalizer, "normalizer");
            Objects.requireNonNull(admission, "admission");
            Objects.requireNonNull(sourceSeal, "sourceSeal");
        }

        @Override public byte[] completedRoot() {
            return completedRoot.clone();
        }
    }

    record Result(
        ZLinkAggregateRelocationCoordinator.Published activated,
        ZLinkAggregateRelocationCoordinator.Published completed) {
    }

    @FunctionalInterface
    interface SourceCleanup {
        CompletionStage<Void> cleanup();
    }

    @FunctionalInterface
    interface BindingRouteSwitch {
        CompletionStage<Void> switchAndAwaitAck();
    }

    @FunctionalInterface
    interface SteadyNormalizer {
        CompletionStage<Void> normalize();
    }

    @FunctionalInterface
    interface SourceSeal {
        CompletionStage<Void> resume();
    }

    @FunctionalInterface
    interface Admission {
        void open();
    }

    interface Backend {
        CompletionStage<ZLinkAggregateRelocationCoordinator.Published> commit(
            ZLinkAggregateRelocationCoordinator.Prepared prepared,
            ZLinkStoreCancellation cancellation);

        CompletionStage<Void> publishAndReplay(
            ZLinkUserSpotAggregateStagingOwner.Staged staged,
            ZLinkUserSpotAggregateStagingOwner.JournalReplayer replayer);

        CompletionStage<ZLinkAggregateRelocationCoordinator.Published>
            completeSourceCleanup(
                ZLinkAggregateRelocationCoordinator.Published published,
                byte[] completedRoot,
                ZLinkStoreCancellation cancellation);

        CompletionStage<Void> abort(
            ZLinkAggregateRelocationCoordinator.Prepared prepared);

        CompletionStage<Void> discard(
            ZLinkUserSpotAggregateStagingOwner.Staged staged);
    }

    private record ProductionBackend(
        ZLinkAggregateRelocationCoordinator coordinator,
        ZLinkUserSpotAggregateStagingOwner target) implements Backend {
        @Override
        public CompletionStage<ZLinkAggregateRelocationCoordinator.Published>
            commit(
                ZLinkAggregateRelocationCoordinator.Prepared prepared,
                ZLinkStoreCancellation cancellation) {
            return coordinator.commit(prepared, cancellation);
        }

        @Override
        public CompletionStage<Void> publishAndReplay(
            ZLinkUserSpotAggregateStagingOwner.Staged staged,
            ZLinkUserSpotAggregateStagingOwner.JournalReplayer replayer) {
            return target.publishAndReplay(staged, replayer);
        }

        @Override
        public CompletionStage<ZLinkAggregateRelocationCoordinator.Published>
            completeSourceCleanup(
                ZLinkAggregateRelocationCoordinator.Published published,
                byte[] completedRoot,
                ZLinkStoreCancellation cancellation) {
            return coordinator.completeSourceCleanup(
                published,
                completedRoot,
                cancellation);
        }

        @Override
        public CompletionStage<Void> abort(
            ZLinkAggregateRelocationCoordinator.Prepared prepared) {
            return coordinator.abort(prepared);
        }

        @Override
        public CompletionStage<Void> discard(
            ZLinkUserSpotAggregateStagingOwner.Staged staged) {
            return target.discard(staged);
        }
    }

    private record CommitAttempt(
        ZLinkAggregateRelocationCoordinator.Published published,
        Throwable failure) {
    }
}
