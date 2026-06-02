package systems.zlink.framework.runtime;

import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.monitoring.ZLinkRuntimeEventDispatcher;
import systems.zlink.framework.monitoring.ZLinkSocketDiagnostic;
import systems.zlink.framework.monitoring.ZLinkSocketEvent;
import systems.zlink.framework.monitoring.ZLinkSocketEventKind;

public final class ZLinkMonitoringRuntime implements AutoCloseable {
    private final List<ZLinkBackendSocketMonitor> socketMonitors = new ArrayList<>();

    public ZLinkMonitoringRuntime(
        DefaultZLinkMonitoringOptions options,
        ZLinkMonitoringBackendAdapter backend,
        Map<String, ZLinkBackendSocket> socketSources,
        ZLinkRuntimeEventDispatcher dispatcher) {
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
        if (!options.registrySources().isEmpty() || !options.spotSources().isEmpty()) {
            throw new ZLinkConfigurationException(
                "registry/spot monitoring polling is not implemented yet");
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
}
