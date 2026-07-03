package systems.zlink.framework.runtime.locations;

import systems.zlink.framework.locations.ZLinkActorLocationStore;
import systems.zlink.framework.locations.ZLinkLocationOptions;
import systems.zlink.framework.locations.ZLinkLocationStore;
import systems.zlink.framework.locations.ZLinkOwnerLeaseStore;
import systems.zlink.framework.locations.ZLinkPeerLocationStore;
import systems.zlink.framework.locations.ZLinkRouteLocationStore;
import systems.zlink.framework.locations.ZLinkSpotLocationStore;

public final class ZLinkLocationRegistration {
    private final ZLinkLocationOptions options = new ZLinkLocationOptions();
    private Class<? extends ZLinkPeerLocationStore> peerStoreType;
    private Class<? extends ZLinkSpotLocationStore> spotStoreType;
    private Class<? extends ZLinkActorLocationStore> actorStoreType;
    private Class<? extends ZLinkRouteLocationStore> routeStoreType;
    private Class<? extends ZLinkOwnerLeaseStore> ownerLeaseStoreType;
    private boolean useInMemoryStores;
    private ZLinkLocationStore storeInstance;

    public ZLinkLocationOptions options() {
        return options;
    }

    public Class<? extends ZLinkPeerLocationStore> peerStoreType() {
        return peerStoreType;
    }

    public void setPeerStoreType(Class<? extends ZLinkPeerLocationStore> peerStoreType) {
        this.peerStoreType = peerStoreType;
    }

    public Class<? extends ZLinkSpotLocationStore> spotStoreType() {
        return spotStoreType;
    }

    public void setSpotStoreType(Class<? extends ZLinkSpotLocationStore> spotStoreType) {
        this.spotStoreType = spotStoreType;
    }

    public Class<? extends ZLinkActorLocationStore> actorStoreType() {
        return actorStoreType;
    }

    public void setActorStoreType(Class<? extends ZLinkActorLocationStore> actorStoreType) {
        this.actorStoreType = actorStoreType;
    }

    public Class<? extends ZLinkRouteLocationStore> routeStoreType() {
        return routeStoreType;
    }

    public void setRouteStoreType(Class<? extends ZLinkRouteLocationStore> routeStoreType) {
        this.routeStoreType = routeStoreType;
    }

    public Class<? extends ZLinkOwnerLeaseStore> ownerLeaseStoreType() {
        return ownerLeaseStoreType;
    }

    public void setOwnerLeaseStoreType(Class<? extends ZLinkOwnerLeaseStore> ownerLeaseStoreType) {
        this.ownerLeaseStoreType = ownerLeaseStoreType;
    }

    public boolean useInMemoryStores() {
        return useInMemoryStores;
    }

    public void enableInMemoryStores() {
        this.useInMemoryStores = true;
    }

    public ZLinkLocationStore storeInstance() {
        return storeInstance;
    }

    public void setStoreInstance(ZLinkLocationStore storeInstance) {
        this.storeInstance = storeInstance;
    }

    public boolean hasAnyStoreType() {
        return peerStoreType != null
            || spotStoreType != null
            || actorStoreType != null
            || routeStoreType != null
            || ownerLeaseStoreType != null;
    }

    public boolean hasAllStoreTypes() {
        return peerStoreType != null
            && spotStoreType != null
            && actorStoreType != null
            && routeStoreType != null
            && ownerLeaseStoreType != null;
    }

    public boolean enabled() {
        return useInMemoryStores || hasAnyStoreType() || storeInstance != null;
    }
}
