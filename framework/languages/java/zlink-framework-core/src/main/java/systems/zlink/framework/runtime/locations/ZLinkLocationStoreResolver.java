package systems.zlink.framework.runtime.locations;

import systems.zlink.framework.locations.ZLinkLocationStore;
import systems.zlink.framework.runtime.internal.handlers.ZLinkHandlerActivator;

public final class ZLinkLocationStoreResolver {
    private ZLinkLocationStoreResolver() {
    }

    public static ZLinkRegisteredLocationStores resolve(
        ZLinkLocationRegistration registration,
        ZLinkHandlerActivator factory) {
        if (registration == null || !registration.enabled()) {
            return null;
        }

        return fromUnified(registration.storeInstance());
    }

    private static ZLinkRegisteredLocationStores fromUnified(ZLinkLocationStore store) {
        return ZLinkRegisteredLocationStores.fromUnified(store);
    }

}
