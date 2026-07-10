package systems.zlink.framework.runtime.host;

import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.monitoring.ZLinkRuntimeEventDispatcher;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterFactory;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterOptions;
import systems.zlink.framework.runtime.backend.ZLinkBackendContext;
import systems.zlink.framework.runtime.channels.ZLinkChannelRuntime;
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerFactory;
import systems.zlink.framework.runtime.locations.ZLinkLocationLifecycle;
import systems.zlink.framework.runtime.spots.ZLinkSpotRuntime;
import systems.zlink.framework.spots.SpotRemoteRefResolver;
import systems.zlink.framework.spots.ZLinkSpotManager;

final class ZLinkFrameworkSpotSubsystem {
    private final ZLinkSpotRuntime spots;
    private final SpotRemoteRefResolver remoteAddressResolver;

    private ZLinkFrameworkSpotSubsystem(
        ZLinkSpotRuntime spots,
        SpotRemoteRefResolver remoteAddressResolver) {
        this.spots = spots;
        this.remoteAddressResolver = remoteAddressResolver;
    }

    static ZLinkFrameworkSpotSubsystem create(
        DefaultZLinkFrameworkOptions options,
        ZLinkBackendAdapterFactory backendFactory,
        ZLinkBackendAdapterOptions adapterOptions,
        ZLinkMessageSerializer serializer,
        ZLinkHandlerFactory.MutableServices runtimeHandlers,
        ZLinkRuntimeEventDispatcher eventDispatcher,
        ZLinkChannelRuntime channels,
        ZLinkBackendContext backendContext,
        ZLinkLocationLifecycle locationLifecycle,
        SpotRemoteRefResolver locationSpotRemoteRefResolver) {
        SpotRemoteRefResolver remoteAddressResolver = remoteAddressResolver(
            options,
            runtimeHandlers,
            locationSpotRemoteRefResolver);
        if (options.registration().spotNodes().isEmpty()) {
            return new ZLinkFrameworkSpotSubsystem(null, remoteAddressResolver);
        }

        ZLinkSpotRuntime spots = new ZLinkSpotRuntime(
            backendFactory,
            adapterOptions,
            options.registration(),
            channels,
            backendContext,
            serializer,
            runtimeHandlers,
            eventDispatcher);
        spots.setLocationLifecycle(locationLifecycle);
        spots.claimEntrySpotLocationsAsync()
            .toCompletableFuture()
            .join();
        runtimeHandlers.add(ZLinkSpotManager.class, spots);
        channels.registerSpotRouteBridgeOwner(spots::primaryNode);
        channels.registerSpotRouteBridgeDispatchDrainer(spots::drainRoutedDispatchQueues);
        return new ZLinkFrameworkSpotSubsystem(spots, remoteAddressResolver);
    }

    ZLinkSpotRuntime spots() {
        return spots;
    }

    SpotRemoteRefResolver remoteAddressResolver() {
        return remoteAddressResolver;
    }

    private static SpotRemoteRefResolver remoteAddressResolver(
        DefaultZLinkFrameworkOptions options,
        ZLinkHandlerFactory.MutableServices runtimeHandlers,
        SpotRemoteRefResolver locationSpotRemoteRefResolver) {
        Class<? extends SpotRemoteRefResolver> resolverType =
            options.registration().spotRemoteRefResolverType();
        return resolverType == null
            ? locationSpotRemoteRefResolver
            : (SpotRemoteRefResolver) runtimeHandlers.create(resolverType);
    }
}
