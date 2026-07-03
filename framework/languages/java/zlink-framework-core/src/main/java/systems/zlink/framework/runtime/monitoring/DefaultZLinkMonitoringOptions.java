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
    private final Map<String, Duration> spotSources = new LinkedHashMap<>();
    private final Map<String, Duration> locationRuntimeSources = new LinkedHashMap<>();

    @Override
    public void addSocketEvents(String sourceName, ZLinkSocketEventKind... events) {
        putUnique(
            socketSources,
            requireName(sourceName, "socket source"),
            events.clone(),
            "socket");
    }

    @Override
    public void addSpotEvents(String sourceName, Duration interval) {
        putUnique(
            spotSources,
            requireName(sourceName, "spot source"),
            requirePositive(interval, "spot interval"),
            "spot");
    }

    @Override
    public void addLocationRuntimeEvents(String sourceName, Duration interval) {
        putUnique(
            locationRuntimeSources,
            requireName(sourceName, "location runtime source"),
            requirePositive(interval, "location runtime interval"),
            "location runtime");
    }

    Map<String, ZLinkSocketEventKind[]> socketSources() {
        return Map.copyOf(socketSources);
    }

    Map<String, Duration> spotSources() {
        return Map.copyOf(spotSources);
    }

    Map<String, Duration> locationRuntimeSources() {
        return Map.copyOf(locationRuntimeSources);
    }

    public boolean hasSources() {
        return !socketSources.isEmpty()
            || !spotSources.isEmpty()
            || !locationRuntimeSources.isEmpty();
    }

    public Duration pollInterval() {
        return java.util.stream.Stream.concat(
                spotSources.values().stream(),
                locationRuntimeSources.values().stream())
            .min(Duration::compareTo)
            .orElse(Duration.ofSeconds(1));
    }

    public Set<String> socketSourceNames() {
        return Set.copyOf(socketSources.keySet());
    }

    public Set<String> spotSourceNames() {
        return Set.copyOf(spotSources.keySet());
    }

    public Set<String> locationRuntimeSourceNames() {
        return Set.copyOf(locationRuntimeSources.keySet());
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
