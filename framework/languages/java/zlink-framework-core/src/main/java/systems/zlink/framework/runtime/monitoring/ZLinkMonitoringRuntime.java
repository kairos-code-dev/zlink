package systems.zlink.framework.runtime.monitoring;

import java.time.Instant;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.service.spot.MeshNodeStatus;
import systems.zlink.contracts.service.spot.MeshPeerEntry;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.locations.ZLinkLocationRuntimeQuery;
import systems.zlink.framework.locations.ZLinkLocationRuntimeStatus;
import systems.zlink.framework.locations.ZLinkLocationServiceSummary;
import systems.zlink.framework.locations.ZLinkLocationServiceSummaryFilter;
import systems.zlink.framework.locations.ZLinkLocationTopologyEntry;
import systems.zlink.framework.locations.ZLinkLocationTopologyFilter;
import systems.zlink.framework.locations.ZLinkPageRequest;
import systems.zlink.framework.monitoring.ZLinkLocationRuntimeEvent;
import systems.zlink.framework.monitoring.ZLinkLocationRuntimeEventKind;
import systems.zlink.framework.monitoring.ZLinkRuntimeEventDispatcher;
import systems.zlink.framework.monitoring.ZLinkSocketDiagnostic;
import systems.zlink.framework.monitoring.ZLinkSocketEvent;
import systems.zlink.framework.monitoring.ZLinkSocketEventKind;
import systems.zlink.framework.monitoring.ZLinkSpotEvent;
import systems.zlink.framework.monitoring.ZLinkSpotEventKind;
import systems.zlink.framework.runtime.backend.ZLinkBackendSocket;
import systems.zlink.framework.runtime.backend.ZLinkBackendSocketMonitor;
import systems.zlink.framework.runtime.backend.ZLinkBackendSocketMonitorEvent;
import systems.zlink.framework.runtime.internal.backend.ZLinkInternalMeshNode;
import systems.zlink.framework.runtime.backend.ZLinkMonitoringBackendAdapter;

public final class ZLinkMonitoringRuntime implements AutoCloseable {
    private final List<ZLinkBackendSocketMonitor> socketMonitors = new ArrayList<>();
    private final Map<String, ZLinkInternalMeshNode> spotSources = new HashMap<>();
    private final Map<String, SpotSnapshot> previousSpotSnapshots = new HashMap<>();
    private final Map<String, LocationSnapshot> previousLocationSnapshots = new HashMap<>();
    private final Map<String, ZLinkLocationRuntimeQuery> locationRuntimeSources = new HashMap<>();
    private final ZLinkRuntimeEventDispatcher dispatcher;

    public ZLinkMonitoringRuntime(
        DefaultZLinkMonitoringOptions options,
        ZLinkMonitoringBackendAdapter backend,
        Map<String, ZLinkBackendSocket> socketSources,
        ZLinkRuntimeEventDispatcher dispatcher) {
        this(options, backend, socketSources, Map.of(), null, dispatcher);
    }

    public ZLinkMonitoringRuntime(
        DefaultZLinkMonitoringOptions options,
        ZLinkMonitoringBackendAdapter backend,
        Map<String, ZLinkBackendSocket> socketSources,
        Map<String, ZLinkInternalMeshNode> spotSources,
        ZLinkLocationRuntimeQuery locationRuntimeQuery,
        ZLinkRuntimeEventDispatcher dispatcher) {
        Objects.requireNonNull(options, "options");
        Objects.requireNonNull(backend, "backend");
        Objects.requireNonNull(socketSources, "socketSources");
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
        for (String sourceName : options.spotSources().keySet()) {
            ZLinkInternalMeshNode spotNode = spotSources.get(sourceName);
            if (spotNode == null) {
                throw new ZLinkConfigurationException(
                    "monitoring spot source is not configured: " + sourceName);
            }
            this.spotSources.put(sourceName, spotNode);
        }
        for (String sourceName : options.locationRuntimeSourceNames()) {
            if (locationRuntimeQuery == null) {
                throw new ZLinkConfigurationException(
                    "monitoring location runtime source requires configured location stores: " + sourceName);
            }
            this.locationRuntimeSources.put(sourceName, locationRuntimeQuery);
        }
    }

    public ZLinkMonitoringRuntime(
        DefaultZLinkMonitoringOptions options,
        ZLinkMonitoringBackendAdapter backend,
        Map<String, ZLinkBackendSocket> socketSources,
        Map<String, ZLinkInternalMeshNode> spotSources,
        ZLinkRuntimeEventDispatcher dispatcher) {
        this(options, backend, socketSources, spotSources, null, dispatcher);
    }

    public void pollSnapshots() {
        for (Map.Entry<String, ZLinkInternalMeshNode> entry : spotSources.entrySet()) {
            pollSpot(entry.getKey(), entry.getValue());
        }
        for (Map.Entry<String, ZLinkLocationRuntimeQuery> entry : locationRuntimeSources.entrySet()) {
            pollLocationRuntime(entry.getKey(), entry.getValue());
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

    private void pollSpot(String sourceName, ZLinkInternalMeshNode spotNode) {
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
    }

    private void pollLocationRuntime(String sourceName, ZLinkLocationRuntimeQuery query) {
        query.getStatus()
            .thenCompose(status -> listAllTopology(query)
                .thenCompose(topology -> query.listServiceSummaries(ZLinkLocationServiceSummaryFilter.all())
                    .thenApply(summary -> new LocationSnapshot(status, topology, List.copyOf(summary)))))
            .whenComplete((current, failure) -> {
                if (failure != null) {
                    dispatcher.publish(new ZLinkLocationRuntimeEvent(
                        sourceName,
                        Instant.now(),
                        ZLinkLocationRuntimeEventKind.STORE_FAILURE,
                        null,
                        null,
                        null));
                    return;
                }
                publishLocationDiff(sourceName, current);
            });
    }

    private CompletionStage<List<ZLinkLocationTopologyEntry>> listAllTopology(
        ZLinkLocationRuntimeQuery query) {
        List<ZLinkLocationTopologyEntry> entries = new ArrayList<>();
        CompletableFuture<List<ZLinkLocationTopologyEntry>> result = new CompletableFuture<>();
        fetchTopologyPage(query, null, entries, result);
        return result;
    }

    private void fetchTopologyPage(
        ZLinkLocationRuntimeQuery query,
        String continuation,
        List<ZLinkLocationTopologyEntry> entries,
        CompletableFuture<List<ZLinkLocationTopologyEntry>> result) {
        query.listTopology(
                ZLinkLocationTopologyFilter.all(),
                new ZLinkPageRequest(128, continuation))
            .whenComplete((page, failure) -> {
                if (failure != null) {
                    result.completeExceptionally(failure);
                    return;
                }
                entries.addAll(page.items());
                if (page.continuationToken() == null) {
                    result.complete(List.copyOf(entries));
                    return;
                }
                fetchTopologyPage(query, page.continuationToken(), entries, result);
            });
    }

    private void publishLocationDiff(String sourceName, LocationSnapshot current) {
        LocationSnapshot previous = previousLocationSnapshots.put(sourceName, current);
        if (previous != null && !previous.status().storeHealthy() && current.status().storeHealthy()) {
            dispatcher.publish(new ZLinkLocationRuntimeEvent(
                sourceName,
                Instant.now(),
                ZLinkLocationRuntimeEventKind.STORE_RECOVERED,
                current.status(),
                null,
                null));
        }
        if (previous == null || !Objects.equals(previous.status(), current.status())) {
            dispatcher.publish(new ZLinkLocationRuntimeEvent(
                sourceName,
                Instant.now(),
                ZLinkLocationRuntimeEventKind.STATUS_CHANGED,
                current.status(),
                null,
                null));
        }
        if (previous == null || !Objects.equals(previous.topology(), current.topology())) {
            dispatcher.publish(new ZLinkLocationRuntimeEvent(
                sourceName,
                Instant.now(),
                ZLinkLocationRuntimeEventKind.TOPOLOGY_CHANGED,
                null,
                current.topology(),
                null));
        }
        if (previous == null || !Objects.equals(previous.serviceSummary(), current.serviceSummary())) {
            dispatcher.publish(new ZLinkLocationRuntimeEvent(
                sourceName,
                Instant.now(),
                ZLinkLocationRuntimeEventKind.SERVICE_SUMMARY_CHANGED,
                null,
                null,
                current.serviceSummary()));
        }
    }

    private record SpotSnapshot(
        MeshNodeStatus status,
        List<MeshPeerEntry> peers,
        List<String> subjects) {
        static SpotSnapshot from(ZLinkInternalMeshNode spotNode) {
            return new SpotSnapshot(
                spotNode.status(),
                List.copyOf(spotNode.peers()),
                List.of());
        }
    }

    private record LocationSnapshot(
        ZLinkLocationRuntimeStatus status,
        List<ZLinkLocationTopologyEntry> topology,
        List<ZLinkLocationServiceSummary> serviceSummary) {
    }

}
