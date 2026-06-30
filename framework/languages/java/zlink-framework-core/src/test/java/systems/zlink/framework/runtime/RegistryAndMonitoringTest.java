package systems.zlink.framework.runtime;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

import java.time.Duration;
import java.util.Map;
import org.junit.jupiter.api.Test;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.monitoring.ZLinkRuntimeEventDispatcher;
import systems.zlink.framework.monitoring.ZLinkSocketEventKind;
import systems.zlink.framework.runtime.monitoring.DefaultZLinkMonitoringOptions;
import systems.zlink.framework.runtime.monitoring.ZLinkMonitoringRuntime;
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions;

final class RegistryAndMonitoringTest {
    @Test
    void addZLinkRegistry_defaultsHeartbeatAndBroadcastIntervals() {
        ZLinkEmbeddedRegistryOptions options = new ZLinkEmbeddedRegistryOptions();

        assertEquals(Duration.ofSeconds(5), options.heartbeatInterval());
        assertEquals(Duration.ofSeconds(15), options.heartbeatTimeout());
        assertEquals(Duration.ofSeconds(30), options.broadcastInterval());
    }

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
    void addZLinkRegistry_throws_whenHeartbeatIntervalIsNotPositive() {
        ZLinkEmbeddedRegistryOptions options = new ZLinkEmbeddedRegistryOptions();

        assertThrows(ZLinkConfigurationException.class,
            () -> options.setHeartbeatInterval(Duration.ZERO));
    }

    @Test
    void addZLinkRegistry_throws_whenHeartbeatTimeoutIsNotPositive() {
        ZLinkEmbeddedRegistryOptions options = new ZLinkEmbeddedRegistryOptions();

        assertThrows(ZLinkConfigurationException.class,
            () -> options.setHeartbeatTimeout(Duration.ZERO));
    }

    @Test
    void addZLinkRegistry_throws_whenBroadcastIntervalIsNotPositive() {
        ZLinkEmbeddedRegistryOptions options = new ZLinkEmbeddedRegistryOptions();

        assertThrows(ZLinkConfigurationException.class,
            () -> options.setBroadcastInterval(Duration.ZERO));
    }

    @Test
    void addZLinkRegistry_throws_whenHeartbeatTimeoutEqualsInterval() {
        ZLinkEmbeddedRegistryOptions options = validRegistryOptions();
        options.setHeartbeatInterval(Duration.ofSeconds(5));
        options.setHeartbeatTimeout(Duration.ofSeconds(5));

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void addZLinkRegistry_throws_whenHeartbeatTimeoutIsLessThanInterval() {
        ZLinkEmbeddedRegistryOptions options = validRegistryOptions();
        options.setHeartbeatInterval(Duration.ofSeconds(5));
        options.setHeartbeatTimeout(Duration.ofSeconds(4));

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
                Map.of(),
                new ZLinkRuntimeEventDispatcher()));
    }

    @Test
    void addZLinkMonitoring_throws_whenSocketSourceIsDuplicated() {
        DefaultZLinkMonitoringOptions options = new DefaultZLinkMonitoringOptions();

        options.addSocketEvents("profile", ZLinkSocketEventKind.CONNECTED);

        ZLinkConfigurationException error = assertThrows(
            ZLinkConfigurationException.class,
            () -> options.addSocketEvents("profile", ZLinkSocketEventKind.CONNECTION_READY));
        assertEquals("Duplicate monitoring socket source 'profile'.", error.getMessage());
    }

    @Test
    void addZLinkMonitoring_throws_whenRegistrySourceIsDuplicated() {
        DefaultZLinkMonitoringOptions options = new DefaultZLinkMonitoringOptions();

        options.addRegistryEvents("registry", Duration.ofSeconds(1));

        ZLinkConfigurationException error = assertThrows(
            ZLinkConfigurationException.class,
            () -> options.addRegistryEvents("registry", Duration.ofSeconds(2)));
        assertEquals("Duplicate monitoring registry source 'registry'.", error.getMessage());
    }

    @Test
    void addZLinkMonitoring_throws_whenSpotSourceIsDuplicated() {
        DefaultZLinkMonitoringOptions options = new DefaultZLinkMonitoringOptions();

        options.addSpotEvents("play", Duration.ofSeconds(1));

        ZLinkConfigurationException error = assertThrows(
            ZLinkConfigurationException.class,
            () -> options.addSpotEvents("play", Duration.ofSeconds(2)));
        assertEquals("Duplicate monitoring spot source 'play'.", error.getMessage());
    }

    @Test
    void addZLinkMonitoring_throws_whenRegistrySourceIsUnknownOnStartup() {
        DefaultZLinkMonitoringOptions options = new DefaultZLinkMonitoringOptions();

        options.addRegistryEvents("missing-registry", Duration.ofSeconds(1));

        assertThrows(ZLinkConfigurationException.class, () ->
            new ZLinkMonitoringRuntime(
                options,
                socket -> null,
                Map.of(),
                Map.of(),
                Map.of(),
                new ZLinkRuntimeEventDispatcher()));
    }

    @Test
    void addZLinkMonitoring_throws_whenSpotSourceIsUnknownOnStartup() {
        DefaultZLinkMonitoringOptions options = new DefaultZLinkMonitoringOptions();

        options.addSpotEvents("missing-spot", Duration.ofSeconds(1));

        assertThrows(ZLinkConfigurationException.class, () ->
            new ZLinkMonitoringRuntime(
                options,
                socket -> null,
                Map.of(),
                Map.of(),
                Map.of(),
                new ZLinkRuntimeEventDispatcher()));
    }

    private static ZLinkEmbeddedRegistryOptions validRegistryOptions() {
        ZLinkEmbeddedRegistryOptions options = new ZLinkEmbeddedRegistryOptions();
        options.setPubEndpoint("inproc://registry-pub");
        options.setRouterEndpoint("inproc://registry-router");
        return options;
    }
}
