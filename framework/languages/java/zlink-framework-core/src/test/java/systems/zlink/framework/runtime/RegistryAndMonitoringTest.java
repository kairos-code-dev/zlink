package systems.zlink.framework.runtime;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

import java.time.Duration;
import java.util.Map;
import org.junit.jupiter.api.Test;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.runtime.internal.monitoring.ZLinkRuntimeEventDispatcher;
import systems.zlink.framework.monitoring.ZLinkSocketEventKind;
import systems.zlink.framework.runtime.monitoring.DefaultZLinkMonitoringOptions;
import systems.zlink.framework.runtime.monitoring.ZLinkMonitoringRuntime;

final class RegistryAndMonitoringTest {
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
    void addZLinkMonitoring_throws_whenSpotSourceIsDuplicated() {
        DefaultZLinkMonitoringOptions options = new DefaultZLinkMonitoringOptions();

        options.addSpotEvents("play", Duration.ofSeconds(1));

        ZLinkConfigurationException error = assertThrows(
            ZLinkConfigurationException.class,
            () -> options.addSpotEvents("play", Duration.ofSeconds(2)));
        assertEquals("Duplicate monitoring spot source 'play'.", error.getMessage());
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
                new ZLinkRuntimeEventDispatcher()));
    }
}
