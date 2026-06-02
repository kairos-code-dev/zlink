package systems.zlink.framework.runtime.host;

import systems.zlink.framework.runtime.backend.*;

import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.channels.ZLinkFanoutClient;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.actors.ZLinkActorRuntime;
import systems.zlink.framework.runtime.actors.ZLinkSessionActorsRuntime;
import systems.zlink.framework.runtime.channels.ZLinkChannelRuntime;
import systems.zlink.framework.runtime.messaging.ZLinkStringMessageSerializer;
import systems.zlink.framework.runtime.spots.ZLinkSpotRuntime;
import systems.zlink.framework.runtime.streams.ZLinkStreamRuntime;
import systems.zlink.framework.spots.ZLinkSpotManager;
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
        options.validate();
        ZLinkBackendAdapterOptions adapterOptions =
            new ZLinkBackendAdapterOptions(options.defaultTimeout());
        this.channels = new ZLinkChannelRuntime(
            backendFactory.createChannelAdapter(adapterOptions),
            options.registration(),
            serializer);
        this.spots = options.registration().spotNodes().isEmpty()
            ? null
            : new ZLinkSpotRuntime(
                backendFactory,
                adapterOptions,
                options.registration());
        this.actors = spots != null && !options.registration().actorFactories().isEmpty()
            ? new ZLinkActorRuntime(
                spots.primaryNode(),
                options.registration().actorFactories())
            : null;
        this.streams = options.registration().streamNodes().isEmpty()
            ? null
            : new ZLinkStreamRuntime(
                backendFactory,
                adapterOptions,
                options.registration(),
                spots == null ? java.util.Map.of() : spots.nodesByName(),
                serializer);
    }

    public static ZLinkFrameworkRuntime start(
        DefaultZLinkFrameworkOptions options,
        ZLinkBackendAdapterFactory backendFactory) {
        return new ZLinkFrameworkRuntime(options, backendFactory, new ZLinkStringMessageSerializer());
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
