package systems.zlink.framework.runtime.locations;

import systems.zlink.framework.locations.ZLinkActorLocationStore;
import systems.zlink.framework.locations.ZLinkAuthorityStore;
import systems.zlink.framework.locations.ZLinkClientServerLocationStore;
import systems.zlink.framework.locations.ZLinkLocationChangeStampStore;
import systems.zlink.framework.locations.ZLinkLocationStore;
import systems.zlink.framework.locations.ZLinkLocationWatchStore;
import systems.zlink.framework.locations.ZLinkOwnerLeaseStore;
import systems.zlink.framework.locations.ZLinkPeerLocationStore;
import systems.zlink.framework.locations.ZLinkRouteLocationStore;
import systems.zlink.framework.locations.ZLinkSpotLocationStore;
import systems.zlink.framework.runtime.internal.handlers.ZLinkHandlerActivator;

public record ZLinkRegisteredLocationStores(
    ZLinkPeerLocationStore peerStore,
    ZLinkSpotLocationStore spotStore,
    ZLinkActorLocationStore actorStore,
    ZLinkRouteLocationStore routeStore,
    ZLinkOwnerLeaseStore ownerLeaseStore,
    ZLinkAuthorityStore authorityStore,
    ZLinkLocationChangeStampStore changeStampStore,
    ZLinkLocationWatchStore watchStore,
    ZLinkLocationStore unifiedStore) {

    public static ZLinkRegisteredLocationStores fromUnified(ZLinkLocationStore store) {
        return new ZLinkRegisteredLocationStores(
            store,
            store,
            store,
            store,
            store,
            store,
            store instanceof ZLinkLocationChangeStampStore stamps ? stamps : null,
            store instanceof ZLinkLocationWatchStore watch ? watch : null,
            store);
    }

    public void addTo(ZLinkHandlerActivator.MutableServices services) {
        services.add(ZLinkPeerLocationStore.class, peerStore);
        services.add(ZLinkSpotLocationStore.class, spotStore);
        services.add(ZLinkActorLocationStore.class, actorStore);
        services.add(ZLinkRouteLocationStore.class, routeStore);
        services.add(ZLinkOwnerLeaseStore.class, ownerLeaseStore);
        if (authorityStore != null) {
            services.add(ZLinkAuthorityStore.class, authorityStore);
        }
        if (changeStampStore != null) {
            services.add(ZLinkLocationChangeStampStore.class, changeStampStore);
        }
        if (watchStore != null) {
            services.add(ZLinkLocationWatchStore.class, watchStore);
        }
        if (unifiedStore != null) {
            services.add(ZLinkLocationStore.class, unifiedStore);
        }
        if (clientServerStore() != null) {
            services.add(
                ZLinkClientServerLocationStore.class,
                clientServerStore());
        }
    }

    public ZLinkClientServerLocationStore clientServerStore() {
        return unifiedStore
            instanceof ZLinkClientServerLocationStore clientServer
                ? clientServer
                : null;
    }
}
