package systems.zlink.framework.runtime.spots;

import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.concurrent.CompletionStage;
import java.util.function.Supplier;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.execution.ZLinkAsyncSerialQueue;
import systems.zlink.framework.execution.ZLinkWorkerPool;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpot;
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
    }

    @Override public RoutingId spotRid() { return backendSpot.routingId(); }
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
            dispatchQueue);
        timerContext.setSpot(new ZLinkEntrySpotTimerSurface(this));
        timerContexts.add(timerContext);
        return timerContext.addTimer(name, period, handlerType, options);
    }

    void closeTimers() {
        timerContexts.forEach(DefaultSpotContext::closeTimers);
        timerContexts.clear();
    }

    @Override
    public CompletionStage<Void> enqueueDispatch(
        Supplier<CompletionStage<Void>> operation) {
        return ZLinkSpotQueueMetrics.enqueue(dispatchQueue, "entry",
            () -> host.runEntryDispatch(this, operation));
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
    private final ZLinkAsyncSerialQueue dispatchQueue;
    private final ZLinkSpotHandlerCatalog handlerCatalog = new ZLinkSpotHandlerCatalog(
        "SPOT handler registration is only allowed while configure is running");
    private ZLinkSpot<?> spot;

    DefaultSpotContext(
        ZLinkSpotContextHost host,
        ZLinkWorkerPool workerPool,
        ZLinkSpotHandlerLoader handlerLoader,
        RoutingId nodeRid,
        ZLinkBackendSpot backendSpot) {
        this(host, workerPool, handlerLoader, nodeRid, backendSpot, new ZLinkAsyncSerialQueue());
    }

    DefaultSpotContext(
        ZLinkSpotContextHost host,
        ZLinkWorkerPool workerPool,
        ZLinkSpotHandlerLoader handlerLoader,
        RoutingId nodeRid,
        ZLinkBackendSpot backendSpot,
        ZLinkAsyncSerialQueue dispatchQueue) {
        this.host = host;
        this.workerPool = workerPool;
        this.handlerLoader = handlerLoader;
        this.nodeRid = nodeRid;
        this.backendSpot = backendSpot;
        this.dispatchQueue = dispatchQueue;
        this.outbound = host.createContextOutbound(backendSpot, nodeRid);
        this.timers = host.createTimerRegistry(
            backendSpot.routingId(),
            operation -> enqueueDispatch(() -> host.runWithOutbound(outbound, operation)));
    }

    void setSpot(ZLinkSpot<?> spot) {
        this.spot = spot;
        timers.setSpot(spot);
    }

    @Override public RoutingId spotRid() { return backendSpot.routingId(); }
    @Override public RoutingId nodeRid() { return nodeRid; }
    @Override public ZLinkSpotOutbound outbound() { return outbound; }
    @Override public DefaultSpotOutbound dispatchOutbound() { return outbound; }
    @Override public ZLinkSpotHandlerRegistry handlers() { return handlerCatalog; }
    @Override public ZLinkSpotHandlerCatalog handlerCatalog() { return handlerCatalog; }

    @Override
    public CompletionStage<Void> leaveActor(ZLinkActor actor) {
        return host.leaveActor(nodeRid, spot, actor, spotRid());
    }

    @Override
    public CompletionStage<Boolean> close() {
        return host.closeSpot(spotRid());
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

    @Override
    public CompletionStage<Void> enqueueDispatch(
        Supplier<CompletionStage<Void>> operation) {
        return ZLinkSpotQueueMetrics.enqueue(dispatchQueue, "user", operation);
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
