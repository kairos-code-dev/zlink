package systems.zlink.framework.runtime.monitoring;

import java.time.Instant;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import systems.zlink.contracts.service.spot.SpotNodePeerEntry;
import systems.zlink.contracts.service.spot.SpotNodeStatus;
import systems.zlink.contracts.service.spot.SpotNodeSubjectEntry;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.monitoring.ZLinkRegistryEvent;
import systems.zlink.framework.monitoring.ZLinkRegistryEventKind;
import systems.zlink.framework.monitoring.ZLinkRuntimeEventDispatcher;
import systems.zlink.framework.monitoring.ZLinkSocketDiagnostic;
import systems.zlink.framework.monitoring.ZLinkSocketEvent;
import systems.zlink.framework.monitoring.ZLinkSocketEventKind;
import systems.zlink.framework.monitoring.ZLinkSpotEvent;
import systems.zlink.framework.monitoring.ZLinkSpotEventKind;
import systems.zlink.framework.registry.ZLinkRegistryServiceSummaryEntry;
import systems.zlink.framework.registry.ZLinkRegistryStatus;
import systems.zlink.framework.registry.ZLinkRegistryTopologyEntry;
import systems.zlink.framework.runtime.backend.ZLinkBackendRegistry;
import systems.zlink.framework.runtime.backend.ZLinkBackendRegistryQueryFilter;
import systems.zlink.framework.runtime.backend.ZLinkBackendRegistryServiceSummaryEntry;
import systems.zlink.framework.runtime.backend.ZLinkBackendRegistryStatus;
import systems.zlink.framework.runtime.backend.ZLinkBackendRegistryTopologyEntry;
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
        for (Map.Entry<String, ZLinkSocketEventKind[]> entry : options.socketSources().entrySet()) {
            String sourceName = entry.getKey();
            ZLinkBackendSocket socket = socketSources.get(sourceName);
            if (socket == null) {
                throw new ZLinkConfigurationException(
                    "monitoring socket source is not configured: " + sourceName);
            }
            ZLinkBackendSocketMonitor monitor = backend.openSocketMonitor(socket);
            monitor.onEvent(event -> {
                ZLinkSocketEvent socketEvent = toSocketEvent(sourceName, event);
                if (accepts(entry.getValue(), socketEvent.event())) {
                    dispatcher.publish(socketEvent);
                }
            });
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
        return switch (event) {
            case "CONNECTED", "ACCEPTED", "LISTENING" -> ZLinkSocketEventKind.CONNECTED;
            case "CONNECTION_READY" -> ZLinkSocketEventKind.CONNECTION_READY;
            case "DISCONNECTED" -> ZLinkSocketEventKind.DISCONNECTED;
            case "HANDSHAKE_FAILED_NO_DETAIL",
                 "HANDSHAKE_FAILED_PROTOCOL",
                 "HANDSHAKE_FAILED_AUTH" -> ZLinkSocketEventKind.HANDSHAKE_FAILED;
            case "PEER_WEIGHT_CHANGED" -> ZLinkSocketEventKind.PEER_ADMISSION_CHANGED;
            case "CLOSED", "CLOSE_FAILED", "MONITOR_STOPPED" -> ZLinkSocketEventKind.CLOSED;
            default -> ZLinkSocketEventKind.INTERNAL;
        };
    }

    private static boolean accepts(
        ZLinkSocketEventKind[] enabled,
        ZLinkSocketEventKind event) {
        if (enabled == null || enabled.length == 0) {
            return true;
        }
        for (ZLinkSocketEventKind candidate : enabled) {
            if (candidate == event) {
                return true;
            }
        }
        return false;
    }

    private void pollRegistry(String sourceName, ZLinkBackendRegistry registry) {
        RegistrySnapshot current = RegistrySnapshot.from(registry);
        RegistrySnapshot previous = previousRegistrySnapshots.put(sourceName, current);
        if (previous == null || !previous.status().equals(current.status())) {
            dispatcher.publish(new ZLinkRegistryEvent(
                sourceName,
                Instant.now(),
                ZLinkRegistryEventKind.STATUS_CHANGED,
                Optional.of(current.status()),
                current.topology(),
                current.serviceSummary()));
        }
        if (previous == null || !previous.topology().equals(current.topology())) {
            dispatcher.publish(new ZLinkRegistryEvent(
                sourceName,
                Instant.now(),
                ZLinkRegistryEventKind.TOPOLOGY_CHANGED,
                Optional.empty(),
                current.topology(),
                current.serviceSummary()));
        }
        if (previous == null || !previous.serviceSummary().equals(current.serviceSummary())) {
            dispatcher.publish(new ZLinkRegistryEvent(
                sourceName,
                Instant.now(),
                ZLinkRegistryEventKind.SERVICE_SUMMARY_CHANGED,
                Optional.empty(),
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
                Optional.of(current.status()),
                current.peers(),
                current.subjects(),
                Optional.empty()));
        }
        if (previous == null || !previous.peers().equals(current.peers())) {
            dispatcher.publish(new ZLinkSpotEvent(
                sourceName,
                Instant.now(),
                ZLinkSpotEventKind.PEERS_CHANGED,
                Optional.empty(),
                current.peers(),
                current.subjects(),
                Optional.empty()));
        }
        if (previous == null || !previous.subjects().equals(current.subjects())) {
            dispatcher.publish(new ZLinkSpotEvent(
                sourceName,
                Instant.now(),
                ZLinkSpotEventKind.SUBJECTS_CHANGED,
                Optional.empty(),
                current.peers(),
                current.subjects(),
                Optional.empty()));
        }
    }

    private record RegistrySnapshot(
        ZLinkRegistryStatus status,
        List<ZLinkRegistryTopologyEntry> topology,
        List<ZLinkRegistryServiceSummaryEntry> serviceSummary) {
        static RegistrySnapshot from(ZLinkBackendRegistry registry) {
            ZLinkBackendRegistryQueryFilter all = ZLinkBackendRegistryQueryFilter.all();
            return new RegistrySnapshot(
                toStatus(registry.status()),
                registry.topology(all).stream()
                    .map(ZLinkMonitoringRuntime::toTopologyEntry)
                    .toList(),
                registry.serviceSummary(all).stream()
                    .map(ZLinkMonitoringRuntime::toServiceSummaryEntry)
                    .toList());
        }
    }

    private record SpotSnapshot(
        SpotNodeStatus status,
        List<SpotNodePeerEntry> peers,
        List<SpotNodeSubjectEntry> subjects) {
        static SpotSnapshot from(ZLinkBackendSpotNode spotNode) {
            return new SpotSnapshot(
                spotNode.status(),
                List.copyOf(spotNode.peers()),
                List.copyOf(spotNode.subjects()));
        }
    }

    private static ZLinkRegistryStatus toStatus(ZLinkBackendRegistryStatus status) {
        return new ZLinkRegistryStatus(
            status.registryId(),
            status.bindEndpoint(),
            status.state(),
            status.topologyEntryCount(),
            status.peerRegistryCount(),
            status.connectedPeerRegistryCount(),
            status.listSeq(),
            status.lastError(),
            status.lastChangedMs());
    }

    private static ZLinkRegistryTopologyEntry toTopologyEntry(
        ZLinkBackendRegistryTopologyEntry entry) {
        return new ZLinkRegistryTopologyEntry(
            entry.autoConnectType(),
            entry.routingId(),
            entry.serviceKind(),
            entry.serviceRole(),
            entry.channelName(),
            entry.endpoint(),
            entry.source(),
            entry.state(),
            entry.desiredCount(),
            entry.readyCount(),
            entry.errorCode(),
            entry.lastReportedMs(),
            entry.spotKind());
    }

    private static ZLinkRegistryServiceSummaryEntry toServiceSummaryEntry(
        ZLinkBackendRegistryServiceSummaryEntry entry) {
        return new ZLinkRegistryServiceSummaryEntry(
            entry.autoConnectType(),
            entry.serviceRole(),
            entry.channelName(),
            entry.totalCount(),
            entry.connectingCount(),
            entry.readyCount(),
            entry.errorCount(),
            entry.stoppedCount(),
            entry.lastReportedMs());
    }
}
