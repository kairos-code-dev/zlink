package systems.zlink.framework.runtime.spots;

import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.concurrent.CompletionStage;
import java.util.function.Supplier;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.configuration.ZLinkUserSpotExecutionMode;
import systems.zlink.framework.execution.ZLinkAsyncSerialQueue;
import systems.zlink.framework.execution.ZLinkWorkerPool;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendSpot;
import systems.zlink.framework.runtime.internal.handlers.ZLinkHandlerInstanceOwner;
import systems.zlink.framework.runtime.internal.metrics.ZLinkRuntimeMetrics;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.spots.ZLinkSpotHandlerRegistry;
import systems.zlink.framework.spots.ZLinkSpotOutbound;
import systems.zlink.framework.spots.ZLinkTimer;
import systems.zlink.framework.spots.ZLinkTimerOptions;
import systems.zlink.framework.spots.ZLinkWorkerCall;
import systems.zlink.framework.spots.ZLinkIoWorkerTask;
import systems.zlink.framework.spots.ZLinkWorkerTask;

final class DefaultEntrySpotContext implements ZLinkEntrySpotContext, SpotDispatchLine {
    private final ZLinkSpotContextHost host;
    private final ZLinkWorkerPool workerPool;
    private final ZLinkSpotHandlerLoader handlerLoader;
    private final RoutingId nodeRid;
    private final ZLinkBackendSpot backendSpot;
    private final DefaultSpotOutbound outbound;
    private final ZLinkAsyncSerialQueue dispatchQueue = new ZLinkAsyncSerialQueue();
    private final ZLinkHandlerInstanceOwner handlerInstances;
    private final List<DefaultSpotContext> timerContexts = new ArrayList<>();
    private final ZLinkSpotHandlerCatalog handlerCatalog = new ZLinkSpotHandlerCatalog(
        "EntrySpot handler registration is only allowed while configure is running");
    private ZLinkEntrySpot<?> entrySpot;

    DefaultEntrySpotContext(
        ZLinkSpotContextHost host,
        ZLinkWorkerPool workerPool,
        ZLinkSpotHandlerLoader handlerLoader,
        RoutingId nodeRid,
        ZLinkBackendSpot backendSpot) {
        this.host = host;
        this.workerPool = workerPool;
        this.handlerLoader = handlerLoader;
        this.nodeRid = nodeRid;
        this.backendSpot = backendSpot;
        this.outbound = host.createContextOutbound(backendSpot, nodeRid);
        this.handlerInstances = host.createHandlerInstances();
    }

    @Override public String spotId() { return backendSpot.spotId(); }
    @Override public long objectGeneration() {
        return backendSpot.lifecycleGeneration();
    }
    @Override public RoutingId nodeRid() { return nodeRid; }
    @Override public ZLinkSpotOutbound outbound() { return outbound; }
    @Override public DefaultSpotOutbound dispatchOutbound() { return outbound; }
    @Override public ZLinkSpotHandlerRegistry handlers() { return handlerCatalog; }
    @Override public ZLinkSpotHandlerCatalog handlerCatalog() { return handlerCatalog; }

    void setEntrySpot(ZLinkEntrySpot<?> entrySpot) {
        this.entrySpot = entrySpot;
    }

    @Override
    public CompletionStage<Void> destroyActor(ZLinkActor actor) {
        return host.destroyActorFromEntry(nodeRid, actor);
    }

    @Override
    public CompletionStage<ZLinkTimer> addTimer(
        String name,
        Duration period,
        Class<?> handlerType,
        ZLinkTimerOptions options) {
        DefaultSpotContext timerContext = new DefaultSpotContext(
            host,
            workerPool,
            handlerLoader,
            nodeRid,
            backendSpot,
            dispatchQueue,
            ZLinkUserSpotExecutionMode.PER_ACTOR,
            false,
            handlerInstances);
        timerContext.setSpot(new ZLinkEntrySpotTimerSurface(this));
        timerContexts.add(timerContext);
        return timerContext.addTimer(name, period, handlerType, options);
    }

    void closeTimers() {
        timerContexts.forEach(DefaultSpotContext::closeTimers);
    }

    @Override
    public ZLinkHandlerInstanceOwner handlerInstances() {
        return handlerInstances;
    }

    void closeHandlerInstances() {
        handlerInstances.close();
    }

    void sealTimerAdmission() {
        timerContexts.forEach(DefaultSpotContext::sealTimerAdmission);
    }

    CompletionStage<Void> awaitAllLanes() {
        List<CompletionStage<Void>> lanes = new ArrayList<>();
        lanes.add(dispatchQueue.awaitQuiescence());
        timerContexts.forEach(context -> lanes.add(context.awaitAllLanes()));
        return java.util.concurrent.CompletableFuture.allOf(
            lanes.stream()
                .map(CompletionStage::toCompletableFuture)
                .toArray(java.util.concurrent.CompletableFuture[]::new));
    }

    @Override
    public CompletionStage<Void> enqueueDispatch(
        Supplier<CompletionStage<Void>> operation) {
        return dispatchQueue.enqueue(
            () -> runApplicationExecution(null, false,
                () -> host.runEntryDispatch(this, operation)));
    }

    @Override
    public CompletionStage<Void> enqueueActorDispatch(
        String actorId,
        Supplier<CompletionStage<Void>> operation) {
        return runApplicationExecution(actorId, false, operation);
    }

    @Override
    public <T> ZLinkWorkerCall<T> runCpuWorker(ZLinkWorkerTask<T> work) {
        Objects.requireNonNull(work, "work");
        return new DefaultZLinkWorkerCall<>(workerPool, work);
    }

    @Override
    public <T> ZLinkWorkerCall<T> runIoWorker(ZLinkIoWorkerTask<T> work) {
        Objects.requireNonNull(work, "work");
        return new DefaultZLinkIoWorkerCall<>(workerPool, work);
    }

    private CompletionStage<Void> runApplicationExecution(
        String actorId,
        boolean yieldAllowed,
        Supplier<CompletionStage<Void>> operation) {
        var execution = new systems.zlink.framework.runtime.internal.handlers
            .ZLinkSuspendInvocationContext.ApplicationExecution(
                spotId(),
                actorId,
                false,
                yieldAllowed,
                ignored -> false);
        try (var ignored = systems.zlink.framework.runtime.internal.handlers
                 .ZLinkSuspendInvocationContext.enterApplicationExecution(execution)) {
            return systems.zlink.framework.runtime.actors
                .ZLinkDeferredActorJoinHandlerScope.run(
                    candidate -> host.isActorAtSpot(candidate, spotId()),
                    operation);
        } catch (RuntimeException failure) {
            return java.util.concurrent.CompletableFuture.failedFuture(failure);
        }
    }

    void closeRegistration() {
        handlerCatalog.closeRegistration(handlerTypes -> handlerLoader.load(
            entrySpot.getClass(),
            handlerTypes,
            this::addTimer));
    }

    void bindSubscriptions(ZLinkBackendSpot spot) {
        for (String topic : handlerCatalog.subscriptionTopics()) {
            spot.setSubscription(topic);
        }
    }
}

final class DefaultSpotContext implements ZLinkSpotContext, SpotDispatchLine {
    private final ZLinkSpotContextHost host;
    private final ZLinkWorkerPool workerPool;
    private final ZLinkSpotHandlerLoader handlerLoader;
    private final RoutingId nodeRid;
    private final ZLinkBackendSpot backendSpot;
    private final DefaultSpotOutbound outbound;
    private final ZLinkSpotTimerRegistry timers;
    private final ZLinkHandlerInstanceOwner handlerInstances;
    private final ZLinkAsyncSerialQueue dispatchQueue;
    private final java.util.concurrent.ConcurrentHashMap<
        String, ZLinkAsyncSerialQueue> timerQueues =
            new java.util.concurrent.ConcurrentHashMap<>();
    private final ZLinkUserSpotExecutionMode executionMode;
    private final boolean instanceSpot;
    private ZLinkUserSpotRelocationBarrier relocationBarrier;
    private final ZLinkSpotHandlerCatalog handlerCatalog = new ZLinkSpotHandlerCatalog(
        "SPOT handler registration is only allowed while configure is running");
    private ZLinkSpot<?> spot;

    DefaultSpotContext(
        ZLinkSpotContextHost host,
        ZLinkWorkerPool workerPool,
        ZLinkSpotHandlerLoader handlerLoader,
        RoutingId nodeRid,
        ZLinkBackendSpot backendSpot) {
        this(
            host,
            workerPool,
            handlerLoader,
            nodeRid,
            backendSpot,
            new ZLinkAsyncSerialQueue(),
            ZLinkUserSpotExecutionMode.SPOT_WIDE,
            false);
    }

    DefaultSpotContext(
        ZLinkSpotContextHost host,
        ZLinkWorkerPool workerPool,
        ZLinkSpotHandlerLoader handlerLoader,
        RoutingId nodeRid,
        ZLinkBackendSpot backendSpot,
        ZLinkAsyncSerialQueue dispatchQueue) {
        this(
            host,
            workerPool,
            handlerLoader,
            nodeRid,
            backendSpot,
            dispatchQueue,
            ZLinkUserSpotExecutionMode.SPOT_WIDE,
            false);
    }

    DefaultSpotContext(
        ZLinkSpotContextHost host,
        ZLinkWorkerPool workerPool,
        ZLinkSpotHandlerLoader handlerLoader,
        RoutingId nodeRid,
        ZLinkBackendSpot backendSpot,
        ZLinkAsyncSerialQueue dispatchQueue,
        ZLinkUserSpotExecutionMode executionMode,
        boolean instanceSpot) {
        this(
            host,
            workerPool,
            handlerLoader,
            nodeRid,
            backendSpot,
            dispatchQueue,
            executionMode,
            instanceSpot,
            null);
    }

    DefaultSpotContext(
        ZLinkSpotContextHost host,
        ZLinkWorkerPool workerPool,
        ZLinkSpotHandlerLoader handlerLoader,
        RoutingId nodeRid,
        ZLinkBackendSpot backendSpot,
        ZLinkAsyncSerialQueue dispatchQueue,
        ZLinkUserSpotExecutionMode executionMode,
        boolean instanceSpot,
        ZLinkHandlerInstanceOwner sharedHandlerInstances) {
        this.host = host;
        this.workerPool = workerPool;
        this.handlerLoader = handlerLoader;
        this.nodeRid = nodeRid;
        this.backendSpot = backendSpot;
        this.dispatchQueue = dispatchQueue;
        this.executionMode = Objects.requireNonNull(executionMode, "executionMode");
        this.instanceSpot = instanceSpot;
        this.handlerInstances = sharedHandlerInstances == null
            ? host.createHandlerInstances()
            : sharedHandlerInstances;
        this.outbound = host.createContextOutbound(backendSpot, nodeRid);
        this.timers = host.createTimerRegistry(
            backendSpot.spotId(),
            handlerInstances,
            (timerName, operation) -> enqueueTimerDispatch(
                timerName,
                () -> host.runWithOutbound(outbound, operation)));
    }

    void setSpot(ZLinkSpot<?> spot) {
        this.spot = spot;
        timers.setSpot(spot);
    }

    @Override public String spotId() { return backendSpot.spotId(); }
    @Override public long objectGeneration() {
        return backendSpot.lifecycleGeneration();
    }
    @Override public RoutingId nodeRid() { return nodeRid; }
    @Override public ZLinkSpotOutbound outbound() { return outbound; }
    @Override public DefaultSpotOutbound dispatchOutbound() { return outbound; }
    @Override public ZLinkSpotHandlerRegistry handlers() { return handlerCatalog; }
    @Override public ZLinkSpotHandlerCatalog handlerCatalog() { return handlerCatalog; }

    @Override
    public CompletionStage<Void> leaveActor(ZLinkActor actor) {
        return host.leaveActor(nodeRid, spot, actor, spotId());
    }

    @Override
    public CompletionStage<Boolean> close() {
        return host.closeSpot(spotId());
    }

    @Override
    public CompletionStage<ZLinkTimer> addTimer(
        String name,
        Duration period,
        Class<?> handlerType,
        ZLinkTimerOptions options) {
        return timers.add(name, period, handlerType, options);
    }

    void closeTimers() {
        timers.close();
    }

    @Override
    public ZLinkHandlerInstanceOwner handlerInstances() {
        return handlerInstances;
    }

    void closeHandlerInstances() {
        handlerInstances.close();
    }

    void sealTimerAdmission() {
        timers.freeze();
    }

    byte[] freezeTimerRelocationEnvelope() {
        return ZLinkSpotTimerRelocationEnvelope.encode(timers.freeze());
    }

    void resumeTimersAfterRelocationAbort() {
        timers.resume();
    }

    void restoreTimerRelocationEnvelope(byte[] envelope) {
        ClassLoader loader = spot.getClass().getClassLoader();
        timers.restore(ZLinkSpotTimerRelocationEnvelope.decode(
            envelope,
            name -> loadTimerHandler(loader, name)));
    }

    void stageTimerRelocationEnvelope(byte[] envelope) {
        ClassLoader loader = spot.getClass().getClassLoader();
        timers.stageRestore(ZLinkSpotTimerRelocationEnvelope.decode(
            envelope,
            name -> loadTimerHandler(loader, name)));
    }

    void publishStagedTimerRelocation() {
        timers.publishStagedRestore();
    }

    @Override
    public CompletionStage<Void> enqueueDispatch(
        Supplier<CompletionStage<Void>> operation) {
        return dispatchQueue.enqueue(
            () -> runApplicationExecution(
                null,
                sharedSpotGate(),
                operation));
    }

    @Override
    public CompletionStage<Void> enqueueActorDispatch(
        String actorId,
        Supplier<CompletionStage<Void>> operation) {
        Objects.requireNonNull(actorId, "actorId");
        if (sharedSpotGate()) {
            return dispatchQueue.enqueue(
                () -> runApplicationExecution(actorId, true, operation));
        }
        return runApplicationExecution(actorId, false, operation);
    }

    CompletionStage<Void> enqueueAcceptedDispatch(
        byte[] acceptedJournalRecord,
        Supplier<CompletionStage<Void>> operation,
        Runnable relocationRelease) {
        return dispatchQueue.enqueueRelocatable(
            acceptedJournalRecord,
            () -> runApplicationExecution(
                null,
                sharedSpotGate(),
                operation),
            relocationRelease);
    }

    CompletionStage<Void> enqueueLifecycle(
        Supplier<CompletionStage<Void>> operation) {
        CompletionStage<Void> barrier;
        if (sharedSpotGate() || timerQueues.isEmpty()) {
            barrier = java.util.concurrent.CompletableFuture.completedFuture(null);
        } else {
            barrier = java.util.concurrent.CompletableFuture.allOf(
                timerQueues.values().stream()
                    .map(queue -> queue.enqueue(() ->
                        java.util.concurrent.CompletableFuture.completedFuture(null))
                        .toCompletableFuture())
                    .toArray(java.util.concurrent.CompletableFuture[]::new));
        }
        return barrier.thenCompose(ignored -> dispatchQueue.enqueue(() ->
            runApplicationExecution(null, false, operation)));
    }

    CompletionStage<Void> awaitAllLanes() {
        List<CompletionStage<Void>> lanes = new ArrayList<>();
        lanes.add(dispatchQueue.awaitQuiescence());
        timerQueues.values().forEach(
            queue -> lanes.add(queue.awaitQuiescence()));
        return java.util.concurrent.CompletableFuture.allOf(
            lanes.stream()
                .map(CompletionStage::toCompletableFuture)
                .toArray(java.util.concurrent.CompletableFuture[]::new));
    }

    java.util.Map<String, ZLinkAsyncSerialQueue> relocationLanes() {
        java.util.LinkedHashMap<String, ZLinkAsyncSerialQueue> lanes =
            new java.util.LinkedHashMap<>();
        lanes.put("spot", dispatchQueue);
        timerQueues.entrySet().stream()
            .sorted(java.util.Map.Entry.comparingByKey())
            .forEach(entry -> lanes.put(
                "timer:" + entry.getKey(), entry.getValue()));
        return java.util.Collections.unmodifiableMap(lanes);
    }

    synchronized ZLinkUserSpotRelocationBarrier relocationBarrier(
        ZLinkActorSessionCoordinator actors) {
        if (relocationBarrier == null) {
            relocationBarrier =
                new ZLinkUserSpotRelocationBarrier(this, actors);
        }
        return relocationBarrier;
    }

    <T> CompletionStage<T> runLifecycleExecution(
        Supplier<CompletionStage<T>> operation) {
        var execution = new systems.zlink.framework.runtime.internal.handlers
            .ZLinkSuspendInvocationContext.ApplicationExecution(
                spotId(),
                null,
                sharedSpotGate(),
                false,
                candidate -> host.isActorMember(spotId(), candidate));
        try (var ignored = systems.zlink.framework.runtime.internal.handlers
                 .ZLinkSuspendInvocationContext.enterApplicationExecution(execution)) {
            return Objects.requireNonNull(operation.get(), "operation result");
        } catch (RuntimeException failure) {
            return java.util.concurrent.CompletableFuture.failedFuture(failure);
        }
    }

    Optional<ZLinkAsyncSerialQueue.RelocationSeal> trySealRelocation() {
        return dispatchQueue.trySealRelocation();
    }

    boolean abortRelocation(ZLinkAsyncSerialQueue.RelocationSeal seal) {
        return dispatchQueue.abortRelocation(seal);
    }

    Optional<List<ZLinkAsyncSerialQueue.QueuedRecord>> commitRelocation(
        ZLinkAsyncSerialQueue.RelocationSeal seal) {
        return dispatchQueue.commitRelocation(seal);
    }

    @Override
    public <T> ZLinkWorkerCall<T> runCpuWorker(ZLinkWorkerTask<T> work) {
        Objects.requireNonNull(work, "work");
        return new DefaultZLinkWorkerCall<>(workerPool, work);
    }

    @Override
    public <T> ZLinkWorkerCall<T> runIoWorker(ZLinkIoWorkerTask<T> work) {
        Objects.requireNonNull(work, "work");
        return new DefaultZLinkIoWorkerCall<>(workerPool, work);
    }

    ZLinkUserSpotExecutionMode executionMode() {
        return executionMode;
    }

    private CompletionStage<Void> enqueueTimerDispatch(
        String timerName,
        Supplier<CompletionStage<Void>> operation) {
        if (sharedSpotGate()) {
            return enqueueDispatch(operation);
        }
        ZLinkAsyncSerialQueue queue = timerQueues.computeIfAbsent(
            Objects.requireNonNull(timerName, "timerName"),
            ignored -> new ZLinkAsyncSerialQueue());
        return queue.enqueue(() -> runApplicationExecution(
            null,
            false,
            operation));
    }

    private boolean sharedSpotGate() {
        return instanceSpot
            || executionMode == ZLinkUserSpotExecutionMode.SPOT_WIDE;
    }

    private CompletionStage<Void> runApplicationExecution(
        String actorId,
        boolean yieldAllowed,
        Supplier<CompletionStage<Void>> operation) {
        var execution = new systems.zlink.framework.runtime.internal.handlers
            .ZLinkSuspendInvocationContext.ApplicationExecution(
                spotId(),
                actorId,
                sharedSpotGate(),
                yieldAllowed,
                candidate -> host.isActorMember(spotId(), candidate));
        try (var ignored = systems.zlink.framework.runtime.internal.handlers
                 .ZLinkSuspendInvocationContext.enterApplicationExecution(execution)) {
            if (instanceSpot) {
                return Objects.requireNonNull(operation.get(), "operation result");
            }
            return systems.zlink.framework.runtime.actors
                .ZLinkDeferredActorJoinHandlerScope.run(
                    candidate -> host.isActorMember(spotId(), candidate),
                    operation);
        } catch (RuntimeException failure) {
            return java.util.concurrent.CompletableFuture.failedFuture(failure);
        }
    }

    void closeRegistration() {
        handlerCatalog.closeRegistration(handlerTypes -> handlerLoader.load(
            spot.getClass(),
            handlerTypes,
            this::addTimer));
    }

    void bindSubscriptions(ZLinkBackendSpot spot) {
        for (String topic : handlerCatalog.subscriptionTopics()) {
            spot.setSubscription(topic);
        }
    }

    private static Class<?> loadTimerHandler(
        ClassLoader loader,
        String name) {
        try {
            return Class.forName(name, false, loader);
        } catch (ClassNotFoundException error) {
            throw new systems.zlink.framework.errors.ZLinkConfigurationException(
                "timer handler is not available on the relocation target: "
                    + name,
                error);
        }
    }
}
