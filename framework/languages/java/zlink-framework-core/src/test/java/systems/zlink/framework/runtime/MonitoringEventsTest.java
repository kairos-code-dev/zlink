package systems.zlink.framework.runtime;

import static org.junit.jupiter.api.Assertions.assertEquals;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import org.junit.jupiter.api.Test;
import systems.zlink.framework.monitoring.ZLinkRuntimeEventDispatcher;
import systems.zlink.framework.monitoring.ZLinkSocketEvent;
import systems.zlink.framework.monitoring.ZLinkSocketEventKind;

final class MonitoringEventsTest {
    @Test
    void socketMonitoring_emitsConnectedEvent() {
        DefaultZLinkMonitoringOptions options = new DefaultZLinkMonitoringOptions();
        options.addSocketEvents("profile", ZLinkSocketEventKind.CONNECTED);
        FakeSocket socket = new FakeSocket("profile");
        FakeMonitoringBackend backend = new FakeMonitoringBackend();
        ZLinkRuntimeEventDispatcher dispatcher = new ZLinkRuntimeEventDispatcher();
        List<ZLinkSocketEvent> events = new ArrayList<>();
        dispatcher.register(ZLinkSocketEvent.class, event -> {
            events.add(event);
            return java.util.concurrent.CompletableFuture.completedFuture(null);
        });

        try (ZLinkMonitoringRuntime ignored = new ZLinkMonitoringRuntime(
                 options,
                 backend,
                 Map.of("profile", socket),
                 dispatcher)) {
            backend.monitor.emit(new ZLinkBackendSocketMonitorEvent(
                "CONNECTED",
                Optional.empty(),
                "tcp://127.0.0.1:7000",
                "tcp://127.0.0.1:7100"));
        }

        assertEquals(1, events.size());
        assertEquals("profile", events.get(0).sourceName());
        assertEquals(ZLinkSocketEventKind.CONNECTED, events.get(0).event());
        assertEquals("tcp://127.0.0.1:7000", events.get(0).localAddr());
        assertEquals("tcp://127.0.0.1:7100", events.get(0).remoteAddr());
    }

    private static final class FakeMonitoringBackend implements ZLinkMonitoringBackendAdapter {
        private FakeSocketMonitor monitor;

        @Override
        public ZLinkBackendSocketMonitor openSocketMonitor(ZLinkBackendSocket socket) {
            monitor = new FakeSocketMonitor();
            return monitor;
        }
    }

    private static final class FakeSocket implements ZLinkBackendSocket {
        private final String name;

        FakeSocket(String name) {
            this.name = name;
        }

        @Override
        public String name() {
            return name;
        }

        @Override
        public void bind(String endpoint) {
        }

        @Override
        public void close() {
        }
    }

    private static final class FakeSocketMonitor implements ZLinkBackendSocketMonitor {
        private ZLinkBackendSocketMonitorHandler handler;

        @Override
        public void onEvent(ZLinkBackendSocketMonitorHandler handler) {
            this.handler = handler;
        }

        @Override
        public ZLinkBackendSocketMonitorEvent recv() {
            return null;
        }

        @Override
        public String name() {
            return "socketMonitor";
        }

        @Override
        public void close() {
        }

        void emit(ZLinkBackendSocketMonitorEvent event) {
            handler.handle(event);
        }
    }
}
