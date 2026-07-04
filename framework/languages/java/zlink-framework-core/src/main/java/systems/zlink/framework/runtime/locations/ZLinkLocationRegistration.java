package systems.zlink.framework.runtime.locations;

import systems.zlink.framework.locations.ZLinkLocationOptions;
import systems.zlink.framework.locations.ZLinkLocationStore;

public final class ZLinkLocationRegistration {
    private final ZLinkLocationOptions options = new ZLinkLocationOptions();
    private boolean useInMemoryStores;
    private ZLinkLocationStore storeInstance;

    public ZLinkLocationOptions options() {
        return options;
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

    public boolean enabled() {
        return useInMemoryStores || storeInstance != null;
    }
}
