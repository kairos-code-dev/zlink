package systems.zlink.framework.runtime.monitoring;

import java.time.Duration;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Set;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.monitoring.ZLinkMonitoringOptions;
import systems.zlink.framework.monitoring.ZLinkSocketEventKind;

public final class DefaultZLinkMonitoringOptions implements ZLinkMonitoringOptions {
    private final Map<String, ZLinkSocketEventKind[]> socketSources = new LinkedHashMap<>();
    private final Map<String, Duration> registrySources = new LinkedHashMap<>();
    private final Map<String, Duration> spotSources = new LinkedHashMap<>();

    @Override
    public void addSocketEvents(String sourceName, ZLinkSocketEventKind... events) {
        putUnique(
            socketSources,
            requireName(sourceName, "socket source"),
            events.clone(),
            "socket");
    }

    @Override
    public void addRegistryEvents(String sourceName, Duration interval) {
        putUnique(
            registrySources,
            requireName(sourceName, "registry source"),
            requirePositive(interval, "registry interval"),
            "registry");
    }

    @Override
    public void addSpotEvents(String sourceName, Duration interval) {
        putUnique(
            spotSources,
            requireName(sourceName, "spot source"),
            requirePositive(interval, "spot interval"),
            "spot");
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

    public Set<String> registrySourceNames() {
        return Set.copyOf(registrySources.keySet());
    }

    public Set<String> socketSourceNames() {
        return Set.copyOf(socketSources.keySet());
    }

    public Set<String> spotSourceNames() {
        return Set.copyOf(spotSources.keySet());
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

    private static <T> void putUnique(
        Map<String, T> sources,
        String sourceName,
        T value,
        String kind) {
        if (sources.containsKey(sourceName)) {
            throw new ZLinkConfigurationException(
                "Duplicate monitoring " + kind + " source '" + sourceName + "'.");
        }
        sources.put(sourceName, value);
    }
}
