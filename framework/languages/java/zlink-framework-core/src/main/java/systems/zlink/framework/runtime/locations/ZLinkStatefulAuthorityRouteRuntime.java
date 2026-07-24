package systems.zlink.framework.runtime.locations;

import java.time.Duration;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.Executors;
import java.util.concurrent.Flow;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.Consumer;
import systems.zlink.framework.locations.ZLinkAuthorityPage;
import systems.zlink.framework.locations.ZLinkAuthorityScanCursor;
import systems.zlink.framework.locations.ZLinkAuthorityScanExpired;
import systems.zlink.framework.locations.ZLinkAuthoritySnapshot;
import systems.zlink.framework.locations.ZLinkAuthorityStore;
import systems.zlink.framework.locations.ZLinkLocationChanged;
import systems.zlink.framework.locations.ZLinkLocationKind;
import systems.zlink.framework.locations.ZLinkLocationWatchFilter;
import systems.zlink.framework.locations.ZLinkLocationWatchStore;
import systems.zlink.framework.locations.ZLinkPlacementAllocationState;
import systems.zlink.framework.locations.ZLinkPlacementObjectKind;
import systems.zlink.framework.locations.ZLinkRouteKind;
import systems.zlink.framework.locations.ZLinkStoreCancellation;
import systems.zlink.framework.runtime.internal.backend.ZLinkInternalMeshNode;
import systems.zlink.framework.runtime.service.ZLinkServiceM6BWireCodec;

/**
 * Reconciles durable Spot authority into fenced raw routes. A full scan is
 * applied atomically; an expired scan keeps the previous route set.
 */
public final class ZLinkStatefulAuthorityRouteRuntime
    implements AutoCloseable {
    private static final ZLinkStoreCancellation OPEN = () -> false;

    private final ZLinkAuthorityStore store;
    private final ZLinkLocationWatchStore watchStore;
    private final Map<String, ZLinkInternalMeshNode> meshNodes;
    private final Duration pollingInterval;
    private final Consumer<Throwable> reportFailure;
    private final ZLinkServiceAuthorityPayloadCodec payloadCodec =
        new ZLinkServiceAuthorityPayloadCodec();
    private final ScheduledExecutorService executor =
        Executors.newSingleThreadScheduledExecutor(
            Thread.ofVirtual()
                .name("zlink-jvm-authority-routes")
                .factory());
    private final AtomicBoolean inFlight = new AtomicBoolean();
    private final AtomicBoolean watchSignaled = new AtomicBoolean();
    private final List<Flow.Subscription> subscriptions =
        new ArrayList<>();
    private final Map<String, Applied> applied = new HashMap<>();
    private volatile boolean started;
    private volatile boolean closed;

    public ZLinkStatefulAuthorityRouteRuntime(
        ZLinkAuthorityStore store,
        ZLinkLocationWatchStore watchStore,
        Map<String, ZLinkInternalMeshNode> meshNodes,
        Duration pollingInterval,
        Consumer<Throwable> reportFailure) {
        this.store = java.util.Objects.requireNonNull(store, "store");
        this.watchStore = watchStore;
        this.meshNodes = Map.copyOf(
            java.util.Objects.requireNonNull(
                meshNodes, "meshNodes"));
        this.pollingInterval = java.util.Objects.requireNonNull(
            pollingInterval, "pollingInterval");
        if (pollingInterval.isNegative()
            || pollingInterval.isZero()) {
            throw new IllegalArgumentException(
                "pollingInterval must be positive");
        }
        this.reportFailure = java.util.Objects.requireNonNull(
            reportFailure, "reportFailure");
    }

    public CompletionStage<Void> start() {
        return reconcile()
            .thenRun(this::subscribe)
            .thenCompose(ignored -> reconcile())
            .thenRun(() -> {
                started = true;
                executor.scheduleWithFixedDelay(
                    this::poll,
                    pollingInterval.toMillis(),
                    pollingInterval.toMillis(),
                    TimeUnit.MILLISECONDS);
                if (watchSignaled.get()) {
                    executor.execute(this::poll);
                }
            });
    }

    public CompletionStage<Void> reconcile() {
        return scan(Optional.empty(), new HashMap<>())
            .thenAccept(this::apply);
    }

    private CompletionStage<Map<String, Applied>> scan(
        Optional<ZLinkAuthorityScanCursor> cursor,
        Map<String, Applied> routes) {
        return store.list(
                ZLinkAuthorityKeyCodec.spotPrefix(),
                cursor,
                1000,
                OPEN)
            .thenCompose(result -> {
                if (result instanceof ZLinkAuthorityScanExpired) {
                    return CompletableFuture.failedFuture(
                        new IllegalStateException(
                            "authority scan expired"));
                }
                ZLinkAuthorityPage page =
                    (ZLinkAuthorityPage) result;
                for (var entry : page.items()) {
                    decode(entry.snapshot()).ifPresent(value ->
                        routes.put(entry.key(), value));
                }
                return page.nextCursor().isEmpty()
                    ? CompletableFuture.completedFuture(routes)
                    : scan(page.nextCursor(), routes);
            });
    }

    private Optional<Applied> decode(
        ZLinkAuthoritySnapshot snapshot) {
        if (snapshot.allocation().objectKind()
                    != ZLinkPlacementObjectKind.USER_SPOT
                && snapshot.allocation().objectKind()
                    != ZLinkPlacementObjectKind.INSTANCE_SPOT) {
            return Optional.empty();
        }
        return payloadCodec.decode(snapshot.payload())
            .filter(value -> {
                boolean ready =
                    snapshot.allocation().state()
                        == ZLinkPlacementAllocationState.ACTIVE
                    && value.state()
                        == ZLinkServiceAuthorityPayloadCodec.State.READY;
                boolean coldActivation =
                    snapshot.allocation().state()
                        == ZLinkPlacementAllocationState.PENDING
                    && value.kind()
                        == ZLinkServiceAuthorityPayloadCodec.Kind.INSTANCE
                    && value.state()
                        == ZLinkServiceAuthorityPayloadCodec.State.CREATING;
                return (ready || coldActivation)
                && value.ownerId().equals(snapshot.ownerId())
                && value.ownerLeaseGeneration()
                    == snapshot.ownerLeaseGeneration()
                && value.meshName().equals(
                    snapshot.allocation().descriptor().meshName())
                && value.nodeRid().equals(
                    snapshot.allocation().descriptor().rid())
                && value.nodeGeneration()
                    == snapshot.allocation()
                        .descriptorLifecycleGeneration();
            })
            .map(value -> {
                var route =
                    new ZLinkInternalMeshNode.SpotAuthorityRoute(
                        value.spotId(),
                        snapshot.objectGeneration(),
                        value.nodeRid(),
                        value.nodeGeneration(),
                        snapshot.authorityOwnerGeneration(),
                        snapshot.ownerLeaseGeneration(),
                        snapshot.ownerId(),
                        value.meshName(),
                        snapshot.storeVersion());
                var instance =
                    new ZLinkServiceM6BWireCodec.InstanceRouteFence(
                        value.nodeRid(),
                        value.nodeGeneration(),
                        value.spotId(),
                        snapshot.objectGeneration(),
                        snapshot.ownerId(),
                        snapshot.authorityOwnerGeneration(),
                        snapshot.ownerLeaseGeneration(),
                        snapshot.storeVersion());
                return new Applied(
                    value.kind(),
                    value.stableType(),
                    value.meshName(),
                    snapshot.allocation().state()
                        == ZLinkPlacementAllocationState.ACTIVE,
                    route,
                    instance);
            });
    }

    private synchronized void apply(Map<String, Applied> next) {
        for (Map.Entry<String, Applied> old : applied.entrySet()) {
            Applied current = next.get(old.getKey());
            if (!old.getValue().equals(current)) {
                forget(old.getValue());
            }
        }
        for (Map.Entry<String, Applied> entry : next.entrySet()) {
            if (!entry.getValue().equals(applied.get(entry.getKey()))) {
                remember(entry.getValue());
            }
        }
        applied.clear();
        applied.putAll(next);
    }

    private void remember(Applied value) {
        ZLinkInternalMeshNode node =
            meshNodes.get(value.meshName());
        if (node == null) {
            return;
        }
        if (value.ready()) {
            node.rememberSpotAuthority(value.route());
        }
        if (value.kind()
                == ZLinkServiceAuthorityPayloadCodec.Kind.INSTANCE) {
            node.registerInstanceIntent(
                value.stableType(), value.instance());
        }
    }

    private void forget(Applied value) {
        ZLinkInternalMeshNode node =
            meshNodes.get(value.meshName());
        if (node == null) {
            return;
        }
        if (value.ready()) {
            node.forgetSpotAuthority(value.route());
        }
        if (value.kind()
                == ZLinkServiceAuthorityPayloadCodec.Kind.INSTANCE) {
            node.forgetInstanceIntent(value.instance());
        }
    }

    private void poll() {
        if (closed || !started
            || !inFlight.compareAndSet(false, true)) {
            return;
        }
        watchSignaled.set(false);
        reconcile().whenComplete((ignored, failure) -> {
            inFlight.set(false);
            if (failure != null && !closed) {
                reportFailure.accept(unwrap(failure));
            }
            if (watchSignaled.get() && !closed) {
                executor.execute(this::poll);
            }
        });
    }

    private void subscribe() {
        if (watchStore == null) {
            return;
        }
        for (String meshName : meshNodes.keySet()) {
            try {
                watchStore.watch(new ZLinkLocationWatchFilter(
                    ZLinkLocationKind.SPOT,
                    meshName,
                    ZLinkRouteKind.INVALID))
                    .subscribe(new AuthoritySubscriber());
            } catch (RuntimeException failure) {
                reportFailure.accept(failure);
            }
        }
    }

    @Override
    public synchronized void close() {
        closed = true;
        subscriptions.forEach(Flow.Subscription::cancel);
        subscriptions.clear();
        executor.shutdownNow();
        applied.values().forEach(this::forget);
        applied.clear();
    }

    private static Throwable unwrap(Throwable failure) {
        return failure instanceof java.util.concurrent.CompletionException
                && failure.getCause() != null
            ? failure.getCause()
            : failure;
    }

    private record Applied(
        ZLinkServiceAuthorityPayloadCodec.Kind kind,
        String stableType,
        String meshName,
        boolean ready,
        ZLinkInternalMeshNode.SpotAuthorityRoute route,
        ZLinkServiceM6BWireCodec.InstanceRouteFence instance) {
    }

    private final class AuthoritySubscriber
        implements Flow.Subscriber<ZLinkLocationChanged> {
        @Override
        public void onSubscribe(Flow.Subscription subscription) {
            synchronized (ZLinkStatefulAuthorityRouteRuntime.this) {
                if (closed) {
                    subscription.cancel();
                    return;
                }
                subscriptions.add(subscription);
            }
            subscription.request(Long.MAX_VALUE);
        }

        @Override
        public void onNext(ZLinkLocationChanged ignored) {
            watchSignaled.set(true);
            if (started) {
                executor.execute(
                    ZLinkStatefulAuthorityRouteRuntime.this::poll);
            }
        }

        @Override
        public void onError(Throwable failure) {
            if (!closed) {
                reportFailure.accept(failure);
            }
        }

        @Override
        public void onComplete() {
            // Periodic reconciliation remains the durable fallback.
        }
    }
}
