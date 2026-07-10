package systems.zlink.framework.runtime.host;

import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.monitoring.ZLinkRuntimeEventDispatcher;
import systems.zlink.framework.runtime.actors.ZLinkActorRuntime;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterFactory;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterOptions;
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerFactory;
import systems.zlink.framework.runtime.spots.ZLinkSpotRuntime;
import systems.zlink.framework.runtime.streams.ZLinkStreamRuntime;

final class ZLinkFrameworkStreamSubsystem {
    private final ZLinkStreamRuntime streams;

    private ZLinkFrameworkStreamSubsystem(ZLinkStreamRuntime streams) {
        this.streams = streams;
    }

    static ZLinkFrameworkStreamSubsystem create(
        DefaultZLinkFrameworkOptions options,
        ZLinkBackendAdapterFactory backendFactory,
        ZLinkBackendAdapterOptions adapterOptions,
        ZLinkMessageSerializer serializer,
        ZLinkHandlerFactory.MutableServices runtimeHandlers,
        ZLinkRuntimeEventDispatcher eventDispatcher,
        ZLinkSpotRuntime spots,
        ZLinkActorRuntime actors) {
        ZLinkStreamRuntime streams = options.registration().streamNodes().isEmpty()
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
                spots,
                eventDispatcher);
        return new ZLinkFrameworkStreamSubsystem(streams);
    }

    ZLinkStreamRuntime streams() {
        return streams;
    }
}
