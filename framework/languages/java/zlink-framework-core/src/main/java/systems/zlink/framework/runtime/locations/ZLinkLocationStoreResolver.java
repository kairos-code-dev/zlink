package systems.zlink.framework.runtime.locations;

import java.util.HashMap;
import java.util.Map;
import systems.zlink.framework.locations.ZLinkActorLocationStore;
import systems.zlink.framework.locations.ZLinkLocationChangeStampStore;
import systems.zlink.framework.locations.ZLinkLocationStore;
import systems.zlink.framework.locations.ZLinkLocationWatchStore;
import systems.zlink.framework.locations.ZLinkOwnerLeaseStore;
import systems.zlink.framework.locations.ZLinkPeerLocationStore;
import systems.zlink.framework.locations.ZLinkRouteLocationStore;
import systems.zlink.framework.locations.ZLinkSpotLocationStore;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerFactory;

public final class ZLinkLocationStoreResolver {
    private ZLinkLocationStoreResolver() {
    }

    public static ZLinkRegisteredLocationStores resolve(
        ZLinkLocationRegistration registration,
        ZLinkHandlerFactory factory) {
        if (registration == null || !registration.enabled()) {
            return null;
        }

        if (registration.storeInstance() != null) {
            return fromUnified(registration.storeInstance());
        }

        if (registration.useInMemoryStores()) {
            return fromUnified(new ZLinkInMemoryLocationStore());
        }

        Map<Class<?>, Object> instances = new HashMap<>();
        ZLinkPeerLocationStore peer = create(
            registration.peerStoreType(), ZLinkPeerLocationStore.class, factory, instances);
        ZLinkSpotLocationStore spot = create(
            registration.spotStoreType(), ZLinkSpotLocationStore.class, factory, instances);
        ZLinkActorLocationStore actor = create(
            registration.actorStoreType(), ZLinkActorLocationStore.class, factory, instances);
        ZLinkRouteLocationStore route = create(
            registration.routeStoreType(), ZLinkRouteLocationStore.class, factory, instances);
        ZLinkOwnerLeaseStore ownerLease = create(
            registration.ownerLeaseStoreType(), ZLinkOwnerLeaseStore.class, factory, instances);

        ZLinkLocationStore unified = firstOfType(instances, ZLinkLocationStore.class);
        ZLinkLocationChangeStampStore stamps = firstOfType(instances, ZLinkLocationChangeStampStore.class);
        ZLinkLocationWatchStore watch = firstOfType(instances, ZLinkLocationWatchStore.class);
        return new ZLinkRegisteredLocationStores(peer, spot, actor, route, ownerLease, stamps, watch, unified);
    }

    private static ZLinkRegisteredLocationStores fromUnified(ZLinkLocationStore store) {
        return ZLinkRegisteredLocationStores.fromUnified(store);
    }

    private static <T> T create(
        Class<? extends T> type,
        Class<T> role,
        ZLinkHandlerFactory factory,
        Map<Class<?>, Object> instances) {
        Object existing = instances.get(type);
        if (existing == null) {
            existing = factory.create(type);
            instances.put(type, existing);
        }
        return role.cast(existing);
    }

    private static <T> T firstOfType(Map<Class<?>, Object> instances, Class<T> type) {
        for (Object instance : instances.values()) {
            if (type.isInstance(instance)) {
                return type.cast(instance);
            }
        }
        return null;
    }
}
