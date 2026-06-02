package systems.zlink.framework.spring;

import systems.zlink.framework.errors.ZLinkConfigurationException;

final class DefaultZLinkRegistryQueryClientOptions implements ZLinkRegistryQueryClientOptions {
    private String endpoint;

    @Override
    public String endpoint() {
        return endpoint;
    }

    @Override
    public void setEndpoint(String endpoint) {
        this.endpoint = endpoint;
    }

    void validate() {
        if (endpoint == null || endpoint.isBlank()) {
            throw new ZLinkConfigurationException(
                "registry query client endpoint is required");
        }
    }
}
