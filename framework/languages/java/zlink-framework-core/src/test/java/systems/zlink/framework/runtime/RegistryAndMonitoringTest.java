package systems.zlink.framework.runtime;

import static org.junit.jupiter.api.Assertions.assertThrows;

import org.junit.jupiter.api.Test;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.monitoring.ZLinkSocketEventKind;
import systems.zlink.framework.runtime.monitoring.DefaultZLinkMonitoringOptions;
import systems.zlink.framework.runtime.monitoring.ZLinkMonitoringRuntime;
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

    @Test
    void addZLinkMonitoring_throws_whenSocketSourceIsUnknownOnStartup() {
        DefaultZLinkMonitoringOptions options = new DefaultZLinkMonitoringOptions();

        options.addSocketEvents("missing-profile", ZLinkSocketEventKind.CONNECTED);

        assertThrows(ZLinkConfigurationException.class, () ->
            new ZLinkMonitoringRuntime(
                options,
                socket -> null,
                java.util.Map.of(),
                new systems.zlink.framework.monitoring.ZLinkRuntimeEventDispatcher()));
    }

    @Test
    void addZLinkMonitoring_throws_whenRegistrySourceIsUnknownOnStartup() {
        DefaultZLinkMonitoringOptions options = new DefaultZLinkMonitoringOptions();

        options.addRegistryEvents("missing-registry", java.time.Duration.ofSeconds(1));

        assertThrows(ZLinkConfigurationException.class, () ->
            new ZLinkMonitoringRuntime(
                options,
                socket -> null,
                java.util.Map.of(),
                java.util.Map.of(),
                java.util.Map.of(),
                new systems.zlink.framework.monitoring.ZLinkRuntimeEventDispatcher()));
    }

    @Test
    void addZLinkMonitoring_throws_whenSpotSourceIsUnknownOnStartup() {
        DefaultZLinkMonitoringOptions options = new DefaultZLinkMonitoringOptions();

        options.addSpotEvents("missing-spot", java.time.Duration.ofSeconds(1));

        assertThrows(ZLinkConfigurationException.class, () ->
            new ZLinkMonitoringRuntime(
                options,
                socket -> null,
                java.util.Map.of(),
                java.util.Map.of(),
                java.util.Map.of(),
                new systems.zlink.framework.monitoring.ZLinkRuntimeEventDispatcher()));
    }
}
