package systems.zlink.framework.runtime.host;

import systems.zlink.framework.runtime.backend.*;

import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.channels.ZLinkFanoutClient;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.actors.ZLinkActorEntrySpotRoutePackets;
import systems.zlink.framework.runtime.actors.ZLinkActorRuntime;
import systems.zlink.framework.runtime.actors.ZLinkSessionActorsRuntime;
import systems.zlink.framework.runtime.channels.ZLinkChannelRuntime;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerFactory;
import systems.zlink.framework.runtime.messaging.ZLinkStringMessageSerializer;
import systems.zlink.framework.runtime.spots.ZLinkSpotRuntime;
import systems.zlink.framework.runtime.streams.ZLinkStreamRuntime;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.framework.spots.ZLinkSpotOutbound;
import systems.zlink.framework.spots.ZLinkSpotPublisherClient;
import systems.zlink.framework.spots.ZLinkSpotRemoteAddress;
import systems.zlink.contracts.core.RoutingId;

public final class ZLinkFrameworkRuntime implements AutoCloseable {
    private final ZLinkChannelRuntime channels;
    private final ZLinkSpotRuntime spots;
    private final ZLinkActorRuntime actors;
    private final ZLinkStreamRuntime streams;

    public ZLinkFrameworkRuntime(
        DefaultZLinkFrameworkOptions options,
        ZLinkBackendAdapterFactory backendFactory,
        ZLinkMessageSerializer serializer) {
        this(options, backendFactory, serializer, ZLinkHandlerFactory.reflection());
    }

    public ZLinkFrameworkRuntime(
        DefaultZLinkFrameworkOptions options,
        ZLinkBackendAdapterFactory backendFactory,
        ZLinkMessageSerializer serializer,
        ZLinkHandlerFactory handlerFactory) {
        options.validate();
        ZLinkBackendAdapterOptions adapterOptions =
            new ZLinkBackendAdapterOptions(options.defaultTimeout());
        this.channels = new ZLinkChannelRuntime(
            backendFactory.createChannelAdapter(adapterOptions),
            backendFactory,
            adapterOptions,
            options.registration(),
            serializer,
            handlerFactory);
        this.spots = options.registration().spotNodes().isEmpty()
            ? null
            : new ZLinkSpotRuntime(
                backendFactory,
                adapterOptions,
                options.registration(),
                channels,
                handlerFactory);
        if (this.spots != null) {
            this.channels.registerSpotRelayIngress(this.spots);
        }
        this.actors = spots != null && !options.registration().actorFactories().isEmpty()
            ? new ZLinkActorRuntime(
                spots.primaryNode(),
                options.registration().actorFactories(),
                options.registration().defaultTimeout(),
                serializer)
            : null;
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
                handlerFactory);
    }

    public static ZLinkFrameworkRuntime start(
        DefaultZLinkFrameworkOptions options,
        ZLinkBackendAdapterFactory backendFactory) {
        return new ZLinkFrameworkRuntime(options, backendFactory, new ZLinkStringMessageSerializer());
    }

    public static ZLinkFrameworkRuntime start(
        DefaultZLinkFrameworkOptions options,
        ZLinkBackendAdapterFactory backendFactory,
        ZLinkHandlerFactory handlerFactory) {
        return new ZLinkFrameworkRuntime(
            options,
            backendFactory,
            new ZLinkStringMessageSerializer(),
            handlerFactory);
    }

    public ZLinkClient client() {
        return channels;
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
        if (actors == null) {
            throw new ZLinkConfigurationException("Actor runtime is not configured");
        }
        return streams.sessionActors(streamNodeName, sessionRid, actors);
    }

    @Override
    public void close() {
        try {
            channels.close();
        } finally {
            try {
                if (streams != null) {
                    streams.close();
                }
            } finally {
                if (spots != null) {
                    spots.close();
                }
            }
        }
    }
}
