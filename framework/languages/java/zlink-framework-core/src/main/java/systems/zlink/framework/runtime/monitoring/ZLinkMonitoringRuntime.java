package systems.zlink.framework.runtime.monitoring;

import java.time.Instant;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.monitoring.ZLinkRegistryEvent;
import systems.zlink.framework.monitoring.ZLinkRegistryEventKind;
import systems.zlink.framework.monitoring.ZLinkRuntimeEventDispatcher;
import systems.zlink.framework.monitoring.ZLinkSocketDiagnostic;
import systems.zlink.framework.monitoring.ZLinkSocketEvent;
import systems.zlink.framework.monitoring.ZLinkSocketEventKind;
import systems.zlink.framework.monitoring.ZLinkSpotEvent;
import systems.zlink.framework.monitoring.ZLinkSpotEventKind;
import systems.zlink.framework.runtime.backend.ZLinkBackendRegistry;
import systems.zlink.framework.runtime.backend.ZLinkBackendRegistryQueryFilter;
import systems.zlink.framework.runtime.backend.ZLinkBackendSocket;
import systems.zlink.framework.runtime.backend.ZLinkBackendSocketMonitor;
import systems.zlink.framework.runtime.backend.ZLinkBackendSocketMonitorEvent;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpotNode;
import systems.zlink.framework.runtime.backend.ZLinkMonitoringBackendAdapter;

public final class ZLinkMonitoringRuntime implements AutoCloseable {
    private final List<ZLinkBackendSocketMonitor> socketMonitors = new ArrayList<>();
    private final Map<String, ZLinkBackendRegistry> registrySources = new HashMap<>();
    private final Map<String, ZLinkBackendSpotNode> spotSources = new HashMap<>();
    private final Map<String, RegistrySnapshot> previousRegistrySnapshots = new HashMap<>();
    private final Map<String, SpotSnapshot> previousSpotSnapshots = new HashMap<>();
    private final ZLinkRuntimeEventDispatcher dispatcher;

    public ZLinkMonitoringRuntime(
        DefaultZLinkMonitoringOptions options,
        ZLinkMonitoringBackendAdapter backend,
        Map<String, ZLinkBackendSocket> socketSources,
        ZLinkRuntimeEventDispatcher dispatcher) {
        this(options, backend, socketSources, Map.of(), Map.of(), dispatcher);
    }

    public ZLinkMonitoringRuntime(
        DefaultZLinkMonitoringOptions options,
        ZLinkMonitoringBackendAdapter backend,
        Map<String, ZLinkBackendSocket> socketSources,
        Map<String, ZLinkBackendRegistry> registrySources,
        Map<String, ZLinkBackendSpotNode> spotSources,
        ZLinkRuntimeEventDispatcher dispatcher) {
        Objects.requireNonNull(options, "options");
        Objects.requireNonNull(backend, "backend");
        Objects.requireNonNull(socketSources, "socketSources");
        Objects.requireNonNull(registrySources, "registrySources");
        Objects.requireNonNull(spotSources, "spotSources");
        this.dispatcher = Objects.requireNonNull(dispatcher, "dispatcher");
        for (String sourceName : options.socketSources().keySet()) {
            ZLinkBackendSocket socket = socketSources.get(sourceName);
            if (socket == null) {
                throw new ZLinkConfigurationException(
                    "monitoring socket source is not configured: " + sourceName);
            }
            ZLinkBackendSocketMonitor monitor = backend.openSocketMonitor(socket);
            monitor.onEvent(event -> dispatcher.publish(toSocketEvent(sourceName, event)));
            socketMonitors.add(monitor);
        }
        for (String sourceName : options.registrySources().keySet()) {
            ZLinkBackendRegistry registry = registrySources.get(sourceName);
            if (registry == null) {
                throw new ZLinkConfigurationException(
                    "monitoring registry source is not configured: " + sourceName);
            }
            this.registrySources.put(sourceName, registry);
        }
        for (String sourceName : options.spotSources().keySet()) {
            ZLinkBackendSpotNode spotNode = spotSources.get(sourceName);
            if (spotNode == null) {
                throw new ZLinkConfigurationException(
                    "monitoring spot source is not configured: " + sourceName);
            }
            this.spotSources.put(sourceName, spotNode);
        }
    }

    public void pollSnapshots() {
        for (Map.Entry<String, ZLinkBackendRegistry> entry : registrySources.entrySet()) {
            pollRegistry(entry.getKey(), entry.getValue());
        }
        for (Map.Entry<String, ZLinkBackendSpotNode> entry : spotSources.entrySet()) {
            pollSpot(entry.getKey(), entry.getValue());
        }
    }

    @Override
    public void close() {
        for (ZLinkBackendSocketMonitor monitor : socketMonitors) {
            monitor.close();
        }
    }

    private static ZLinkSocketEvent toSocketEvent(
        String sourceName,
        ZLinkBackendSocketMonitorEvent event) {
        return new ZLinkSocketEvent(
            sourceName,
            Instant.now(),
            toKind(event.event()),
            event.routingId(),
            event.localAddress(),
            event.remoteAddress(),
            Optional.of(new ZLinkSocketDiagnostic(0, 0)));
    }

    private static ZLinkSocketEventKind toKind(String event) {
        if (event == null || event.isBlank()) {
            return ZLinkSocketEventKind.INTERNAL;
        }
        try {
            return ZLinkSocketEventKind.valueOf(event);
        } catch (IllegalArgumentException ignored) {
            return ZLinkSocketEventKind.INTERNAL;
        }
    }

    private void pollRegistry(String sourceName, ZLinkBackendRegistry registry) {
        RegistrySnapshot current = RegistrySnapshot.from(registry);
        RegistrySnapshot previous = previousRegistrySnapshots.put(sourceName, current);
        if (previous == null || !previous.status().equals(current.status())) {
            dispatcher.publish(new ZLinkRegistryEvent(
                sourceName,
                Instant.now(),
                ZLinkRegistryEventKind.STATUS_CHANGED,
                current.topology(),
                current.serviceSummary()));
        }
        if (previous == null || !previous.topology().equals(current.topology())) {
            dispatcher.publish(new ZLinkRegistryEvent(
                sourceName,
                Instant.now(),
                ZLinkRegistryEventKind.TOPOLOGY_CHANGED,
                current.topology(),
                current.serviceSummary()));
        }
        if (previous == null || !previous.serviceSummary().equals(current.serviceSummary())) {
            dispatcher.publish(new ZLinkRegistryEvent(
                sourceName,
                Instant.now(),
                ZLinkRegistryEventKind.SERVICE_SUMMARY_CHANGED,
                current.topology(),
                current.serviceSummary()));
        }
    }

    private void pollSpot(String sourceName, ZLinkBackendSpotNode spotNode) {
        SpotSnapshot current = SpotSnapshot.from(spotNode);
        SpotSnapshot previous = previousSpotSnapshots.put(sourceName, current);
        if (previous == null || !previous.status().equals(current.status())) {
            dispatcher.publish(new ZLinkSpotEvent(
                sourceName,
                Instant.now(),
                ZLinkSpotEventKind.STATUS_CHANGED,
                current.peers(),
                current.subjects(),
                ""));
        }
        if (previous == null || !previous.peers().equals(current.peers())) {
            dispatcher.publish(new ZLinkSpotEvent(
                sourceName,
                Instant.now(),
                ZLinkSpotEventKind.PEERS_CHANGED,
                current.peers(),
                current.subjects(),
                ""));
        }
        if (previous == null || !previous.subjects().equals(current.subjects())) {
            dispatcher.publish(new ZLinkSpotEvent(
                sourceName,
                Instant.now(),
                ZLinkSpotEventKind.SUBJECTS_CHANGED,
                current.peers(),
                current.subjects(),
                ""));
        }
    }

    private record RegistrySnapshot(
        String status,
        List<String> topology,
        List<String> serviceSummary) {
        static RegistrySnapshot from(ZLinkBackendRegistry registry) {
            ZLinkBackendRegistryQueryFilter all = ZLinkBackendRegistryQueryFilter.all();
            return new RegistrySnapshot(
                registry.status().toString(),
                registry.topology(all).stream()
                    .map(Object::toString)
                    .toList(),
                registry.serviceSummary(all).stream()
                    .map(Object::toString)
                    .toList());
        }
    }

    private record SpotSnapshot(
        String status,
        List<String> peers,
        List<String> subjects) {
        static SpotSnapshot from(ZLinkBackendSpotNode spotNode) {
            return new SpotSnapshot(
                String.valueOf(spotNode.status()),
                spotNode.peers().stream().map(Object::toString).toList(),
                spotNode.subjects().stream().map(Object::toString).toList());
        }
    }
}
