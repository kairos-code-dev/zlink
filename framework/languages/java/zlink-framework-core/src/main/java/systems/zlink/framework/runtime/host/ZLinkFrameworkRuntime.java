package systems.zlink.framework.runtime.host;

import systems.zlink.framework.runtime.internal.backend.ZLinkBackendAdapterProvider;

import systems.zlink.framework.runtime.internal.backend.ZLinkInternalSpotNode;
import systems.zlink.framework.runtime.internal.backend.ZLinkInternalMeshNode;

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
import systems.zlink.framework.runtime.locations.ZLinkAllocatedRoutingIdRuntime;
import systems.zlink.framework.runtime.locations.ZLinkRegisteredLocationStores;
import systems.zlink.framework.runtime.locations.ZLinkStoreLocationResolvers;
import systems.zlink.framework.runtime.locations.ZLinkStatefulAuthorityRouteRuntime;
import systems.zlink.framework.runtime.messaging.ZLinkJsonMessageSerializer;
import systems.zlink.framework.runtime.mesh.ZLinkMeshNodesRuntime;
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
        systems.zlink.framework.monitoring.ZLinkDrainControl,
        systems.zlink.framework.monitoring.ZLinkRuntimeQuery {
    public static final java.time.Duration DEFAULT_TERMINATION_DEADLINE =
        java.time.Duration.ofSeconds(30);
    private final ZLinkChannelRuntime channels;
    private final ZLinkMeshNodesRuntime meshNodes;
    private final ZLinkSpotRuntime spots;
    private final ZLinkActorRuntime actors;
    private final ZLinkActorDirectory actorDirectory;
    private final ZLinkActorClient actorClient;
    private final ZLinkStreamRuntime streams;
    private final ZLinkBackendContext backendContext;
    private final ZLinkFrameworkRegistration registration;
    private final ZLinkRegisteredLocationStores locationStores;
    private final systems.zlink.framework.runtime.internal.drain.ZLinkMeshDrainCoordinator
        meshDrains;
    private final ZLinkLocationRuntime locationRuntime;
    private final ZLinkLocationRuntimeQuery locationRuntimeQuery;
    private final ZLinkLocationLifecycle locationLifecycle;
    private final ZLinkLocationAutoConnectHost locationAutoConnectHost;
    private final ZLinkStatefulAuthorityRouteRuntime
        authorityRouteRuntime;
    private final systems.zlink.framework.runtime.locations
        .ZLinkObjectServerDescriptorPublisher objectDescriptors;
    private final ZLinkAllocatedRoutingIdRuntime allocatedRoutingIds;
    private final systems.zlink.framework.runtime.internal.spots.SpotTransportAddressResolver
        spotTransportAddressResolver;
    private final ZLinkStoreLocationResolvers storeLocationResolvers;
    private final java.util.concurrent.atomic.AtomicBoolean spotRuntimeStopped =
        new java.util.concurrent.atomic.AtomicBoolean(false);
    private final java.util.concurrent.atomic.AtomicBoolean ready =
        new java.util.concurrent.atomic.AtomicBoolean(false);
    private final java.util.concurrent.atomic.AtomicReference<
        ZLinkFrameworkRuntimeState> runtimeState =
        new java.util.concurrent.atomic.AtomicReference<>(
            ZLinkFrameworkRuntimeState.PREPARING);
    private final java.util.concurrent.atomic.AtomicReference<
        ZLinkTerminationIntent> effectiveTerminationIntent =
        new java.util.concurrent.atomic.AtomicReference<>();
    private final java.util.concurrent.atomic.AtomicReference<
        ZLinkTerminationReason> terminationBlocker =
        new java.util.concurrent.atomic.AtomicReference<>();
    private final java.util.concurrent.atomic.AtomicReference<
        ZLinkTerminationResult> terminalTermination =
        new java.util.concurrent.atomic.AtomicReference<>();
    private final java.util.concurrent.atomic.AtomicReference<
        java.util.concurrent.CompletableFuture<ZLinkTerminationResult>>
        activeTermination =
        new java.util.concurrent.atomic.AtomicReference<>();
    private final java.util.concurrent.atomic.AtomicReference<
        java.time.Instant> terminationDeadline =
        new java.util.concurrent.atomic.AtomicReference<>();
    private final java.util.concurrent.atomic.AtomicLong terminationSequence =
        new java.util.concurrent.atomic.AtomicLong();
    private final java.util.concurrent.CopyOnWriteArrayList<
        java.util.concurrent.SubmissionPublisher<
            systems.zlink.framework.monitoring.ZLinkFrameworkRuntimeEvent>>
        terminationObservers =
        new java.util.concurrent.CopyOnWriteArrayList<>();
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
        this.meshDrains = new systems.zlink.framework.runtime.internal.drain
            .ZLinkMeshDrainCoordinator(this.registration.meshNodes().stream()
                .map(systems.zlink.framework.runtime.mesh.MeshNodeRegistration::meshName)
                .toList());
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
        var relocationAdapters = new systems.zlink.framework.runtime.internal.relocation
            .ZLinkRelocationAdapterRegistry(this.registration, handlerFactory);
        if (relocationAdapters.hasAdapters()) {
            runtimeHandlers.add(
                systems.zlink.framework.runtime.internal.relocation
                    .ZLinkRelocationAdapterRegistry.class,
                relocationAdapters);
        }
        ZLinkFrameworkLocationSubsystem locationSubsystem =
            ZLinkFrameworkLocationSubsystem.create(this.registration, runtimeHandlers);
        if (this.registration.relocationStore() != null) {
            runtimeHandlers.add(
                systems.zlink.framework.locations.ZLinkRelocationStore.class,
                this.registration.relocationStore());
        }
        this.locationStores = locationSubsystem.locationStores();
        if (this.locationStores != null
            && this.locationStores.authorityStore() != null
            && this.registration.relocationStore() != null) {
            runtimeHandlers.add(
                systems.zlink.framework.runtime.internal.locations
                    .ZLinkRelocationPublicationCoordinator.class,
                new systems.zlink.framework.runtime.internal.locations
                    .ZLinkRelocationPublicationCoordinator(
                        this.locationStores.authorityStore(),
                        this.registration.relocationStore()));
        }
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
        boolean hasAllocatedRoutingIds = this.registration.meshNodes().stream()
            .anyMatch(node -> node.allocationSlotCount() != null);
        if (hasAllocatedRoutingIds) {
            if (!locationSubsystem.enabled()
                || !(this.locationStores.unifiedStore()
                    instanceof systems.zlink.framework.locations.ZLinkRoutingIdSlotAllocationStore store)) {
                throw new ZLinkConfigurationException(
                    "allocated routing IDs require a location store with slot allocation support");
            }
            locationSubsystem.startup().toCompletableFuture().join();
            this.allocatedRoutingIds = new ZLinkAllocatedRoutingIdRuntime(
                this.registration,
                store,
                this.locationRuntime,
                this.registration.locations().options());
            runtimeHandlers.add(
                systems.zlink.framework.locations.ZLinkAllocatedRoutingIdProvider.class,
                this.allocatedRoutingIds);
            this.allocatedRoutingIds.start();
        } else {
            this.allocatedRoutingIds = null;
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
        if (this.registration.meshNodes().isEmpty()) {
            this.meshNodes = ZLinkMeshNodesRuntime.empty();
        } else {
            systems.zlink.framework.runtime.backend.ZLinkMeshBackendAdapter meshAdapter =
                backendFactory.createMeshAdapter(adapterOptions);
            this.meshNodes = ZLinkMeshNodesRuntime.start(
                this.registration.meshNodes(),
                meshAdapter,
                this.backendContext,
                mesh -> new systems.zlink.framework.runtime.channels.ZLinkMeshApplicationDispatcher(
                    mesh,
                    serializer,
                    this.registration,
                    handlerFactory,
                    this.meshDrains));
        }
        this.objectDescriptors =
            this.locationRuntime != null
                && this.locationStores.unifiedStore()
                    instanceof systems.zlink.framework.locations
                        .ZLinkLocationStore store
                ? new systems.zlink.framework.runtime.locations
                    .ZLinkObjectServerDescriptorPublisher(
                        store,
                        this.locationRuntime,
                        this.registration,
                        this.meshNodes.nodesByName())
                : null;
        runtimeHandlers.add(
            systems.zlink.framework.channels.ZLinkRouteMeshRuntimeOptions.class,
            new systems.zlink.framework.runtime.channels
                .ZLinkRouteMeshRuntimeOptionsRuntime(
                    this.meshNodes.nodesByName(),
                    () -> {
                        if (this.objectDescriptors != null) {
                            this.objectDescriptors
                                .publish(this.runtimeState.get())
                                .toCompletableFuture()
                                .join();
                        }
                    }));

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
            this.locationStores == null
                ? null : this.locationStores.authorityStore(),
            this.locationStores != null
                && this.locationStores.unifiedStore()
                    instanceof systems.zlink.framework.locations.ZLinkLocationStore
                        store
                ? store
                : null,
            this.locationRuntime,
            this.spotTransportAddressResolver,
            this.meshNodes.nodesByName());
        this.spots = spotSubsystem.spots();

        this.authorityRouteRuntime =
            this.locationStores != null
                && this.locationStores.authorityStore() != null
                && !this.meshNodes.nodesByName().isEmpty()
            ? new ZLinkStatefulAuthorityRouteRuntime(
                this.locationStores.authorityStore(),
                this.locationStores.watchStore(),
                this.meshNodes.nodesByName(),
                this.registration.locations().options()
                    .pollingInterval(),
                failure -> java.util.logging.Logger.getLogger(
                    ZLinkStatefulAuthorityRouteRuntime.class
                        .getName())
                    .warning(
                        "Durable authority route reconcile failed: "
                            + failure.getMessage()))
            : null;

        ZLinkFrameworkActorSubsystem actorSubsystem = ZLinkFrameworkActorSubsystem.create(
            backendFactory,
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
            spotSubsystem.remoteAddressResolver(),
            this.locationStores != null
                && this.locationStores.unifiedStore()
                    instanceof systems.zlink.framework.locations
                        .ZLinkLocationStore store
                ? store
                : null,
            this.meshNodes.nodesByName());
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
            this.meshNodes,
            this.backendContext,
            this.spots,
            this.actors);
        this.streams = streamSubsystem.streams();

        locationSubsystem.startup()
            .thenCompose(ignored ->
                this.objectDescriptors == null
                    ? java.util.concurrent.CompletableFuture
                        .completedFuture(null)
                    : this.objectDescriptors.publish(
                        ZLinkFrameworkRuntimeState.SERVING))
            .thenCompose(ignored -> spotSubsystem.startup())
            .thenCompose(ignored ->
                this.authorityRouteRuntime == null
                    ? java.util.concurrent.CompletableFuture
                        .completedFuture(null)
                    : this.authorityRouteRuntime.start())
            .thenCompose(ignored -> ZLinkFrameworkAutoConnectSubsystem.start(
                this.locationAutoConnectHost,
                this.registration,
                this.channels,
                this.meshNodes,
                this.spots))
            .whenComplete((ignored, failure) -> {
                if (failure == null && !drainStarted.get()) {
                    if (allocatedRoutingIds != null) {
                        allocatedRoutingIds.markReady();
                    }
                    ready.set(true);
                    publishRuntimeState(ZLinkFrameworkRuntimeState.SERVING);
                    publishDrainState(systems.zlink.framework.monitoring.ZLinkDrainState.SERVING);
                } else if (failure != null) {
                    publishRuntimeState(ZLinkFrameworkRuntimeState.ERROR);
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

    public java.util.Map<String, ZLinkInternalMeshNode> monitoringSpotSources() {
        return meshNodes.nodesByName();
    }

    public ZLinkLocationRuntimeQuery monitoringLocationRuntimeQuery() {
        if (locationRuntimeQuery == null) {
            throw new ZLinkConfigurationException("Location runtime is not configured");
        }
        return locationRuntimeQuery;
    }

    public systems.zlink.framework.locations.ZLinkAllocatedRoutingIdProvider allocatedRoutingIds() {
        if (allocatedRoutingIds == null) {
            throw new ZLinkConfigurationException(
                "Routing ID allocation is not configured");
        }
        return allocatedRoutingIds;
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
    public ZLinkFrameworkRuntimeState state() {
        return runtimeState.get();
    }

    @Override
    public systems.zlink.framework.monitoring.ZLinkFrameworkRuntimeSnapshot
        snapshot() {
        return runtimeSnapshot(terminationSequence.get());
    }

    @Override
    public java.util.concurrent.Flow.Publisher<
        systems.zlink.framework.monitoring.ZLinkFrameworkRuntimeEvent>
        observe(int capacity) {
        if (capacity <= 0) {
            throw new IllegalArgumentException(
                "observer capacity must be positive");
        }
        var publisher = new java.util.concurrent.SubmissionPublisher<
            systems.zlink.framework.monitoring.ZLinkFrameworkRuntimeEvent>(
                java.util.concurrent.ForkJoinPool.commonPool(),
                capacity);
        return subscriber -> {
            publisher.subscribe(subscriber);
            terminationObservers.addIfAbsent(publisher);
            long sequence = terminationSequence.incrementAndGet();
            publisher.submit(new systems.zlink.framework.monitoring
                .ZLinkFrameworkRuntimeEvent(
                    sequence,
                    java.time.Instant.now(),
                    runtimeSnapshot(sequence)));
        };
    }

    public java.util.concurrent.CompletionStage<ZLinkTerminationResult>
        retire() {
        return retire(DEFAULT_TERMINATION_DEADLINE);
    }

    public java.util.concurrent.CompletionStage<ZLinkTerminationResult>
        retire(java.time.Duration deadline) {
        return beginTermination(ZLinkTerminationIntent.RETIRE, deadline);
    }

    public java.util.concurrent.CompletionStage<ZLinkTerminationResult>
        shutdown() {
        return shutdown(DEFAULT_TERMINATION_DEADLINE);
    }

    public java.util.concurrent.CompletionStage<ZLinkTerminationResult>
        shutdown(java.time.Duration deadline) {
        return beginTermination(ZLinkTerminationIntent.SHUTDOWN, deadline);
    }

    private java.util.concurrent.CompletionStage<ZLinkTerminationResult>
        beginTermination(
            ZLinkTerminationIntent intent,
            java.time.Duration deadline) {
        java.util.Objects.requireNonNull(intent, "intent");
        java.util.Objects.requireNonNull(deadline, "deadline");
        if (deadline.isZero() || deadline.isNegative()) {
            throw new IllegalArgumentException(
                "termination deadline must be positive");
        }
        java.util.concurrent.CompletableFuture<ZLinkTerminationResult>
            current = activeTermination.get();
        if (current != null) {
            if (intent == ZLinkTerminationIntent.SHUTDOWN
                && !drainStarted.get()
                && effectiveTerminationIntent.compareAndSet(
                    ZLinkTerminationIntent.RETIRE,
                    ZLinkTerminationIntent.SHUTDOWN)) {
                startTermination(current, deadline);
            }
            return independentWaiter(current);
        }
        var candidate =
            new java.util.concurrent.CompletableFuture<
                ZLinkTerminationResult>();
        if (!activeTermination.compareAndSet(null, candidate)) {
            return beginTermination(intent, deadline);
        }
        terminalTermination.set(null);
        terminationBlocker.set(null);
        terminationDeadline.set(java.time.Instant.now().plus(deadline));
        effectiveTerminationIntent.set(intent);
        publishRuntimeState(runtimeState.get());
        if (intent == ZLinkTerminationIntent.SHUTDOWN) {
            startTermination(candidate, deadline);
            return independentWaiter(candidate);
        }
        retirePreflight().whenComplete((reason, failure) -> {
            if (candidate.isDone()) {
                return;
            }
            if (effectiveTerminationIntent.get()
                == ZLinkTerminationIntent.SHUTDOWN) {
                startTermination(candidate, deadline);
                return;
            }
            ZLinkTerminationReason blocker = failure == null
                ? reason
                : ZLinkTerminationReason.STORE_UNAVAILABLE;
            if (blocker != ZLinkTerminationReason.NONE) {
                terminationBlocker.set(blocker);
                ZLinkTerminationResult blocked =
                    new ZLinkTerminationResult(
                        ZLinkTerminationIntent.RETIRE,
                        ZLinkTerminationOutcome.BLOCKED,
                        blocker);
                terminalTermination.set(blocked);
                publishRuntimeState(ZLinkFrameworkRuntimeState.SERVING);
                candidate.complete(blocked);
                effectiveTerminationIntent.compareAndSet(
                    ZLinkTerminationIntent.RETIRE, null);
                activeTermination.compareAndSet(candidate, null);
                return;
            }
            publishRuntimeState(ZLinkFrameworkRuntimeState.RETIRING);
            startTermination(candidate, deadline);
        });
        return independentWaiter(candidate);
    }

    private void startTermination(
        java.util.concurrent.CompletableFuture<ZLinkTerminationResult>
            completion,
        java.time.Duration deadline) {
        drain(deadline).whenComplete((result, failure) -> {
            if (completion.isDone()) {
                return;
            }
            ZLinkTerminationIntent intent =
                effectiveTerminationIntent.get();
            if (intent == null) {
                intent = ZLinkTerminationIntent.SHUTDOWN;
            }
            ZLinkTerminationResult terminal;
            if (failure != null) {
                terminal = new ZLinkTerminationResult(
                    intent,
                    ZLinkTerminationOutcome.FORCE_STOPPED,
                    ZLinkTerminationReason.TEARDOWN_FAILED);
            } else if (
                result instanceof systems.zlink.framework.monitoring.Drained) {
                terminal = new ZLinkTerminationResult(
                    intent,
                    ZLinkTerminationOutcome.STOPPED,
                    ZLinkTerminationReason.NONE);
            } else {
                terminal = new ZLinkTerminationResult(
                    intent,
                    ZLinkTerminationOutcome.FORCE_STOPPED,
                    ZLinkTerminationReason.DEADLINE_EXCEEDED);
            }
            terminalTermination.set(terminal);
            publishRuntimeState(ZLinkFrameworkRuntimeState.STOPPED);
            completion.complete(terminal);
        });
    }

    private java.util.concurrent.CompletionStage<ZLinkTerminationReason>
        retirePreflight() {
        if (!ready.get()) {
            return java.util.concurrent.CompletableFuture.completedFuture(
                ZLinkTerminationReason.RUNTIME_NOT_READY);
        }
        boolean hasActiveRelocatableWork =
            (actors != null && !actors.activeActorTypes().isEmpty())
                || (spots != null && spots.activeUserSpotCount() > 0);
        if (hasActiveRelocatableWork) {
            boolean relocationPolicyConfigured =
                registration.meshNodes().stream()
                    .anyMatch(
                        systems.zlink.framework.runtime.mesh
                            .MeshNodeRegistration::requiresRelocationStore);
            return java.util.concurrent.CompletableFuture.completedFuture(
                relocationPolicyConfigured
                    ? ZLinkTerminationReason.RELOCATION_FAILED
                    : ZLinkTerminationReason.RELOCATION_DISABLED);
        }
        if (actors == null || actors.activeActorTypes().isEmpty()) {
            return java.util.concurrent.CompletableFuture.completedFuture(
                ZLinkTerminationReason.NONE);
        }
        if (storeLocationResolvers == null || spots == null) {
            return java.util.concurrent.CompletableFuture.completedFuture(
                ZLinkTerminationReason.STORE_UNAVAILABLE);
        }
        java.util.Set<RoutingId> localNodes = new java.util.HashSet<>();
        spots.nodesByName().values().forEach(
            node -> localNodes.add(node.routingId()));
        java.util.concurrent.CompletionStage<ZLinkTerminationReason> result =
            java.util.concurrent.CompletableFuture.completedFuture(
                ZLinkTerminationReason.NONE);
        for (String actorType :
            actors.activeActorTypes().stream().sorted().toList()) {
            result = result.thenCompose(current -> {
                if (current != ZLinkTerminationReason.NONE) {
                    return java.util.concurrent.CompletableFuture
                        .completedFuture(current);
                }
                String meshName =
                    actorDrainMeshName(registration, actorType);
                if (meshName == null) {
                    return java.util.concurrent.CompletableFuture
                        .completedFuture(
                            ZLinkTerminationReason.TARGET_UNAVAILABLE);
                }
                return storeLocationResolvers.listLivePeers(
                    new systems.zlink.framework.locations
                        .ZLinkPeerLocationFilter(
                            systems.zlink.framework.locations
                                .ZLinkLocationAutoConnectType.SPOT_MESH,
                            meshName,
                            systems.zlink.framework.locations
                                .ZLinkLocationRole.SPOT,
                            null,
                            null))
                    .thenApply(found -> found.stream()
                        .filter(peer -> !peer.draining())
                        .anyMatch(peer -> isEligibleActorHandoffTarget(
                            peer,
                            actorType,
                            localNodes))
                            ? ZLinkTerminationReason.NONE
                            : ZLinkTerminationReason.TARGET_UNAVAILABLE);
            });
        }
        return result;
    }

    private systems.zlink.framework.monitoring
        .ZLinkFrameworkRuntimeSnapshot runtimeSnapshot(long sequence) {
        return new systems.zlink.framework.monitoring
            .ZLinkFrameworkRuntimeSnapshot(
                runtimeState.get(),
                java.util.Optional.ofNullable(
                    effectiveTerminationIntent.get()),
                java.util.Optional.ofNullable(terminationDeadline.get()),
                drainStarted.get(),
                java.util.Optional.ofNullable(terminationBlocker.get()),
                0,
                0,
                0,
                java.util.Optional.ofNullable(terminalTermination.get()),
                sequence,
                java.time.Instant.now());
    }

    private void publishRuntimeState(
        ZLinkFrameworkRuntimeState state) {
        runtimeState.set(state);
        if (objectDescriptors != null
            && (state == ZLinkFrameworkRuntimeState.RETIRING
                || state == ZLinkFrameworkRuntimeState.DRAINING)) {
            objectDescriptors.publish(state).exceptionally(failure -> {
                java.util.logging.Logger.getLogger(
                    ZLinkFrameworkRuntime.class.getName())
                    .warning(
                        "Object Server descriptor state publication failed: "
                            + failure.getMessage());
                return null;
            });
        }
        long sequence = terminationSequence.incrementAndGet();
        var event = new systems.zlink.framework.monitoring
            .ZLinkFrameworkRuntimeEvent(
                sequence,
                java.time.Instant.now(),
                runtimeSnapshot(sequence));
        terminationObservers.forEach(publisher -> publisher.submit(event));
        if (state == ZLinkFrameworkRuntimeState.STOPPED
            || state == ZLinkFrameworkRuntimeState.ERROR) {
            terminationObservers.forEach(
                java.util.concurrent.SubmissionPublisher::close);
            terminationObservers.clear();
        }
        if (eventDispatcher != null) {
            eventDispatcher.publish(event);
        }
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
        if (allocatedRoutingIds != null) {
            shutdown.defer(allocatedRoutingIds::close);
        }
        if (authorityRouteRuntime != null) {
            shutdown.defer(authorityRouteRuntime::close);
        }
        shutdown.defer(meshNodes::close);
        if (locationRuntime != null) {
            shutdown.defer(locationRuntime::close);
            shutdown.defer(locationLifecycle::close);
            shutdown.deferStage(locationRuntime::stop);
            if (objectDescriptors != null) {
                shutdown.deferStage(objectDescriptors::remove);
            }
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
            if (failure != null) {
                publishRuntimeState(ZLinkFrameworkRuntimeState.ERROR);
            } else if (!drainStarted.get()) {
                publishRuntimeState(ZLinkFrameworkRuntimeState.STOPPED);
            } else if (activeTermination.get() == null) {
                terminalTermination.set(new ZLinkTerminationResult(
                    ZLinkTerminationIntent.SHUTDOWN,
                    ZLinkTerminationOutcome.STOPPED,
                    ZLinkTerminationReason.NONE));
                publishRuntimeState(ZLinkFrameworkRuntimeState.STOPPED);
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
            effectiveTerminationIntent.compareAndSet(
                null, ZLinkTerminationIntent.SHUTDOWN);
            terminationDeadline.compareAndSet(
                null, java.time.Instant.now().plus(deadline));
            meshDrains.sealAll();
            if (streams != null) {
                streams.beginDrain();
            }
            if (spots != null) {
                spots.beginDrain().exceptionally(error -> null);
            }
            if (actors != null) {
                actors.beginDrain();
            }
            ready.set(false);
            publishRuntimeState(ZLinkFrameworkRuntimeState.DRAINING);
            publishDrainState(systems.zlink.framework.monitoring.ZLinkDrainState.DRAINING);
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
            java.util.concurrent.CompletionStage<Void> applicationBarrier =
                meshDrains.awaitAllZero();
            if (spots != null) {
                applicationBarrier = applicationBarrier.thenCompose(
                    acceptedIgnored -> spots.awaitDrainBarrier());
            }
            if (actors != null) {
                applicationBarrier = applicationBarrier.thenCompose(
                    acceptedIgnored -> actors.awaitDrainBarrier());
            }
            if (streams != null) {
                applicationBarrier = applicationBarrier.thenCompose(
                    acceptedIgnored -> streams.awaitDrainBarrier());
            }
            applicationBarrier
                .whenComplete((barrierIgnored, barrierFailure) -> {
                if (barrierFailure != null) {
                    forceStop(
                        systems.zlink.framework.monitoring
                            .ZLinkDrainForceReason.TEARDOWN_FAILED);
                    return;
                }
                java.util.concurrent.CompletionStage<Void> streamBarrier = streams == null
                    ? java.util.concurrent.CompletableFuture.completedFuture(null)
                    : streams.awaitDrainBarrier()
                        .thenCompose(streamIgnored -> streams.notifyServerDrain());
                java.util.concurrent.CompletionStage<Void> actorShutdown =
                    streamBarrier.thenCompose(streamIgnored ->
                        actors == null
                            ? java.util.concurrent.CompletableFuture
                                .completedFuture(null)
                            : actors.closeAsync());
                java.util.concurrent.CompletionStage<Void> spotDrain = actorShutdown.thenCompose(
                    streamIgnored -> spots == null
                        ? java.util.concurrent.CompletableFuture.completedFuture(null)
                        : spots.continueDrain(
                            systems.zlink.framework.spots
                                .ZLinkSpotCloseReason.HOST_SHUTDOWN,
                            java.util.Optional.ofNullable(
                                    terminationDeadline.get())
                                .orElseGet(java.time.Instant::now)));
                spotDrain.thenCompose(spotIgnored -> awaitWorkloadsDrained())
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
                    ? spots.continueDrain(
                            systems.zlink.framework.spots
                                .ZLinkSpotCloseReason.HOST_SHUTDOWN,
                            java.util.Optional.ofNullable(
                                    terminationDeadline.get())
                                .orElseGet(java.time.Instant::now))
                        .exceptionally(error -> null)
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
        closeAsync()
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
