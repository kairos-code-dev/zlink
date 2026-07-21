package systems.zlink.framework.runtime.host;

import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.monitoring.ZLinkRuntimeEventDispatcher;
import systems.zlink.framework.runtime.actors.ZLinkActorRuntime;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendAdapterProvider;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterOptions;
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.internal.handlers.ZLinkHandlerActivator;
import systems.zlink.framework.runtime.spots.ZLinkSpotRuntime;
import systems.zlink.framework.runtime.streams.ZLinkStreamRuntime;

final class ZLinkFrameworkStreamSubsystem {
    private final ZLinkStreamRuntime streams;

    private ZLinkFrameworkStreamSubsystem(ZLinkStreamRuntime streams) {
        this.streams = streams;
    }

    static ZLinkFrameworkStreamSubsystem create(
        DefaultZLinkFrameworkOptions options,
        ZLinkBackendAdapterProvider backendFactory,
        ZLinkBackendAdapterOptions adapterOptions,
        ZLinkMessageSerializer serializer,
        ZLinkHandlerActivator.MutableServices runtimeHandlers,
        ZLinkRuntimeEventDispatcher eventDispatcher,
        systems.zlink.framework.runtime.mesh.ZLinkMeshNodesRuntime meshNodes,
        systems.zlink.framework.runtime.backend.ZLinkBackendContext backendContext,
        ZLinkSpotRuntime spots,
        ZLinkActorRuntime actors,
        systems.zlink.framework.actors.ZLinkActorDirectory actorDirectory) {
        ZLinkStreamRuntime streams = options.registration().streamNodes().isEmpty()
            ? null
            : new ZLinkStreamRuntime(
                backendFactory,
                adapterOptions,
                options.registration(),
                spots == null ? java.util.Map.of() : spots.nodesByName(),
                meshNodes.nodesByName(),
                serializer,
                actors,
                runtimeHandlers,
                spots == null ? ignored -> true : spots::isSessionRelayRouteReady,
                spots,
                eventDispatcher,
                backendContext,
                false);
        if (streams != null) {
            streams.setActorDirectory(actorDirectory);
        }
        return new ZLinkFrameworkStreamSubsystem(streams);
    }

    ZLinkStreamRuntime streams() {
        return streams;
    }
}
