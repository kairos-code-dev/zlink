package systems.zlink.framework.runtime.host;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.locations.ZLinkLocationRuntimeQuery;
import systems.zlink.framework.runtime.configuration.ZLinkFrameworkRegistration;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerFactory;
import systems.zlink.framework.runtime.locations.ZLinkLiveLocationRows;
import systems.zlink.framework.runtime.locations.ZLinkLocationAutoConnectHost;
import systems.zlink.framework.runtime.locations.ZLinkLocationLifecycle;
import systems.zlink.framework.runtime.locations.ZLinkLocationRuntime;
import systems.zlink.framework.runtime.locations.ZLinkLocationRuntimeQueryService;
import systems.zlink.framework.runtime.locations.ZLinkLocationSpotRemoteRefResolver;
import systems.zlink.framework.runtime.locations.ZLinkLocationStoreResolver;
import systems.zlink.framework.runtime.locations.ZLinkRegisteredLocationStores;
import systems.zlink.framework.runtime.locations.ZLinkStoreLocationResolvers;
import systems.zlink.framework.spots.SpotRemoteRefResolver;

final class ZLinkFrameworkLocationSubsystem {
    private final ZLinkRegisteredLocationStores locationStores;
    private final ZLinkLocationRuntime locationRuntime;
    private final ZLinkLocationRuntimeQuery locationRuntimeQuery;
    private final ZLinkLocationLifecycle locationLifecycle;
    private final ZLinkLocationAutoConnectHost locationAutoConnectHost;
    private final SpotRemoteRefResolver locationSpotRemoteRefResolver;
    private final ZLinkStoreLocationResolvers storeLocationResolvers;

    private ZLinkFrameworkLocationSubsystem(
        ZLinkRegisteredLocationStores locationStores,
        ZLinkLocationRuntime locationRuntime,
        ZLinkLocationRuntimeQuery locationRuntimeQuery,
        ZLinkLocationLifecycle locationLifecycle,
        ZLinkLocationAutoConnectHost locationAutoConnectHost,
        SpotRemoteRefResolver locationSpotRemoteRefResolver,
        ZLinkStoreLocationResolvers storeLocationResolvers) {
        this.locationStores = locationStores;
        this.locationRuntime = locationRuntime;
        this.locationRuntimeQuery = locationRuntimeQuery;
        this.locationLifecycle = locationLifecycle;
        this.locationAutoConnectHost = locationAutoConnectHost;
        this.locationSpotRemoteRefResolver = locationSpotRemoteRefResolver;
        this.storeLocationResolvers = storeLocationResolvers;
    }

    static ZLinkFrameworkLocationSubsystem create(
        ZLinkFrameworkRegistration registration,
        ZLinkHandlerFactory.MutableServices runtimeHandlers) {
        ZLinkRegisteredLocationStores locationStores = ZLinkLocationStoreResolver.resolve(
            registration.locations(),
            runtimeHandlers);
        if (locationStores == null) {
            return disabled();
        }

        locationStores.addTo(runtimeHandlers);
        ZLinkLocationRuntime locationRuntime = new ZLinkLocationRuntime(
            locationStores,
            registration.locations().options().ownerLeaseTtl(),
            registration.locations().options().heartbeatInterval());
        locationRuntime.startAsync(RoutingId.from(locationRuntime.ownerId()))
            .toCompletableFuture()
            .join();

        ZLinkLiveLocationRows liveLocationRows = ZLinkLiveLocationRows.create(
            locationStores,
            registration.locations().options());
        ZLinkLocationRuntimeQuery locationRuntimeQuery = new ZLinkLocationRuntimeQueryService(
            locationStores,
            locationRuntime,
            registration.locations().options(),
            liveLocationRows);
        ZLinkLocationLifecycle locationLifecycle = new ZLinkLocationLifecycle(locationRuntime);
        ZLinkStoreLocationResolvers storeLocationResolvers =
            new ZLinkStoreLocationResolvers(locationStores, liveLocationRows);
        ZLinkLocationAutoConnectHost locationAutoConnectHost = new ZLinkLocationAutoConnectHost(
            locationRuntime,
            storeLocationResolvers,
            registration.locations().options());
        ZLinkStoreLocationResolvers.AddressResolvers locationAddressResolvers =
            new ZLinkStoreLocationResolvers.AddressResolvers(
                spotMeshNames(registration),
                registration.locations().options().spotRouterChannels(),
                storeLocationResolvers);
        SpotRemoteRefResolver locationSpotRemoteRefResolver =
            new ZLinkLocationSpotRemoteRefResolver(locationAddressResolvers);

        runtimeHandlers.add(ZLinkLocationRuntime.class, locationRuntime);
        runtimeHandlers.add(ZLinkLocationRuntimeQuery.class, locationRuntimeQuery);
        runtimeHandlers.add(ZLinkLocationLifecycle.class, locationLifecycle);
        runtimeHandlers.add(ZLinkLocationAutoConnectHost.class, locationAutoConnectHost);
        runtimeHandlers.add(ZLinkStoreLocationResolvers.class, storeLocationResolvers);
        runtimeHandlers.add(ZLinkStoreLocationResolvers.AddressResolvers.class, locationAddressResolvers);
        runtimeHandlers.add(ZLinkLocationSpotRemoteRefResolver.class, locationSpotRemoteRefResolver);

        return new ZLinkFrameworkLocationSubsystem(
            locationStores,
            locationRuntime,
            locationRuntimeQuery,
            locationLifecycle,
            locationAutoConnectHost,
            locationSpotRemoteRefResolver,
            storeLocationResolvers);
    }

    boolean enabled() {
        return locationStores != null;
    }

    ZLinkRegisteredLocationStores locationStores() {
        return locationStores;
    }

    ZLinkLocationRuntime locationRuntime() {
        return locationRuntime;
    }

    ZLinkLocationRuntimeQuery locationRuntimeQuery() {
        return locationRuntimeQuery;
    }

    ZLinkLocationLifecycle locationLifecycle() {
        return locationLifecycle;
    }

    ZLinkLocationAutoConnectHost locationAutoConnectHost() {
        return locationAutoConnectHost;
    }

    SpotRemoteRefResolver locationSpotRemoteRefResolver() {
        return locationSpotRemoteRefResolver;
    }

    ZLinkStoreLocationResolvers storeLocationResolvers() {
        return storeLocationResolvers;
    }

    private static ZLinkFrameworkLocationSubsystem disabled() {
        return new ZLinkFrameworkLocationSubsystem(null, null, null, null, null, null, null);
    }

    private static java.util.List<String> spotMeshNames(ZLinkFrameworkRegistration registration) {
        return registration.spotNodes().stream()
            .map(systems.zlink.framework.runtime.spots.SpotNodeRegistration::meshName)
            .distinct()
            .toList();
    }
}
