package systems.zlink.framework.runtime.spots;

import java.util.Collection;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.Executor;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.errors.ZlinkCloseException;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.errors.ZLinkFrameworkErrorKind;
import systems.zlink.framework.errors.ZLinkFrameworkException;
import systems.zlink.framework.locations.ZLinkLocationWriteStatus;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpot;
import systems.zlink.framework.runtime.internal.backend.ZLinkInternalSpotNode;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotCreateResult;
import systems.zlink.framework.spots.ZLinkSpotCreateState;
import systems.zlink.framework.spots.ZLinkSpotInfo;
import systems.zlink.framework.runtime.internal.metrics.ZLinkRuntimeMetrics;

final class ZLinkSpotLifecycle {
    @FunctionalInterface
    interface ActivationFactory {
        CompletionStage<SpotActivationCreateResult> activate(
            Class<? extends ZLinkSpot<?>> spotType,
            ZLinkBackendSpot backendSpot,
            ZLinkMessage request);
    }

    @FunctionalInterface
    interface ActorOccupancy {
        boolean hasActorsInSpot(RoutingId spotRid);
    }

    private final ZLinkInternalSpotNode primaryNode;
    private final Executor backendExecutor;
    private final ZLinkSpotLocationCoordinator locations;
    private final ActivationFactory activationFactory;
    private final ActorOccupancy actorOccupancy;
    private final Set<Class<? extends ZLinkSpot<?>>> registeredSpotTypes;
    private final List<EntrySpotActivation> entrySpots;
    private final Map<RoutingId, SpotActivation> spots = new ConcurrentHashMap<>();
    private final Map<RoutingId, CompletionStage<ZLinkSpotCreateResult>> pendingCreates =
        new ConcurrentHashMap<>();

    ZLinkSpotLifecycle(
        ZLinkInternalSpotNode primaryNode,
        Executor backendExecutor,
        ZLinkSpotLocationCoordinator locations,
        Collection<Class<? extends ZLinkSpot<?>>> registeredSpotTypes,
        ActivationFactory activationFactory,
        ActorOccupancy actorOccupancy) {
        this.primaryNode = primaryNode;
        this.backendExecutor = backendExecutor;
        this.locations = locations;
        this.registeredSpotTypes = Set.copyOf(registeredSpotTypes);
        this.entrySpots = new java.util.ArrayList<>();
        this.activationFactory = activationFactory;
        this.actorOccupancy = actorOccupancy;
    }

    void addEntrySpot(EntrySpotActivation activation) {
        entrySpots.add(activation);
        ZLinkRuntimeMetrics.add("zlink.spot.count", 1, Map.of("kind", "entry"));
        ZLinkRuntimeMetrics.increment("zlink.spot.created", Map.of("kind", "entry"));
    }

    CompletionStage<ZLinkSpotCreateResult> create(
        Class<? extends ZLinkSpot<?>> spotType,
        ZLinkMessage request) {
        requireRegistered(spotType);
        return createBackendSpotAsync()
            .thenCompose(backendSpot -> {
                RoutingId spotRid = backendSpot.routingId();
                if (spots.containsKey(spotRid)) {
                    backendSpot.close();
                    throw duplicateSpot(spotRid);
                }
                return activateAndClaim(spotType, backendSpot, request);
            });
    }

    CompletionStage<ZLinkSpotCreateResult> create(
        Class<? extends ZLinkSpot<?>> spotType,
        RoutingId spotRid,
        ZLinkMessage request) {
        requireRegistered(spotType);
        requireRoutingId(spotRid);
        if (spots.containsKey(spotRid) || pendingCreates.containsKey(spotRid)) {
            throw duplicateSpot(spotRid);
        }
        return beginCreate(spotType, spotRid, request, false);
    }

    CompletionStage<ZLinkSpotCreateResult> getOrCreate(
        Class<? extends ZLinkSpot<?>> spotType,
        RoutingId spotRid,
        ZLinkMessage request) {
        requireRegistered(spotType);
        requireRoutingId(spotRid);
        SpotActivation existing = spots.get(spotRid);
        if (existing != null) {
            if (existing.spot().getClass() != spotType) {
                throw new ZLinkConfigurationException("spot type mismatch: " + spotRid);
            }
            return CompletableFuture.completedFuture(existingResult(spotRid));
        }
        CompletionStage<ZLinkSpotCreateResult> pending = pendingCreates.get(spotRid);
        return pending == null
            ? beginCreate(spotType, spotRid, request, true)
            : asExisting(pending);
    }

    CompletionStage<Optional<ZLinkSpotInfo>> find(RoutingId spotRid) {
        requireRoutingId(spotRid);
        return CompletableFuture.completedFuture(
            spots.containsKey(spotRid)
                ? Optional.of(new ZLinkSpotInfo(spotRid))
                : Optional.empty());
    }

    CompletionStage<List<ZLinkSpotInfo>> list() {
        return CompletableFuture.completedFuture(
            spots.keySet().stream().map(ZLinkSpotInfo::new).toList());
    }

    CompletionStage<Boolean> close(RoutingId spotRid) {
        requireRoutingId(spotRid);
        if (actorOccupancy.hasActorsInSpot(spotRid)) {
            return CompletableFuture.completedFuture(false);
        }
        SpotActivation removed = spots.remove(spotRid);
        if (removed == null) {
            return CompletableFuture.completedFuture(false);
        }
        removed.close();
        return locations.releaseUserSpotAsync(primaryNode.routingId(), spotRid)
            .whenComplete((ignored, error) -> {
                ZLinkRuntimeMetrics.add("zlink.spot.count", -1, Map.of("kind", "user"));
                ZLinkRuntimeMetrics.increment("zlink.spot.closed", Map.of("kind", "user"));
            })
            .thenApply(ignored -> true);
    }

    void drainRoutedDispatchQueues() {
        for (EntrySpotActivation activation : entrySpots) {
            activation.drainPolledDispatchQueues();
        }
        for (SpotActivation activation : spots.values()) {
            activation.drainPolledDispatchQueues();
        }
    }

    void sealApplicationAdmission() {
        for (EntrySpotActivation activation : entrySpots) {
            activation.context.closeTimers();
        }
        for (SpotActivation activation : spots.values()) {
            activation.context.closeTimers();
        }
    }

    CompletionStage<Void> awaitApplicationTurns() {
        List<CompletableFuture<Void>> barriers = new java.util.ArrayList<>();
        for (EntrySpotActivation activation : entrySpots) {
            barriers.add(activation.context.enqueueDispatch(
                () -> CompletableFuture.completedFuture(null)).toCompletableFuture());
        }
        for (SpotActivation activation : spots.values()) {
            barriers.add(activation.context.enqueueDispatch(
                () -> CompletableFuture.completedFuture(null)).toCompletableFuture());
        }
        return CompletableFuture.allOf(barriers.toArray(CompletableFuture[]::new));
    }

    CompletionStage<Void> notifyEntrySpotActorCreated(
        RoutingId nodeRid,
        ZLinkActor actor,
        ZLinkMessage createRequest,
        Object createContext) {
        for (EntrySpotActivation activation : entrySpots) {
            if (activation.context.nodeRid().equals(nodeRid)) {
                return activation.notifyActorCreated(actor, createRequest, createContext);
            }
        }
        return java.util.concurrent.CompletableFuture.completedFuture(null);
    }

    ZLinkSpot<?> spotFor(RoutingId spotRid) {
        SpotActivation activation = spots.get(spotRid);
        return activation == null ? null : activation.spot();
    }

    DefaultSpotContext contextFor(ZLinkSpot<?> spot) {
        for (SpotActivation activation : spots.values()) {
            if (activation.spot() == spot) {
                return activation.context;
            }
        }
        return null;
    }

    boolean hasUserSpot(RoutingId spotRid) {
        return spots.containsKey(spotRid);
    }

    Object spotSurfaceFor(RoutingId spotRid) {
        SpotActivation activation = spots.get(spotRid);
        if (activation != null) {
            return activation.spot();
        }
        EntrySpotActivation entry = entrySpotActivationFor(spotRid);
        return entry == null ? null : entry.entrySpot();
    }

    EntrySpotActivation entrySpotActivationFor(RoutingId spotRid) {
        for (EntrySpotActivation activation : entrySpots) {
            if (activation.backendSpot.routingId().equals(spotRid)) {
                return activation;
            }
        }
        return null;
    }

    Object firstEntrySpot() {
        return entrySpots.isEmpty() ? null : entrySpots.get(0).entrySpot();
    }

    void closeAll() {
        closeAllAsync();
    }

    CompletionStage<Void> closeAllAsync() {
        java.util.concurrent.atomic.AtomicReference<RuntimeException> firstFailure =
            new java.util.concurrent.atomic.AtomicReference<>();
        List<EntrySpotActivation> closingEntrySpots = List.copyOf(entrySpots);
        List<SpotActivation> closingSpots = List.copyOf(spots.values());
        for (EntrySpotActivation entrySpot : closingEntrySpots) {
            recordCloseFailure(firstFailure, closeComponent(entrySpot::close, null));
        }
        for (SpotActivation spot : closingSpots) {
            recordCloseFailure(firstFailure, closeComponent(spot::close, null));
        }
        if (!entrySpots.isEmpty()) {
            ZLinkRuntimeMetrics.add("zlink.spot.count", -entrySpots.size(), Map.of("kind", "entry"));
        }
        entrySpots.clear();
        if (!spots.isEmpty()) {
            ZLinkRuntimeMetrics.add("zlink.spot.count", -spots.size(), Map.of("kind", "user"));
        }
        spots.clear();
        List<CompletableFuture<Void>> cleanups = new java.util.ArrayList<>();
        for (EntrySpotActivation entrySpot : closingEntrySpots) {
            cleanups.add(locations.releaseEntrySpotAsync(entrySpot.context.nodeRid())
                .handle((ignored, error) -> {
                    recordCloseFailure(firstFailure, error);
                    return (Void) null;
                }).toCompletableFuture());
        }
        for (SpotActivation spot : closingSpots) {
            cleanups.add(locations.releaseUserSpotAsync(
                    primaryNode.routingId(), spot.backendSpot.routingId())
                .handle((ignored, error) -> {
                    recordCloseFailure(firstFailure, error);
                    return (Void) null;
                }).toCompletableFuture());
        }
        return CompletableFuture.allOf(cleanups.toArray(CompletableFuture[]::new))
            .thenCompose(ignored -> firstFailure.get() == null
                ? CompletableFuture.completedFuture(null)
                : CompletableFuture.failedFuture(firstFailure.get()));
    }

    private static void recordCloseFailure(
        java.util.concurrent.atomic.AtomicReference<RuntimeException> target,
        Throwable error) {
        if (error == null) {
            return;
        }
        Throwable value = error;
        while (value instanceof java.util.concurrent.CompletionException && value.getCause() != null) {
            value = value.getCause();
        }
        RuntimeException failure = value instanceof RuntimeException runtime
            ? runtime : new RuntimeException(value);
        RuntimeException first = target.get();
        if (first == null) {
            target.compareAndSet(null, failure);
        } else {
            first.addSuppressed(failure);
        }
    }

    CompletionStage<Void> releaseRecreatableSpots() {
        List<RoutingId> spotRids = List.copyOf(spots.keySet());
        for (RoutingId spotRid : spotRids) {
            if (actorOccupancy.hasActorsInSpot(spotRid)) {
                return CompletableFuture.failedFuture(new IllegalStateException(
                    "recreatable spot still has actors: " + spotRid));
            }
        }
        List<SpotActivation> released = new java.util.ArrayList<>(spotRids.size());
        for (RoutingId spotRid : spotRids) {
            SpotActivation activation = spots.remove(spotRid);
            if (activation != null) {
                released.add(activation);
            }
        }
        java.util.concurrent.atomic.AtomicReference<RuntimeException> firstFailure =
            new java.util.concurrent.atomic.AtomicReference<>();
        for (SpotActivation activation : released) {
            recordCloseFailure(firstFailure, closeComponent(activation::close, null));
        }
        List<CompletableFuture<Void>> cleanups = new java.util.ArrayList<>(released.size());
        for (SpotActivation activation : released) {
            cleanups.add(locations.releaseUserSpotAsync(
                    primaryNode.routingId(), activation.backendSpot.routingId())
                .handle((ignored, error) -> {
                    recordCloseFailure(firstFailure, error);
                    ZLinkRuntimeMetrics.add("zlink.spot.count", -1, Map.of("kind", "user"));
                    ZLinkRuntimeMetrics.increment("zlink.spot.closed", Map.of("kind", "user"));
                    return (Void) null;
                }).toCompletableFuture());
        }
        return CompletableFuture.allOf(cleanups.toArray(CompletableFuture[]::new))
            .thenCompose(ignored -> firstFailure.get() == null
                ? CompletableFuture.completedFuture(null)
                : CompletableFuture.failedFuture(firstFailure.get()));
    }

    boolean userSpotsDrained() {
        return spots.isEmpty() && pendingCreates.isEmpty();
    }

    int userSpotCount() {
        return spots.size();
    }

    private CompletionStage<ZLinkSpotCreateResult> beginCreate(
        Class<? extends ZLinkSpot<?>> spotType,
        RoutingId spotRid,
        ZLinkMessage request,
        boolean reuseConcurrentCreate) {
        CompletableFuture<ZLinkSpotCreateResult> result = new CompletableFuture<>();
        CompletionStage<ZLinkSpotCreateResult> concurrent =
            pendingCreates.putIfAbsent(spotRid, result);
        if (concurrent != null) {
            if (reuseConcurrentCreate) {
                return asExisting(concurrent);
            }
            throw duplicateSpot(spotRid);
        }
        try {
            createBackendSpotAsync(spotRid)
                .thenCompose(backendSpot -> activationFactory
                    .activate(spotType, backendSpot, request)
                    .thenCompose(created -> createResultAsync(
                        spotRid,
                        backendSpot.lifecycleGeneration(),
                        spotType,
                        created)))
                .whenComplete((created, error) -> {
                    pendingCreates.remove(spotRid, result);
                    if (error == null) {
                        result.complete(created);
                    } else {
                        result.completeExceptionally(error);
                    }
                });
        } catch (RuntimeException error) {
            pendingCreates.remove(spotRid, result);
            result.completeExceptionally(error);
        }
        return result;
    }

    private CompletionStage<ZLinkSpotCreateResult> activateAndClaim(
        Class<? extends ZLinkSpot<?>> spotType,
        ZLinkBackendSpot backendSpot,
        ZLinkMessage request) {
        RoutingId spotRid = backendSpot.routingId();
        return activationFactory.activate(spotType, backendSpot, request)
            .thenCompose(result -> createResultAsync(
                spotRid,
                backendSpot.lifecycleGeneration(),
                spotType,
                result));
    }

    private CompletionStage<ZLinkSpotCreateResult> createResultAsync(
        RoutingId spotRid,
        long spotGeneration,
        Class<? extends ZLinkSpot<?>> spotType,
        SpotActivationCreateResult result) {
        if (!result.response().accepted()) {
            return CompletableFuture.completedFuture(new ZLinkSpotCreateResult(
                spotRid,
                ZLinkSpotCreateState.REJECTED,
                result.response().reply()));
        }
        SpotActivation activation = result.activation();
        return locations.claimUserSpotAsync(
                primaryNode.routingId(),
                spotRid,
                spotGeneration,
                spotType,
                () -> close(spotRid))
            .thenApply(status -> {
                if (status != ZLinkLocationWriteStatus.STORED) {
                    throw spotCreateLocationFailure(spotRid, status);
                }
                spots.put(spotRid, activation);
                ZLinkRuntimeMetrics.add("zlink.spot.count", 1, Map.of("kind", "user"));
                ZLinkRuntimeMetrics.increment("zlink.spot.created", Map.of("kind", "user"));
                return new ZLinkSpotCreateResult(
                    spotRid,
                    ZLinkSpotCreateState.CREATED,
                    result.response().reply());
            })
            .whenComplete((ignored, error) -> {
                if (error != null) {
                    activation.close();
                }
            });
    }

    private CompletionStage<ZLinkBackendSpot> createBackendSpotAsync() {
        return CompletableFuture.supplyAsync(primaryNode::createSpot, backendExecutor);
    }

    private CompletionStage<ZLinkBackendSpot> createBackendSpotAsync(RoutingId spotRid) {
        return CompletableFuture.supplyAsync(
            () -> primaryNode.createSpot(spotRid),
            backendExecutor);
    }

    private void requireRegistered(Class<? extends ZLinkSpot<?>> spotType) {
        if (spotType == null) {
            throw new ZLinkConfigurationException("spot type is required");
        }
        if (!registeredSpotTypes.contains(spotType)) {
            throw new ZLinkConfigurationException(
                "spot type is not registered: " + spotType.getName());
        }
    }

    private static void requireRoutingId(RoutingId spotRid) {
        if (spotRid == null) {
            throw new ZLinkConfigurationException("spotRid is required");
        }
    }

    private static CompletionStage<ZLinkSpotCreateResult> asExisting(
        CompletionStage<ZLinkSpotCreateResult> create) {
        return create.thenApply(result -> result.state() == ZLinkSpotCreateState.CREATED
            ? new ZLinkSpotCreateResult(
                result.spotRid(),
                ZLinkSpotCreateState.EXISTING,
                result.reply())
            : result);
    }

    private static ZLinkSpotCreateResult existingResult(RoutingId spotRid) {
        return new ZLinkSpotCreateResult(spotRid, ZLinkSpotCreateState.EXISTING, null);
    }

    private static ZLinkConfigurationException duplicateSpot(RoutingId spotRid) {
        return new ZLinkConfigurationException("duplicate spot rid: " + spotRid);
    }

    private static ZLinkFrameworkException spotCreateLocationFailure(
        RoutingId spotRid,
        ZLinkLocationWriteStatus status) {
        String message = status == ZLinkLocationWriteStatus.REJECTED_CONFLICT
            ? "SPOT '" + spotRid + "' location is owned by another runtime."
            : "SPOT '" + spotRid
                + "' location claim failed because the location store is unavailable.";
        return new ZLinkFrameworkException(ZLinkFrameworkErrorKind.SPOT_CREATE_FAILED, message);
    }

    private static RuntimeException closeComponent(
        Runnable close,
        RuntimeException firstFailure) {
        try {
            close.run();
        } catch (ZlinkCloseException ignored) {
        } catch (RuntimeException error) {
            if (firstFailure == null) {
                return error;
            }
            firstFailure.addSuppressed(error);
        }
        return firstFailure;
    }
}
