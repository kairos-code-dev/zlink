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
        systems.zlink.framework.runtime.internal.backend
            .ZLinkBackendAdapterProvider backendFactory,
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
        SpotTransportAddressResolver remoteAddressResolver,
        systems.zlink.framework.locations.ZLinkLocationStore locationStore,
        java.util.Map<String,
            systems.zlink.framework.runtime.internal.backend
                .ZLinkInternalMeshNode> meshNodes) {
        var legacyActorNode = registration.spotNodes().stream()
            .filter(node -> !node.actorFactories().isEmpty())
            .findFirst()
            .orElse(null);
        var meshActorNode = registration.meshNodes().stream()
            .filter(node -> !node.actorFactories().isEmpty())
            .findFirst()
            .orElse(null);
        String actorNodeName = legacyActorNode != null
            ? legacyActorNode.nodeName()
            : meshActorNode == null ? null : meshActorNode.meshName();
        var actorFactories = legacyActorNode != null
            ? legacyActorNode.actorFactories()
            : meshActorNode == null ? java.util.Map.<String,
                Class<? extends systems.zlink.framework.actors.ZLinkActorFactory>>of()
                : meshActorNode.actorFactories();
        var transferAdapters = legacyActorNode != null
            ? legacyActorNode.actorTransferAdapters()
            : meshActorNode == null ? java.util.Map.<String,
                Class<? extends systems.zlink.framework.actors.ZLinkActorTransferAdapter<?>>>of()
                : meshActorNode.actorTransferAdapters();
        ZLinkActorRuntime actors = spots != null && actorNodeName != null
            ? new ZLinkActorRuntime(
                spots.node(actorNodeName),
                actorFactories,
                transferAdapters,
                registration.defaultRequestTimeout(),
                registration.actorTransferForwardWindow(),
                serializer,
                runtimeHandlers,
                defaultStreamCodec,
                ZLinkAdmissionRuntime.factory(backendFactory))
            : null;
        ZLinkActorDirectory actorDirectory = actors != null
            ? actors
            : spots != null && storeLocationResolvers != null
                ? new ZLinkStoreActorDirectory(storeLocationResolvers)
                : null;
        if (actors != null) {
            actors.setMeshName(actorNodeName);
            actors.setMetadataPolicy(
                registration.metadataPolicy().sessionToActorKeys(),
                registration.metadataPolicy().actorToSessionKeys());
            if (meshActorNode != null
                && !meshActorNode.relocatableActorFactories().isEmpty()
                && locationStore != null) {
                var meshNode = meshNodes.get(meshActorNode.meshName());
                if (meshNode != null) {
                    var creation =
                        new systems.zlink.framework.runtime.actors
                            .ZLinkActorCreationCoordinator(
                                meshActorNode.meshName(),
                                meshNode,
                                locationStore,
                                actors,
                                serializer,
                                registration.defaultRequestTimeout());
                    meshNode.setActorCreateOperationHandler(creation);
                    actors.setCreationSubmitter(creation);
                    actors.setEntrySpotTargetSelector(
                        creation::selectEntrySpotTarget);
                }
            }
        }
        ZLinkActorClient actorClient = spots != null && storeLocationResolvers != null
            ? new ZLinkActorClientRuntime(
                spots::primaryNode,
                storeLocationResolvers,
                serializer,
                registration.defaultRequestTimeout(),
                ZLinkAdmissionRuntime.factory(backendFactory))
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
