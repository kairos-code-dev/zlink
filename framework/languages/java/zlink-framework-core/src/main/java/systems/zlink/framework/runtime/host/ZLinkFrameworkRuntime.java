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
import systems.zlink.framework.runtime.messaging.ZLinkJsonMessageSerializer;
import systems.zlink.framework.runtime.messaging.ZLinkStringMessageSerializer;
import systems.zlink.framework.runtime.registry.ZLinkRegistrySpotRemoteAddressResolver;
import systems.zlink.framework.runtime.spots.ZLinkSpotRuntime;
import systems.zlink.framework.runtime.streams.ZLinkStreamRuntime;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.framework.spots.ZLinkSpotOutbound;
import systems.zlink.framework.spots.ZLinkSpotPublisherClient;
import systems.zlink.framework.spots.ZLinkSpotRemoteAddress;
import systems.zlink.framework.spots.ZLinkSpotRemoteAddressResolver;
import systems.zlink.framework.streams.ZLinkStreamCodec;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.errors.ZlinkCloseException;

public final class ZLinkFrameworkRuntime
    implements AutoCloseable, systems.zlink.framework.configuration.ZLinkMessageFlowControl {
    private final ZLinkChannelRuntime channels;
    private final ZLinkSpotRuntime spots;
    private final ZLinkActorRuntime actors;
    private final ZLinkStreamRuntime streams;
    private final ZLinkFrameworkRegistration registration;
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
        this.channels = new ZLinkChannelRuntime(
            backendFactory.createChannelAdapter(adapterOptions),
            backendFactory,
            adapterOptions,
            options.registration(),
            serializer,
            runtimeHandlers);
        runtimeHandlers.add(ZLinkClient.class, this.channels);
        runtimeHandlers.add(ZLinkFanoutClient.class, this.channels);
        runtimeHandlers.add(ZLinkRouteClient.class, this.channels);
        this.spots = options.registration().spotNodes().isEmpty()
            ? null
            : new ZLinkSpotRuntime(
                backendFactory,
                adapterOptions,
                options.registration(),
                channels,
                serializer,
                runtimeHandlers,
                eventDispatcher);
        Class<? extends ZLinkSpotRemoteAddressResolver> resolverType =
            options.registration().spotRemoteAddressResolverType();
        if (this.spots != null) {
            runtimeHandlers.add(ZLinkSpotManager.class, this.spots);
            if (resolverType != null) {
                this.spots.setRemoteAddressResolver(
                    (ZLinkSpotRemoteAddressResolver) runtimeHandlers.create(resolverType));
            }
        }
        if (this.spots != null) {
            this.channels.registerSpotRouteBridgeOwner(this.spots::primaryNode);
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
            this.actors.setMessageFlowTracer(
                new systems.zlink.framework.runtime.diagnostics.ZLinkMessageFlowTracer(
                    this.registration.dispatchOptions(),
                    handlerFactory,
                    this.registration.handlerExecutor()));
            this.actors.setRoutedTransport(
                this.channels,
                () -> this.spots.primaryNode().entrySpot().routingId());
            if (resolverType != null) {
                this.actors.setRemoteAddressResolver(
                    (ZLinkSpotRemoteAddressResolver) runtimeHandlers.create(resolverType));
            } else if (options.registration().registrySpotRemoteAddresses() != null) {
                this.actors.setRemoteAddressResolver(
                    new ZLinkRegistrySpotRemoteAddressResolver(this, options.registration()));
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
        ZLinkMessageSerializer fallback =
            options.registration().codecs().registeredCodecs().isEmpty()
                && options.registration().codecs().serializers().isEmpty()
                ? new ZLinkStringMessageSerializer()
                : new ZLinkJsonMessageSerializer();
        return options.registration().codecs().serializerWithFallback(fallback);
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

    public ZLinkSpotRemoteAddress resolveRegistrySpotRemoteAddress(
        String namespaceName,
        String configuredRouterChannelId,
        RoutingId spotRid) {
        if (spots == null) {
            throw new ZLinkConfigurationException("Spot runtime is not configured");
        }
        return spots.resolveRegistrySpotRemoteAddress(
            namespaceName,
            configuredRouterChannelId,
            spotRid);
    }

    public java.util.Map<String, ZLinkBackendSocket> monitoringSocketSources() {
        return channels.monitoringSocketSources();
    }

    public java.util.Map<String, ZLinkBackendSpotNode> monitoringSpotSources() {
        return spots == null ? java.util.Map.of() : spots.nodesByName();
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

    @Override
    public void close() {
        if (spots != null) {
            spots.beginClose();
        }
        try {
            closeRuntimeComponent(channels::close);
        } finally {
            try {
                if (streams != null) {
                    closeRuntimeComponent(streams::close);
                }
            } finally {
                try {
                    if (actors != null) {
                        closeRuntimeComponent(actors::close);
                    }
                } finally {
                    try {
                        if (spots != null) {
                            closeRuntimeComponent(spots::close);
                        }
                    } finally {
                        closeHandlerExecutor();
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
        if (registration.closeHandlerExecutor()
            && registration.handlerExecutor() instanceof AutoCloseable closeable) {
            try {
                closeable.close();
            } catch (Exception ex) {
                throw new ZLinkConfigurationException("failed to close handler executor", ex);
            }
        }
    }
}
