package systems.zlink.framework.runtime.locations;

import java.util.Objects;
import systems.zlink.framework.locations.ZLinkLocationStore;
import systems.zlink.framework.runtime.internal.handlers.ZLinkHandlerActivator;

/** Internal holder for the single complete Location Store capability. */
public final class ZLinkRegisteredLocationStores {
    private final ZLinkLocationStore store;

    private ZLinkRegisteredLocationStores(ZLinkLocationStore store) {
        this.store = Objects.requireNonNull(store, "store");
    }

    public static ZLinkRegisteredLocationStores fromUnified(
        ZLinkLocationStore store) {
        return new ZLinkRegisteredLocationStores(store);
    }

    public void addTo(ZLinkHandlerActivator.MutableServices services) {
        services.add(ZLinkLocationStore.class, store);
    }

    public ZLinkLocationStore unifiedStore() { return store; }
    public ZLinkLocationStore peerStore() { return store; }
    public ZLinkLocationStore spotStore() { return store; }
    public ZLinkLocationStore actorStore() { return store; }
    public ZLinkLocationStore routeStore() { return store; }
    public ZLinkLocationStore ownerLeaseStore() { return store; }
    public ZLinkLocationStore authorityStore() { return store; }
    public ZLinkLocationStore clientServerStore() { return store; }
    public ZLinkLocationStore fanoutStore() { return store; }
}
