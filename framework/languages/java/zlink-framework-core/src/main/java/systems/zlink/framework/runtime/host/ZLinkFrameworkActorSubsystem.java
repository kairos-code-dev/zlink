package systems.zlink.framework.runtime.host;

import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.actors.ZLinkActorClient;
import systems.zlink.framework.actors.ZLinkActorDirectory;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.monitoring.ZLinkRuntimeEventDispatcher;
import systems.zlink.framework.runtime.actors.ZLinkActorClientRuntime;
import systems.zlink.framework.runtime.actors.ZLinkActorEntrySpotRoutePackets;
import systems.zlink.framework.runtime.actors.ZLinkActorEntryTransferEnvelope;
import systems.zlink.framework.runtime.actors.ZLinkActorRuntime;
import systems.zlink.framework.runtime.actors.ZLinkStoreActorDirectory;
import systems.zlink.framework.runtime.channels.ZLinkChannelRuntime;
import systems.zlink.framework.runtime.configuration.ZLinkFrameworkRegistration;
import systems.zlink.framework.runtime.diagnostics.ZLinkMessageFlowTracer;
import systems.zlink.framework.runtime.internal.handlers.ZLinkHandlerActivator;
import systems.zlink.framework.runtime.locations.ZLinkLocationLifecycle;
import systems.zlink.framework.runtime.locations.ZLinkStoreLocationResolvers;
import systems.zlink.framework.runtime.spots.ZLinkSpotRuntime;
import systems.zlink.framework.runtime.internal.spots.SpotTransportAddressResolver;
import systems.zlink.framework.streams.ZLinkStreamCodec;

final class ZLinkFrameworkActorSubsystem {
    private final ZLinkActorRuntime actors;
    private final ZLinkActorDirectory actorDirectory;
    private final ZLinkActorClient actorClient;

    private ZLinkFrameworkActorSubsystem(
        ZLinkActorRuntime actors,
        ZLinkActorDirectory actorDirectory,
        ZLinkActorClient actorClient) {
        this.actors = actors;
        this.actorDirectory = actorDirectory;
        this.actorClient = actorClient;
    }

    static ZLinkFrameworkActorSubsystem create(
        ZLinkFrameworkRegistration registration,
        ZLinkMessageSerializer serializer,
        ZLinkHandlerActivator.MutableServices runtimeHandlers,
        ZLinkHandlerActivator handlerFactory,
        ZLinkRuntimeEventDispatcher eventDispatcher,
        ZLinkStreamCodec defaultStreamCodec,
        ZLinkChannelRuntime channels,
        ZLinkSpotRuntime spots,
        ZLinkLocationLifecycle locationLifecycle,
        ZLinkStoreLocationResolvers storeLocationResolvers,
        SpotTransportAddressResolver remoteAddressResolver) {
        var actorNodeRegistration = registration.spotNodes().stream()
            .filter(node -> !node.actorFactories().isEmpty())
            .findFirst()
            .orElse(null);
        ZLinkActorRuntime actors = spots != null && actorNodeRegistration != null
            ? new ZLinkActorRuntime(
                spots.node(actorNodeRegistration.nodeName()),
                actorNodeRegistration.actorFactories(),
                actorNodeRegistration.actorTransferAdapters(),
                registration.defaultRequestTimeout(),
                registration.actorTransferForwardWindow(),
                serializer,
                runtimeHandlers,
                defaultStreamCodec)
            : null;
        ZLinkActorDirectory actorDirectory = actors != null
            ? actors
            : spots != null && storeLocationResolvers != null
                ? new ZLinkStoreActorDirectory(storeLocationResolvers)
                : null;
        ZLinkActorClient actorClient = spots != null && storeLocationResolvers != null
            ? new ZLinkActorClientRuntime(
                spots::primaryNode,
                storeLocationResolvers,
                serializer,
                registration.defaultRequestTimeout())
            : null;
        registerActorServices(runtimeHandlers, actorClient, actorDirectory, actors);
        wireActorRuntime(
            registration,
            handlerFactory,
            eventDispatcher,
            channels,
            spots,
            locationLifecycle,
            storeLocationResolvers,
            remoteAddressResolver,
            actors);
        return new ZLinkFrameworkActorSubsystem(actors, actorDirectory, actorClient);
    }

    ZLinkActorRuntime actors() {
        return actors;
    }

    ZLinkActorDirectory actorDirectory() {
        return actorDirectory;
    }

    ZLinkActorClient actorClient() {
        return actorClient;
    }

    private static void registerActorServices(
        ZLinkHandlerActivator.MutableServices runtimeHandlers,
        ZLinkActorClient actorClient,
        ZLinkActorDirectory actorDirectory,
        ZLinkActorRuntime actors) {
        if (actorClient != null) {
            runtimeHandlers.add(ZLinkActorClient.class, actorClient);
        }
        if (actorDirectory != null) {
            runtimeHandlers.add(ZLinkActorDirectory.class, actorDirectory);
        }
        if (actors != null) {
            runtimeHandlers.add(ZLinkActorManager.class, actors);
        }
    }

    private static void wireActorRuntime(
        ZLinkFrameworkRegistration registration,
        ZLinkHandlerActivator handlerFactory,
        ZLinkRuntimeEventDispatcher eventDispatcher,
        ZLinkChannelRuntime channels,
        ZLinkSpotRuntime spots,
        ZLinkLocationLifecycle locationLifecycle,
        ZLinkStoreLocationResolvers storeLocationResolvers,
        SpotTransportAddressResolver remoteAddressResolver,
        ZLinkActorRuntime actors) {
        if (actors == null) {
            return;
        }

        actors.setLocationLifecycle(locationLifecycle);
        actors.setStoreLocationResolvers(storeLocationResolvers);
        actors.setMessageFlowTracer(new ZLinkMessageFlowTracer(
            registration.dispatchOptions(),
            handlerFactory,
            registration.handlerExecutor(),
            eventDispatcher));
        actors.setRoutedTransport(channels, () -> spots.primaryNode().entrySpot().routingId());
        if (remoteAddressResolver != null) {
            actors.setRemoteAddressResolver(remoteAddressResolver);
        }
        spots.attachActorRuntime(actors);
        channels.registerRouteInternalRequestHandler(
            ZLinkActorEntrySpotRoutePackets.JOIN_ENTRY_SPOT_PACKET_NAME,
            actors::handleEntrySpotRouteJoin);
        channels.registerRouteInternalRequestHandler(
            ZLinkActorEntryTransferEnvelope.PACKET_NAME,
            spots::handleEntryActorTransferRoute);
    }
}
