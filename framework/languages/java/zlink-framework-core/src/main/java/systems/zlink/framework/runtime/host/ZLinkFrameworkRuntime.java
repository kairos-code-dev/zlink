package systems.zlink.framework.runtime.host;

import systems.zlink.framework.runtime.internal.backend.ZLinkBackendAdapterProvider;

import systems.zlink.framework.runtime.internal.backend.ZLinkInternalSpotNode;

import systems.zlink.framework.runtime.backend.*;

import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.actors.ZLinkActorClient;
import systems.zlink.framework.actors.ZLinkActorDirectory;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.channels.ZLinkChannelRuntimeOptions;
import systems.zlink.framework.channels.ZLinkFanoutClient;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.monitoring.ZLinkRuntimeEventDispatcher;
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.configuration.ZLinkFrameworkRegistration;
import systems.zlink.framework.runtime.actors.ZLinkActorRuntime;
import systems.zlink.framework.runtime.actors.ZLinkSessionActorsRuntime;
import systems.zlink.framework.runtime.channels.ZLinkChannelRuntime;
import systems.zlink.framework.runtime.internal.handlers.ZLinkHandlerActivator;
import systems.zlink.framework.runtime.locations.ZLinkLocationLifecycle;
import systems.zlink.framework.runtime.locations.ZLinkLocationAutoConnectHost;
import systems.zlink.framework.runtime.locations.ZLinkLocationRuntime;
import systems.zlink.framework.runtime.locations.ZLinkLocationReadinessService;
import systems.zlink.framework.runtime.locations.ZLinkRegisteredLocationStores;
import systems.zlink.framework.runtime.locations.ZLinkStoreLocationResolvers;
import systems.zlink.framework.runtime.messaging.ZLinkJsonMessageSerializer;
import systems.zlink.framework.runtime.spots.ZLinkSpotRuntime;
import systems.zlink.framework.runtime.streams.ZLinkStreamRuntime;
import systems.zlink.framework.spots.ZLinkSpotOutbound;
import systems.zlink.framework.spots.ZLinkSpotPublisherClient;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.framework.locations.ZLinkLocationRuntimeQuery;
import systems.zlink.framework.streams.ZLinkStreamCodec;
import systems.zlink.contracts.core.RoutingId;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.TimeUnit;

public final class ZLinkFrameworkRuntime
    implements AutoCloseable,
        systems.zlink.framework.configuration.ZLinkMessageFlowControl,
        systems.zlink.framework.monitoring.ZLinkDrainControl {
    private final ZLinkChannelRuntime channels;
    private final ZLinkSpotRuntime spots;
    private final ZLinkActorRuntime actors;
    private final ZLinkActorDirectory actorDirectory;
    private final ZLinkActorClient actorClient;
    private final ZLinkStreamRuntime streams;
    private final ZLinkBackendContext backendContext;
    private final ZLinkFrameworkRegistration registration;
    private final ZLinkRegisteredLocationStores locationStores;
    private final ZLinkLocationRuntime locationRuntime;
    private final ZLinkLocationRuntimeQuery locationRuntimeQuery;
    private final ZLinkLocationLifecycle locationLifecycle;
    private final ZLinkLocationAutoConnectHost locationAutoConnectHost;
    private final systems.zlink.framework.runtime.internal.spots.SpotTransportAddressResolver
        spotTransportAddressResolver;
    private final ZLinkStoreLocationResolvers storeLocationResolvers;
    private final java.util.concurrent.atomic.AtomicBoolean spotRuntimeStopped =
        new java.util.concurrent.atomic.AtomicBoolean(false);
    private final java.util.concurrent.atomic.AtomicBoolean ready =
        new java.util.concurrent.atomic.AtomicBoolean(false);
    private final java.util.concurrent.atomic.AtomicBoolean drainStarted =
        new java.util.concurrent.atomic.AtomicBoolean(false);
    private final java.util.concurrent.atomic.AtomicBoolean drainTerminalStarted =
        new java.util.concurrent.atomic.AtomicBoolean(false);
    private final ZLinkCloseGate closeGate = new ZLinkCloseGate();
    private final java.util.concurrent.CompletableFuture<
        systems.zlink.framework.monitoring.ZLinkDrainResult> drained =
        new java.util.concurrent.CompletableFuture<>();
    // Shared, runtime-mutable message-flow mode cell, installed into the diagnostics
    // options so every surface observes setMessageFlowMode live.
    private final java.util.concurrent.atomic.AtomicReference<
        systems.zlink.framework.configuration.ZLinkMessageFlowLogMode> messageFlowMode;
    private final ZLinkRuntimeEventDispatcher eventDispatcher;

    ZLinkFrameworkRuntime(
        DefaultZLinkFrameworkOptions options,
        ZLinkBackendAdapterProvider backendFactory,
        ZLinkMessageSerializer serializer) {
        this(options, backendFactory, serializer, ZLinkHandlerActivator.reflection());
    }

    ZLinkFrameworkRuntime(
        DefaultZLinkFrameworkOptions options,
        ZLinkBackendAdapterProvider backendFactory,
        ZLinkMessageSerializer serializer,
        ZLinkHandlerActivator handlerFactory) {
        this(options, backendFactory, serializer, handlerFactory, null);
    }

    ZLinkFrameworkRuntime(
        DefaultZLinkFrameworkOptions options,
        ZLinkBackendAdapterProvider backendFactory,
        ZLinkMessageSerializer serializer,
        ZLinkHandlerActivator handlerFactory,
        ZLinkRuntimeEventDispatcher eventDispatcher) {
        options.validate();
        this.eventDispatcher = eventDispatcher;
        this.registration = options.registration();
        var diagnostics = this.registration.dispatchOptions().diagnostics();
        this.messageFlowMode =
            new java.util.concurrent.atomic.AtomicReference<>(diagnostics.messageFlow());
        diagnostics.installLiveMode(this.messageFlowMode);
        ZLinkBackendAdapterOptions adapterOptions =
            new ZLinkBackendAdapterOptions(options.defaultRequestTimeout());
        ZLinkStreamCodec defaultStreamCodec = defaultStreamCodec(options);
        ZLinkHandlerActivator.MutableServices runtimeHandlers =
            ZLinkHandlerActivator.services(handlerFactory);
        runtimeHandlers.add(ZLinkFrameworkRegistration.class, this.registration);
        runtimeHandlers.add(ZLinkFrameworkRuntime.class, this);
        ZLinkFrameworkLocationSubsystem locationSubsystem =
            ZLinkFrameworkLocationSubsystem.create(this.registration, runtimeHandlers);
        this.locationStores = locationSubsystem.locationStores();
        if (locationSubsystem.enabled()) {
            this.locationRuntime = locationSubsystem.locationRuntime();
            this.locationRuntimeQuery = locationSubsystem.locationRuntimeQuery();
            this.locationLifecycle = locationSubsystem.locationLifecycle();
            this.locationAutoConnectHost = locationSubsystem.locationAutoConnectHost();
            this.spotTransportAddressResolver = locationSubsystem.spotTransportAddressResolver();
            this.storeLocationResolvers = locationSubsystem.storeLocationResolvers();
        } else {
            this.locationRuntime = null;
            this.locationRuntimeQuery = null;
            this.locationLifecycle = null;
            this.locationAutoConnectHost = null;
            this.spotTransportAddressResolver = null;
            this.storeLocationResolvers = null;
        }
        ZLinkFrameworkChannelSubsystem channelSubsystem = ZLinkFrameworkChannelSubsystem.create(
            options,
            backendFactory,
            adapterOptions,
            serializer,
            runtimeHandlers,
            eventDispatcher);
        this.backendContext = channelSubsystem.backendContext();
        this.channels = channelSubsystem.channels();

        ZLinkFrameworkSpotSubsystem spotSubsystem = ZLinkFrameworkSpotSubsystem.create(
            options,
            backendFactory,
            adapterOptions,
            serializer,
            runtimeHandlers,
            eventDispatcher,
            this.channels,
            this.backendContext,
            this.locationLifecycle,
            this.spotTransportAddressResolver);
        this.spots = spotSubsystem.spots();

        ZLinkFrameworkActorSubsystem actorSubsystem = ZLinkFrameworkActorSubsystem.create(
            this.registration,
            serializer,
            runtimeHandlers,
            handlerFactory,
            eventDispatcher,
            defaultStreamCodec,
            this.channels,
            this.spots,
            this.locationLifecycle,
            this.storeLocationResolvers,
            spotSubsystem.remoteAddressResolver());
        this.actors = actorSubsystem.actors();
        this.actorDirectory = actorSubsystem.actorDirectory();
        this.actorClient = actorSubsystem.actorClient();

        ZLinkFrameworkStreamSubsystem streamSubsystem = ZLinkFrameworkStreamSubsystem.create(
            options,
            backendFactory,
            adapterOptions,
            serializer,
            runtimeHandlers,
            eventDispatcher,
            this.spots,
            this.actors,
            this.actorDirectory);
        this.streams = streamSubsystem.streams();

        locationSubsystem.startup()
            .thenCompose(ignored -> spotSubsystem.startup())
            .thenCompose(ignored -> ZLinkFrameworkAutoConnectSubsystem.start(
                this.locationAutoConnectHost,
                this.registration,
                this.channels,
                this.spots))
            .whenComplete((ignored, failure) -> {
                if (failure == null && !drainStarted.get()) {
                    ready.set(true);
                    publishDrainState(systems.zlink.framework.monitoring.ZLinkDrainState.SERVING);
                }
            });
    }

    static ZLinkFrameworkRuntime start(
        DefaultZLinkFrameworkOptions options,
        ZLinkBackendAdapterProvider backendFactory) {
        return new ZLinkFrameworkRuntime(options, backendFactory, serializerFor(options));
    }

    static ZLinkFrameworkRuntime start(
        DefaultZLinkFrameworkOptions options,
        ZLinkBackendAdapterProvider backendFactory,
        ZLinkHandlerActivator handlerFactory) {
        return start(options, backendFactory, handlerFactory, null);
    }

    static ZLinkFrameworkRuntime start(
        DefaultZLinkFrameworkOptions options,
        ZLinkBackendAdapterProvider backendFactory,
        ZLinkHandlerActivator handlerFactory,
        ZLinkRuntimeEventDispatcher eventDispatcher) {
        return new ZLinkFrameworkRuntime(
            options,
            backendFactory,
            serializerFor(options),
            handlerFactory,
            eventDispatcher);
    }

    static ZLinkMessageSerializer serializerFor(DefaultZLinkFrameworkOptions options) {
        java.util.Optional<ZLinkMessageSerializer> custom =
            options.registration().codecs().customSerializer();
        if (custom.isPresent()) {
            return custom.get();
        }
        return options.registration().codecs().serializerWithFallback(new ZLinkJsonMessageSerializer());
    }

    private static ZLinkStreamCodec defaultStreamCodec(DefaultZLinkFrameworkOptions options) {
        return options.registration().codecs().streamCodecForCustomSerializer()
            .orElse(ZLinkStreamCodec.JSON);
    }

    public ZLinkClient client() {
        return channels;
    }

    public ZLinkChannelRuntimeOptions channelRuntimeOptions() {
        return channels;
    }

    // Runtime toggle (ZLinkMessageFlowControl): flip the shared live-mode cell so every
    // surface starts/stops tracing without a restart. Thread-safe.
    @Override
    public void setMessageFlowMode(systems.zlink.framework.configuration.ZLinkMessageFlowLogMode mode) {
        if (mode == null) {
            throw new IllegalArgumentException("mode is required");
        }
        messageFlowMode.set(mode);
    }

    @Override
    public systems.zlink.framework.configuration.ZLinkMessageFlowLogMode messageFlowMode() {
        return registration.dispatchOptions().diagnostics().effectiveMessageFlow();
    }

    public ZLinkFanoutClient fanout() {
        return channels;
    }

    public ZLinkRouteClient route() {
        return channels;
    }

    public ZLinkSpotManager spotManager() {
        if (spots == null) {
            throw new ZLinkConfigurationException("Spot runtime is not configured");
        }
        return spots;
    }

    public ZLinkSpotOutbound spotOutbound() {
        if (spots == null) {
            throw new ZLinkConfigurationException("Spot runtime is not configured");
        }
        return spots.outbound();
    }

    public ZLinkSpotPublisherClient spotPublisherClient() {
        if (spots == null) {
            throw new ZLinkConfigurationException("Spot runtime is not configured");
        }
        return spots.publisherClient();
    }

    public java.util.Map<String, ZLinkBackendSocket> monitoringSocketSources() {
        return channels.monitoringSocketSources();
    }

    public java.util.Map<String, ZLinkInternalSpotNode> monitoringSpotSources() {
        return spots == null ? java.util.Map.of() : spots.nodesByName();
    }

    public ZLinkLocationRuntimeQuery monitoringLocationRuntimeQuery() {
        if (locationRuntimeQuery == null) {
            throw new ZLinkConfigurationException("Location runtime is not configured");
        }
        return locationRuntimeQuery;
    }

    public systems.zlink.framework.locations.ZLinkLocationReadiness locationReadiness() {
        if (locationRuntimeQuery == null) {
            throw new ZLinkConfigurationException("Location runtime is not configured");
        }
        return new ZLinkLocationReadinessService(locationRuntimeQuery);
    }

    public boolean stopSpotRuntime() {
        if (spots == null || !spotRuntimeStopped.compareAndSet(false, true)) {
            return false;
        }
        spots.beginClose();
        spots.closeAsync();
        return true;
    }

    public ZLinkActorManager actorManager() {
        if (actors == null) {
            throw new ZLinkConfigurationException("Actor runtime is not configured");
        }
        return actors;
    }

    public systems.zlink.framework.actors.ZLinkActorDirectory actorDirectory() {
        if (actorDirectory == null) {
            throw new ZLinkConfigurationException("Actor directory requires a SPOT node and location store");
        }
        return actorDirectory;
    }

    public ZLinkActorClient actorClient() {
        if (actorClient == null) {
            throw new ZLinkConfigurationException("Actor client requires a SPOT node and location store");
        }
        return actorClient;
    }

    public systems.zlink.framework.spots.SpotHandleResolver spotHandleResolver() {
        if (spotTransportAddressResolver
            instanceof systems.zlink.framework.spots.SpotHandleResolver resolver) {
            return resolver;
        }
        throw new systems.zlink.framework.errors.ZLinkConfigurationException(
            "SpotHandleResolver requires a configured location store");
    }

    public systems.zlink.framework.spots.ActorSpotHandleResolver actorSpotHandleResolver() {
        if (spotTransportAddressResolver
            instanceof systems.zlink.framework.spots.ActorSpotHandleResolver resolver) {
            return resolver;
        }
        throw new systems.zlink.framework.errors.ZLinkConfigurationException(
            "ActorSpotHandleResolver requires a configured location store");
    }

    public ZLinkSessionActorsRuntime sessionActors(String streamNodeName, RoutingId sessionRid) {
        if (streams == null) {
            throw new ZLinkConfigurationException("Stream runtime is not configured");
        }
        return streams.sessionActors(streamNodeName, sessionRid, actors);
    }

    @Override
    public void close() {
        try {
            closeAsync().toCompletableFuture().get();
        } catch (java.lang.InterruptedException error) {
            Thread.currentThread().interrupt();
            throw new ZLinkConfigurationException("framework shutdown was interrupted", error);
        } catch (java.util.concurrent.ExecutionException error) {
            Throwable cause = error.getCause();
            if (cause instanceof RuntimeException runtimeError) {
                throw runtimeError;
            }
            throw new ZLinkConfigurationException("framework shutdown failed", cause);
        }
    }

    java.util.concurrent.CompletionStage<Void> closeAsync() {
        return closeGate.close(this::closeCoreAsync);
    }

    private java.util.concurrent.CompletionStage<Void> closeCoreAsync() {
        ready.set(false);
        if (spots != null && !spotRuntimeStopped.get()) {
            spots.beginClose();
        }
        channels.beginClose();
        ZLinkFrameworkShutdown shutdown = new ZLinkFrameworkShutdown();
        shutdown.defer(this::closeHandlerExecutor);
        shutdown.defer(backendContext::close);
        if (locationRuntime != null) {
            shutdown.defer(locationRuntime::close);
            shutdown.defer(locationLifecycle::close);
            shutdown.deferStage(locationRuntime::stop);
        }
        if (spots != null) {
            shutdown.deferStage(() -> {
                if (spotRuntimeStopped.compareAndSet(false, true)) {
                    return spots.closeAsync();
                }
                return java.util.concurrent.CompletableFuture.completedFuture(null);
            });
        }
        shutdown.defer(channels::close);
        if (locationAutoConnectHost != null) {
            shutdown.deferStage(locationAutoConnectHost::stop);
        }
        if (actors != null) {
            shutdown.deferStage(actors::closeAsync);
        }
        if (streams != null) {
            shutdown.deferStage(streams::closeAsync);
        }
        return shutdown.closeAsync().whenComplete((ignored, failure) -> {
            if (!drainStarted.get() && failure == null) {
                publishDrainState(systems.zlink.framework.monitoring.ZLinkDrainState.DRAINED);
                drained.complete(new systems.zlink.framework.monitoring.Drained());
            }
        });
    }

    @Override
    public java.util.concurrent.CompletionStage<systems.zlink.framework.monitoring.ZLinkDrainResult> drain() {
        return drain(java.time.Duration.ofSeconds(30));
    }

    @Override
    public java.util.concurrent.CompletionStage<systems.zlink.framework.monitoring.ZLinkDrainResult> drain(
        java.time.Duration deadline) {
        java.util.Objects.requireNonNull(deadline, "deadline");
        if (deadline.isZero() || deadline.isNegative()) {
            throw new IllegalArgumentException("deadline must be positive");
        }
        if (drainStarted.compareAndSet(false, true)) {
            ready.set(false);
            publishDrainState(systems.zlink.framework.monitoring.ZLinkDrainState.DRAINING);
            if (streams != null) {
                streams.beginDrain();
            }
            if (spots != null) {
                spots.beginDrain().exceptionally(error -> null);
            }
            if (actors != null) {
                actors.beginDrain();
            }
            runDrain();
            java.util.concurrent.CompletableFuture.delayedExecutor(
                deadline.toMillis(), java.util.concurrent.TimeUnit.MILLISECONDS)
                .execute(() -> forceStop(
                    systems.zlink.framework.monitoring.ZLinkDrainForceReason.DEADLINE_EXCEEDED));
        }
        return independentWaiter(drained);
    }

    @Override
    public java.util.concurrent.CompletionStage<systems.zlink.framework.monitoring.ZLinkDrainResult> awaitDrained() {
        return independentWaiter(drained);
    }

    static <T> java.util.concurrent.CompletionStage<T> independentWaiter(
        java.util.concurrent.CompletionStage<T> shared) {
        return shared.thenApply(result -> result);
    }

    @Override
    public boolean isReady() {
        return ready.get();
    }

    private void runDrain() {
        java.util.concurrent.CompletionStage<Void> markerPublished = locationAutoConnectHost == null
            ? java.util.concurrent.CompletableFuture.completedFuture(null)
            : locationAutoConnectHost.markDraining();
        markerPublished.whenComplete((ignored, publishFailure) -> {
            if (publishFailure != null) {
                forceStop(systems.zlink.framework.monitoring.ZLinkDrainForceReason.DRAINING_STATE_PUBLISH_FAILED);
                return;
            }
            beginActorHandoff().whenComplete((handoffCount, handoffFailure) -> {
                if (handoffFailure != null) {
                    forceStop(systems.zlink.framework.monitoring.ZLinkDrainForceReason.TEARDOWN_FAILED);
                    return;
                }
                for (int i = 0; i < handoffCount; i++) {
                    systems.zlink.framework.runtime.internal.metrics.ZLinkRuntimeMetrics.increment(
                        "zlink.drain.actors.handed_off", java.util.Map.of());
                }
                java.util.concurrent.CompletionStage<Void> spotDrain = spots == null
                    ? java.util.concurrent.CompletableFuture.completedFuture(null)
                    : spots.continueDrain();
                spotDrain.exceptionally(error -> null).thenCompose(spotIgnored -> awaitWorkloadsDrained())
                    .whenComplete((workloadsIgnored, workloadFailure) -> {
                if (workloadFailure != null) {
                    forceStop(systems.zlink.framework.monitoring.ZLinkDrainForceReason.TEARDOWN_FAILED);
                    return;
                }
                completeDrain();
                    });
            });
        });
    }

    private java.util.concurrent.CompletionStage<Integer> beginActorHandoff() {
        if (actors == null || storeLocationResolvers == null || spots == null) {
            return java.util.concurrent.CompletableFuture.completedFuture(0);
        }
        java.util.Set<RoutingId> localNodes = new java.util.HashSet<>();
        for (ZLinkInternalSpotNode node : spots.nodesByName().values()) {
            localNodes.add(node.routingId());
        }
        java.util.concurrent.CompletionStage<Integer> transferred =
            java.util.concurrent.CompletableFuture.completedFuture(0);
        for (String actorType : actors.activeActorTypes().stream().sorted().toList()) {
            String meshName = actorDrainMeshName(registration, actorType);
            if (meshName == null) {
                continue;
            }
            String transferRouteChannel = transferRouteChannelName(registration, meshName);
            if (transferRouteChannel == null) {
                continue;
            }
            transferred = transferred.thenCompose(count -> storeLocationResolvers.listLivePeers(
                    new systems.zlink.framework.locations.ZLinkPeerLocationFilter(
                        systems.zlink.framework.locations.ZLinkLocationAutoConnectType.SPOT_MESH,
                        meshName,
                        systems.zlink.framework.locations.ZLinkLocationRole.SPOT,
                        null,
                        null))
                .thenCompose(found -> found.stream()
                    .filter(peer -> !peer.draining())
                    .filter(peer -> isEligibleActorHandoffTarget(peer, actorType, localNodes))
                    .sorted(java.util.Comparator.comparing(peer -> peer.nodeRid().toString()))
                    .findFirst()
                    .<java.util.concurrent.CompletionStage<Integer>>map(peer ->
                        actors.handoffActorsToEntrySpot(
                            actorType, transferRouteChannel, peer.nodeRid()))
                    .orElseGet(() -> java.util.concurrent.CompletableFuture.completedFuture(0)))
                .thenApply(moved -> count + moved));
        }
        return transferred;
    }

    static String actorDrainMeshName(
        systems.zlink.framework.runtime.configuration.ZLinkFrameworkRegistration registration,
        String actorType) {
        return registration.spotNodes().stream()
            .filter(node -> node.actorFactories().containsKey(actorType))
            .map(systems.zlink.framework.runtime.spots.SpotNodeRegistration::meshName)
            .findFirst()
            .orElse(null);
    }

    static String transferRouteChannelName(
        systems.zlink.framework.runtime.configuration.ZLinkFrameworkRegistration registration,
        String spotMeshName) {
        String mapped = registration.locations().options().spotRouterChannels()
            .getOrDefault(spotMeshName, spotMeshName);
        return registration.channels().stream()
            .filter(channel -> channel.kind()
                == systems.zlink.framework.runtime.channels.ChannelKind.ROUTE_MESH)
            .filter(channel -> channel.name().equals(mapped))
            .map(systems.zlink.framework.runtime.channels.ChannelRegistration::name)
            .findFirst()
            .orElse(null);
    }

    static boolean isEligibleActorHandoffTarget(
        systems.zlink.framework.locations.ZLinkPeerLocation peer,
        String actorType,
        java.util.Set<RoutingId> localNodes) {
        return peer != null
            && actorType != null
            && peer.capabilities() != null
            && peer.capabilities().contains("actor:" + actorType)
            && !localNodes.contains(peer.nodeRid());
    }

    private java.util.concurrent.CompletionStage<Void> awaitWorkloadsDrained() {
        if (drained.isDone() || workloadsDrained()) {
            return java.util.concurrent.CompletableFuture.completedFuture(null);
        }
        java.util.concurrent.CompletableFuture<Void> result = new java.util.concurrent.CompletableFuture<>();
        java.util.concurrent.CompletableFuture.delayedExecutor(10L, java.util.concurrent.TimeUnit.MILLISECONDS)
            .execute(() -> {
                java.util.concurrent.CompletionStage<Void> retrySpotDrain =
                    (actors == null || actors.drainComplete())
                        && spots != null && !spots.drainComplete()
                    ? spots.continueDrain().exceptionally(error -> null)
                    : java.util.concurrent.CompletableFuture.completedFuture(null);
                retrySpotDrain.thenCompose(ignored -> awaitWorkloadsDrained())
                    .whenComplete((ignored, failure) -> {
                if (failure != null) {
                    result.completeExceptionally(failure);
                } else {
                    result.complete(null);
                }
                    });
            });
        return result;
    }

    private boolean workloadsDrained() {
        return (spots == null || spots.drainComplete())
            && (actors == null || actors.drainComplete());
    }

    private void completeDrain() {
        if (!drainTerminalStarted.compareAndSet(false, true)) {
            return;
        }
        ZLinkTeardownExecutor.execute(this::completeDrainOnTeardownThread);
    }

    private void completeDrainOnTeardownThread() {
        if (drained.isDone()) {
            return;
        }
        java.util.concurrent.CompletionStage<Void> notification = streams == null
            ? java.util.concurrent.CompletableFuture.completedFuture(null)
            : streams.notifyServerDrain();
        notification.thenCompose(ignored -> closeAsync())
            .whenComplete((ignored, failure) -> {
                if (failure == null) {
                    publishDrainState(systems.zlink.framework.monitoring.ZLinkDrainState.DRAINED);
                    drained.complete(new systems.zlink.framework.monitoring.Drained());
                } else {
                    completeForcedStop(
                        systems.zlink.framework.monitoring.ZLinkDrainForceReason.OWNER_CLEANUP_FAILED);
                }
            });
    }

    private void forceStop(systems.zlink.framework.monitoring.ZLinkDrainForceReason reason) {
        if (!drainTerminalStarted.compareAndSet(false, true)) {
            return;
        }
        ZLinkTeardownExecutor.execute(() -> forceStopOnTeardownThread(reason));
    }

    private void forceStopOnTeardownThread(
        systems.zlink.framework.monitoring.ZLinkDrainForceReason initialReason) {
        if (drained.isDone()) {
            return;
        }
        systems.zlink.framework.monitoring.ZLinkDrainForceReason reason = initialReason;
        publishDrainState(systems.zlink.framework.monitoring.ZLinkDrainState.FORCE_STOPPING);
        systems.zlink.framework.runtime.internal.metrics.ZLinkRuntimeMetrics.increment(
            "zlink.drain.forced", java.util.Map.of("kind", streams == null ? "runtime" : "session"));
        java.util.concurrent.CompletionStage<Void> notification = streams == null
            ? java.util.concurrent.CompletableFuture.completedFuture(null)
            : streams.notifyServerDrain();
        systems.zlink.framework.monitoring.ZLinkDrainForceReason requestedReason = reason;
        notification.handle((ignored, failure) -> null)
            .thenCompose(ignored -> closeAsync())
            .whenComplete((ignored, failure) -> completeForcedStop(failure == null
                ? requestedReason
                : systems.zlink.framework.monitoring.ZLinkDrainForceReason.TEARDOWN_FAILED));
    }

    private void completeForcedStop(
        systems.zlink.framework.monitoring.ZLinkDrainForceReason reason) {
        drained.complete(new systems.zlink.framework.monitoring.ForceStopped(reason));
    }

    private void publishDrainState(systems.zlink.framework.monitoring.ZLinkDrainState state) {
        if (eventDispatcher != null) {
            eventDispatcher.publish(new systems.zlink.framework.monitoring.ZLinkDrainEvent(
                state, java.time.Instant.now()));
        }
    }

    private void closeHandlerExecutor() {
        if (!registration.closeHandlerExecutor()) {
            return;
        }
        if (registration.handlerExecutor() instanceof ExecutorService executor) {
            executor.shutdown();
            try {
                if (!executor.awaitTermination(1, TimeUnit.SECONDS)) {
                    executor.shutdownNow();
                    executor.awaitTermination(4, TimeUnit.SECONDS);
                }
            } catch (InterruptedException ex) {
                executor.shutdownNow();
                Thread.currentThread().interrupt();
                throw new ZLinkConfigurationException("failed to close handler executor", ex);
            }
            return;
        }
        if (registration.handlerExecutor() instanceof AutoCloseable closeable) {
            try {
                closeable.close();
            } catch (Exception ex) {
                throw new ZLinkConfigurationException("failed to close handler executor", ex);
            }
        }
    }
}
