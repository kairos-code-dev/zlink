package systems.zlink.framework.runtime.configuration;

import systems.zlink.framework.configuration.ZLinkRegistrySpotRemoteAddressesOptions;
import systems.zlink.framework.errors.ZLinkConfigurationException;

public final class ZLinkRegistrySpotRemoteAddressesRegistration
    implements ZLinkRegistrySpotRemoteAddressesOptions {
    private final String namespaceName;
    private String routerChannelId;

    public ZLinkRegistrySpotRemoteAddressesRegistration(String namespaceName) {
        if (namespaceName == null || namespaceName.isBlank()) {
            throw new ZLinkConfigurationException("namespaceName name is required");
        }
        this.namespaceName = namespaceName;
    }

    public String namespaceName() {
        return namespaceName;
    }

    public String routerChannelId() {
        return routerChannelId;
    }

    @Override
    public void setRouterChannelId(String routerChannelId) {
        if (routerChannelId == null || routerChannelId.isBlank()) {
            throw new ZLinkConfigurationException("routerChannelId name is required");
        }
        this.routerChannelId = routerChannelId;
    }
}
