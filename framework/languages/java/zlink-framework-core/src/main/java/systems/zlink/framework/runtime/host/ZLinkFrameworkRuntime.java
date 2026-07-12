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
        new java.util.concurrent.atomic.AtomicBoolean(true);
    private final java.util.concurrent.CompletableFuture<
        systems.zlink.framework.monitoring.ZLinkDrainResult> drained =
        new java.util.concurrent.CompletableFuture<>();
    // Shared, runtime-mutable message-flow mode cell, installed into the diagnostics
    // options so every surface observes setMessageFlowMode live.
    private final java.util.concurrent.atomic.AtomicReference<
        systems.zlink.framework.configuration.ZLinkMessageFlowLogMode> messageFlowMode;

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
            this.actors);
        this.streams = streamSubsystem.streams();

        ZLinkFrameworkAutoConnectSubsystem.start(
            this.locationAutoConnectHost,
            this.registration,
            this.channels,
            this.spots);
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
        ZLinkFrameworkShutdown shutdown = new ZLinkFrameworkShutdown();
        shutdown.defer(spots::close);
        shutdown.close();
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

    public ZLinkSessionActorsRuntime sessionActors(String streamNodeName, RoutingId sessionRid) {
        if (streams == null) {
            throw new ZLinkConfigurationException("Stream runtime is not configured");
        }
        return streams.sessionActors(streamNodeName, sessionRid, actors);
    }

    @Override
    public void close() {
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
            shutdown.defer(() -> locationRuntime.stop().toCompletableFuture().join());
        }
        if (spots != null) {
            shutdown.defer(() -> {
                if (spotRuntimeStopped.compareAndSet(false, true)) {
                    spots.close();
                }
            });
        }
        shutdown.defer(channels::close);
        if (locationAutoConnectHost != null) {
            shutdown.defer(() -> locationAutoConnectHost.stop().toCompletableFuture().join());
        }
        if (streams != null) {
            shutdown.defer(streams::close);
        }
        if (actors != null) {
            shutdown.defer(actors::close);
        }
        shutdown.close();
        drained.complete(new systems.zlink.framework.monitoring.Drained());
    }

    @Override
    public java.util.concurrent.CompletionStage<systems.zlink.framework.monitoring.ZLinkDrainResult> drain() {
        return drain(java.time.Duration.ofSeconds(30));
    }

    @Override
    public java.util.concurrent.CompletionStage<systems.zlink.framework.monitoring.ZLinkDrainResult> drain(
        java.time.Duration deadline) {
        java.util.Objects.requireNonNull(deadline, "deadline");
        if (ready.compareAndSet(true, false)) {
            java.util.concurrent.CompletableFuture.runAsync(this::close)
                .whenComplete((ignored, error) -> drained.complete(
                    error == null
                        ? new systems.zlink.framework.monitoring.Drained()
                        : new systems.zlink.framework.monitoring.ForceStopped(
                            systems.zlink.framework.monitoring.ZLinkDrainForceReason.TEARDOWN_FAILED)));
            java.util.concurrent.CompletableFuture.delayedExecutor(
                Math.max(0L, deadline.toMillis()), java.util.concurrent.TimeUnit.MILLISECONDS)
                .execute(() -> drained.complete(new systems.zlink.framework.monitoring.ForceStopped(
                    systems.zlink.framework.monitoring.ZLinkDrainForceReason.DEADLINE_EXCEEDED)));
        }
        return drained;
    }

    @Override
    public java.util.concurrent.CompletionStage<systems.zlink.framework.monitoring.ZLinkDrainResult> awaitDrained() {
        return drained;
    }

    @Override
    public boolean isReady() {
        return ready.get();
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
