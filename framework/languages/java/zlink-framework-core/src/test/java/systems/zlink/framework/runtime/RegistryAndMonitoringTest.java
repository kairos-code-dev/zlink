package systems.zlink.framework.runtime;

import static org.junit.jupiter.api.Assertions.assertThrows;

import org.junit.jupiter.api.Test;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions;

final class RegistryAndMonitoringTest {
    @Test
    void addZLinkRegistry_throws_whenPubEndpointIsMissing() {
        ZLinkEmbeddedRegistryOptions options = new ZLinkEmbeddedRegistryOptions();

        options.setRouterEndpoint("inproc://registry-router");

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void addZLinkRegistry_throws_whenRouterEndpointIsMissing() {
        ZLinkEmbeddedRegistryOptions options = new ZLinkEmbeddedRegistryOptions();

        options.setPubEndpoint("inproc://registry-pub");

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }
}
