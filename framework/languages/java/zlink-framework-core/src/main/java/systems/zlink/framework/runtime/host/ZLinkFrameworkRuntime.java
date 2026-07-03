package systems.zlink.framework.runtime.host;

import systems.zlink.framework.runtime.backend.*;

import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.channels.ZLinkChannelRuntimeOptions;
import systems.zlink.framework.channels.ZLinkFanoutClient;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.monitoring.ZLinkRuntimeEventDispatcher;
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.configuration.ZLinkFrameworkRegistration;
import systems.zlink.framework.runtime.actors.ZLinkActorEntrySpotRoutePackets;
import systems.zlink.framework.runtime.actors.ZLinkActorRuntime;
import systems.zlink.framework.runtime.actors.ZLinkSessionActorsRuntime;
import systems.zlink.framework.runtime.channels.ZLinkChannelRuntime;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerFactory;
import systems.zlink.framework.runtime.locations.ZLinkLocationLifecycle;
import systems.zlink.framework.runtime.locations.ZLinkLocationAutoConnectHost;
import systems.zlink.framework.runtime.locations.ZLinkLocationRuntime;
import systems.zlink.framework.runtime.locations.ZLinkLocationRuntimeQueryService;
import systems.zlink.framework.runtime.locations.ZLinkLocationSpotRemoteAddressResolver;
import systems.zlink.framework.runtime.locations.ZLinkLocationStoreResolver;
import systems.zlink.framework.runtime.locations.ZLinkRegisteredLocationStores;
import systems.zlink.framework.runtime.locations.ZLinkStoreLocationResolvers;
import systems.zlink.framework.runtime.messaging.ZLinkJsonMessageSerializer;
import systems.zlink.framework.runtime.spots.ZLinkSpotRuntime;
import systems.zlink.framework.runtime.streams.ZLinkStreamRuntime;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.framework.spots.ZLinkSpotOutbound;
import systems.zlink.framework.spots.ZLinkSpotPublisherClient;
import systems.zlink.framework.spots.ZLinkSpotRemoteAddress;
import systems.zlink.framework.spots.ZLinkSpotRemoteAddressResolver;
import systems.zlink.framework.locations.ZLinkLocationRuntimeQuery;
import systems.zlink.framework.streams.ZLinkStreamCodec;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.errors.ZlinkCloseException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.TimeUnit;

public final class ZLinkFrameworkRuntime
    implements AutoCloseable, systems.zlink.framework.configuration.ZLinkMessageFlowControl {
    private final ZLinkChannelRuntime channels;
    private final ZLinkSpotRuntime spots;
    private final ZLinkActorRuntime actors;
    private final ZLinkStreamRuntime streams;
    private final ZLinkBackendContext backendContext;
    private final ZLinkFrameworkRegistration registration;
    private final ZLinkRegisteredLocationStores locationStores;
    private final ZLinkLocationRuntime locationRuntime;
    private final ZLinkLocationRuntimeQuery locationRuntimeQuery;
    private final ZLinkLocationLifecycle locationLifecycle;
    private final ZLinkLocationAutoConnectHost locationAutoConnectHost;
    private final ZLinkSpotRemoteAddressResolver locationSpotRemoteAddressResolver;
    private final java.util.concurrent.atomic.AtomicBoolean spotRuntimeStopped =
        new java.util.concurrent.atomic.AtomicBoolean(false);
    // Shared, runtime-mutable message-flow mode cell, installed into the diagnostics
    // options so every surface observes setMessageFlowMode live.
    private final java.util.concurrent.atomic.AtomicReference<
        systems.zlink.framework.configuration.ZLinkMessageFlowLogMode> messageFlowMode;

    ZLinkFrameworkRuntime(
        DefaultZLinkFrameworkOptions options,
        ZLinkBackendAdapterFactory backendFactory,
        ZLinkMessageSerializer serializer) {
        this(options, backendFactory, serializer, ZLinkHandlerFactory.reflection());
    }

    ZLinkFrameworkRuntime(
        DefaultZLinkFrameworkOptions options,
        ZLinkBackendAdapterFactory backendFactory,
        ZLinkMessageSerializer serializer,
        ZLinkHandlerFactory handlerFactory) {
        this(options, backendFactory, serializer, handlerFactory, null);
    }

    ZLinkFrameworkRuntime(
        DefaultZLinkFrameworkOptions options,
        ZLinkBackendAdapterFactory backendFactory,
        ZLinkMessageSerializer serializer,
        ZLinkHandlerFactory handlerFactory,
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
        ZLinkHandlerFactory.MutableServices runtimeHandlers =
            ZLinkHandlerFactory.services(handlerFactory);
        runtimeHandlers.add(ZLinkFrameworkRegistration.class, this.registration);
        runtimeHandlers.add(ZLinkFrameworkRuntime.class, this);
        this.locationStores = ZLinkLocationStoreResolver.resolve(
            this.registration.locations(),
            runtimeHandlers);
        if (this.locationStores != null) {
            this.locationStores.addTo(runtimeHandlers);
            this.locationRuntime = new ZLinkLocationRuntime(
                this.locationStores,
                this.registration.locations().options().ownerLeaseTtl(),
                this.registration.locations().options().heartbeatInterval());
            this.locationRuntime.startAsync(RoutingId.from(this.locationRuntime.ownerId()))
                .toCompletableFuture()
                .join();
            this.locationRuntimeQuery = new ZLinkLocationRuntimeQueryService(
                this.locationStores,
                this.locationRuntime,
                this.registration.locations().options());
            this.locationLifecycle = new ZLinkLocationLifecycle(this.locationRuntime);
            ZLinkStoreLocationResolvers storeLocationResolvers =
                new ZLinkStoreLocationResolvers(
                    this.locationStores,
                    this.registration.locations().options());
            this.locationAutoConnectHost = new ZLinkLocationAutoConnectHost(
                this.locationRuntime,
                storeLocationResolvers,
                this.registration.locations().options());
            ZLinkStoreLocationResolvers.AddressResolvers locationAddressResolvers =
                new ZLinkStoreLocationResolvers.AddressResolvers(
                    spotMeshNames(this.registration),
                    this.registration.locations().options().spotRouterChannels(),
                    storeLocationResolvers);
            this.locationSpotRemoteAddressResolver =
                new ZLinkLocationSpotRemoteAddressResolver(locationAddressResolvers);
            runtimeHandlers.add(ZLinkLocationRuntime.class, this.locationRuntime);
            runtimeHandlers.add(ZLinkLocationRuntimeQuery.class, this.locationRuntimeQuery);
            runtimeHandlers.add(ZLinkLocationLifecycle.class, this.locationLifecycle);
            runtimeHandlers.add(ZLinkLocationAutoConnectHost.class, this.locationAutoConnectHost);
            runtimeHandlers.add(ZLinkStoreLocationResolvers.class, storeLocationResolvers);
            runtimeHandlers.add(
                ZLinkStoreLocationResolvers.AddressResolvers.class,
                locationAddressResolvers);
            runtimeHandlers.add(
                ZLinkLocationSpotRemoteAddressResolver.class,
                this.locationSpotRemoteAddressResolver);
        } else {
            this.locationRuntime = null;
            this.locationRuntimeQuery = null;
            this.locationLifecycle = null;
            this.locationAutoConnectHost = null;
            this.locationSpotRemoteAddressResolver = null;
        }
        ZLinkChannelBackendAdapter channelBackend =
            backendFactory.createChannelAdapter(adapterOptions);
        ZLinkBackendContext backendContext = channelBackend.createContext();
        this.backendContext = backendContext;
        this.channels = new ZLinkChannelRuntime(
            channelBackend,
            backendContext,
            backendFactory,
            adapterOptions,
            options.registration(),
            serializer,
            runtimeHandlers);
        runtimeHandlers.add(ZLinkClient.class, this.channels);
        runtimeHandlers.add(ZLinkFanoutClient.class, this.channels);
        runtimeHandlers.add(ZLinkRouteClient.class, this.channels);
        if (options.registration().spotNodes().isEmpty()) {
            this.spots = null;
        } else {
            this.spots = new ZLinkSpotRuntime(
                backendFactory,
                adapterOptions,
                options.registration(),
                channels,
                backendContext,
                serializer,
                runtimeHandlers,
                eventDispatcher);
            this.spots.setLocationLifecycle(this.locationLifecycle);
        }
        Class<? extends ZLinkSpotRemoteAddressResolver> resolverType =
            options.registration().spotRemoteAddressResolverType();
        ZLinkSpotRemoteAddressResolver remoteAddressResolver = resolverType == null
            ? this.locationSpotRemoteAddressResolver
            : (ZLinkSpotRemoteAddressResolver) runtimeHandlers.create(resolverType);
        if (this.spots != null) {
            runtimeHandlers.add(ZLinkSpotManager.class, this.spots);
            if (remoteAddressResolver != null) {
                this.spots.setRemoteAddressResolver(remoteAddressResolver);
            }
        }
        if (this.spots != null) {
            this.channels.registerSpotRouteBridgeOwner(this.spots::primaryNode);
            this.channels.registerSpotRouteBridgeDispatchDrainer(
                this.spots::drainRoutedDispatchQueues);
        }
        if (this.locationAutoConnectHost != null) {
            this.locationAutoConnectHost.startAsync(
                    this.registration,
                    this.channels,
                    this.spots == null ? java.util.Map.of() : this.spots.nodesByName(),
                    this.spots)
                .toCompletableFuture()
                .join();
        }
        var actorNodeRegistration = options.registration().spotNodes().stream()
            .filter(node -> !node.actorFactories().isEmpty())
            .findFirst()
            .orElse(null);
        this.actors = spots != null && actorNodeRegistration != null
            ? new ZLinkActorRuntime(
                spots.node(actorNodeRegistration.nodeName()),
                actorNodeRegistration.actorFactories(),
                options.registration().defaultRequestTimeout(),
                serializer,
                runtimeHandlers,
                defaultStreamCodec)
            : null;
        if (this.actors != null) {
            runtimeHandlers.add(ZLinkActorManager.class, this.actors);
            this.actors.setLocationLifecycle(this.locationLifecycle);
            this.actors.setMessageFlowTracer(
                new systems.zlink.framework.runtime.diagnostics.ZLinkMessageFlowTracer(
                    this.registration.dispatchOptions(),
                    handlerFactory,
                    this.registration.handlerExecutor()));
            this.actors.setRoutedTransport(
                this.channels,
                () -> this.spots.primaryNode().entrySpot().routingId());
            if (remoteAddressResolver != null) {
                this.actors.setRemoteAddressResolver(remoteAddressResolver);
            }
        }
        if (this.actors != null) {
            this.spots.attachActorRuntime(this.actors);
            this.channels.registerRouteInternalRequestHandler(
                ZLinkActorEntrySpotRoutePackets.JOIN_ENTRY_SPOT_PACKET_NAME,
                this.actors::handleEntrySpotRouteJoin);
        }
        this.streams = options.registration().streamNodes().isEmpty()
            ? null
            : new ZLinkStreamRuntime(
                backendFactory,
                adapterOptions,
                options.registration(),
                spots == null ? java.util.Map.of() : spots.nodesByName(),
                serializer,
                actors,
                runtimeHandlers,
                spots == null ? ignored -> true : spots::isSessionRelayRouteReady,
                spots);
    }

    static ZLinkFrameworkRuntime start(
        DefaultZLinkFrameworkOptions options,
        ZLinkBackendAdapterFactory backendFactory) {
        return new ZLinkFrameworkRuntime(options, backendFactory, serializerFor(options));
    }

    static ZLinkFrameworkRuntime start(
        DefaultZLinkFrameworkOptions options,
        ZLinkBackendAdapterFactory backendFactory,
        ZLinkHandlerFactory handlerFactory) {
        return start(options, backendFactory, handlerFactory, null);
    }

    static ZLinkFrameworkRuntime start(
        DefaultZLinkFrameworkOptions options,
        ZLinkBackendAdapterFactory backendFactory,
        ZLinkHandlerFactory handlerFactory,
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

    public java.util.Map<String, ZLinkBackendSpotNode> monitoringSpotSources() {
        return spots == null ? java.util.Map.of() : spots.nodesByName();
    }

    public ZLinkLocationRuntimeQuery monitoringLocationRuntimeQuery() {
        if (locationRuntimeQuery == null) {
            throw new ZLinkConfigurationException("Location runtime is not configured");
        }
        return locationRuntimeQuery;
    }

    public boolean stopSpotRuntime() {
        if (spots == null || !spotRuntimeStopped.compareAndSet(false, true)) {
            return false;
        }
        spots.beginClose();
        closeRuntimeComponent(spots::close);
        return true;
    }

    public ZLinkActorManager actorManager() {
        if (actors == null) {
            throw new ZLinkConfigurationException("Actor runtime is not configured");
        }
        return actors;
    }

    public ZLinkSessionActorsRuntime sessionActors(String streamNodeName, RoutingId sessionRid) {
        if (streams == null) {
            throw new ZLinkConfigurationException("Stream runtime is not configured");
        }
        return streams.sessionActors(streamNodeName, sessionRid, actors);
    }

    private static java.util.List<String> spotMeshNames(ZLinkFrameworkRegistration registration) {
        return registration.spotNodes().stream()
            .map(systems.zlink.framework.runtime.spots.SpotNodeRegistration::meshName)
            .distinct()
            .toList();
    }

    @Override
    public void close() {
        if (spots != null && !spotRuntimeStopped.get()) {
            spots.beginClose();
        }
        channels.beginClose();
        try {
            if (actors != null) {
                closeRuntimeComponent(actors::close);
            }
        } finally {
            try {
                if (streams != null) {
                    closeRuntimeComponent(streams::close);
                }
            } finally {
                try {
                    if (locationAutoConnectHost != null) {
                        closeRuntimeComponent(() -> locationAutoConnectHost.stopAsync()
                            .toCompletableFuture()
                            .join());
                    }
                    closeRuntimeComponent(channels::close);
                } finally {
                    try {
                        if (spots != null && spotRuntimeStopped.compareAndSet(false, true)) {
                            closeRuntimeComponent(spots::close);
                        }
                    } finally {
                        try {
                            if (locationRuntime != null) {
                                closeRuntimeComponent(() -> locationRuntime.stopAsync()
                                    .toCompletableFuture()
                                    .join());
                                closeRuntimeComponent(locationLifecycle::close);
                                closeRuntimeComponent(locationRuntime::close);
                            }
                        } finally {
                            try {
                                closeRuntimeComponent(backendContext::close);
                            } finally {
                                closeHandlerExecutor();
                            }
                        }
                    }
                }
            }
        }
    }

    private static void closeRuntimeComponent(Runnable close) {
        try {
            close.run();
        } catch (ZlinkCloseException ignored) {
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
