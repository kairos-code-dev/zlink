package systems.zlink.framework.runtime.monitoring;

import java.time.Duration;
import java.util.LinkedHashMap;
import java.util.Map;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.monitoring.ZLinkMonitoringOptions;
import systems.zlink.framework.monitoring.ZLinkSocketEventKind;

public final class DefaultZLinkMonitoringOptions implements ZLinkMonitoringOptions {
    private final Map<String, ZLinkSocketEventKind[]> socketSources = new LinkedHashMap<>();
    private final Map<String, Duration> registrySources = new LinkedHashMap<>();
    private final Map<String, Duration> spotSources = new LinkedHashMap<>();

    @Override
    public void addSocketEvents(String sourceName, ZLinkSocketEventKind... events) {
        socketSources.put(requireName(sourceName, "socket source"), events.clone());
    }

    @Override
    public void addRegistryEvents(String sourceName, Duration interval) {
        registrySources.put(
            requireName(sourceName, "registry source"),
            requirePositive(interval, "registry interval"));
    }

    @Override
    public void addSpotEvents(String sourceName, Duration interval) {
        spotSources.put(
            requireName(sourceName, "spot source"),
            requirePositive(interval, "spot interval"));
    }

    Map<String, ZLinkSocketEventKind[]> socketSources() {
        return Map.copyOf(socketSources);
    }

    Map<String, Duration> registrySources() {
        return Map.copyOf(registrySources);
    }

    Map<String, Duration> spotSources() {
        return Map.copyOf(spotSources);
    }

    public boolean hasSources() {
        return !socketSources.isEmpty()
            || !registrySources.isEmpty()
            || !spotSources.isEmpty();
    }

    public Duration pollInterval() {
        return java.util.stream.Stream.concat(
                registrySources.values().stream(),
                spotSources.values().stream())
            .min(Duration::compareTo)
            .orElse(Duration.ofSeconds(1));
    }

    private static String requireName(String value, String label) {
        if (value == null || value.isBlank()) {
            throw new ZLinkConfigurationException(label + " name is required");
        }
        return value;
    }

    private static Duration requirePositive(Duration value, String label) {
        if (value == null || value.isZero() || value.isNegative()) {
            throw new ZLinkConfigurationException(label + " must be positive");
        }
        return value;
    }
}
