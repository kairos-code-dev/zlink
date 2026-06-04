package systems.zlink.framework.runtime;

import systems.zlink.framework.runtime.backend.*;

import static org.junit.jupiter.api.Assertions.assertEquals;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import org.junit.jupiter.api.Test;
import systems.zlink.framework.monitoring.ZLinkRuntimeEventDispatcher;
import systems.zlink.framework.monitoring.ZLinkRegistryEvent;
import systems.zlink.framework.monitoring.ZLinkRegistryEventKind;
import systems.zlink.framework.monitoring.ZLinkSocketEvent;
import systems.zlink.framework.monitoring.ZLinkSocketEventKind;
import systems.zlink.framework.monitoring.ZLinkSpotEvent;
import systems.zlink.framework.monitoring.ZLinkSpotEventKind;
import systems.zlink.framework.runtime.monitoring.DefaultZLinkMonitoringOptions;
import systems.zlink.framework.runtime.monitoring.ZLinkMonitoringRuntime;

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

    @Test
    void registryMonitoring_emitsStatusChanged_forEmbeddedRegistry() {
        DefaultZLinkMonitoringOptions options = new DefaultZLinkMonitoringOptions();
        options.addRegistryEvents("registry", java.time.Duration.ofSeconds(1));
        FakeRegistry registry = new FakeRegistry();
        ZLinkRuntimeEventDispatcher dispatcher = new ZLinkRuntimeEventDispatcher();
        List<ZLinkRegistryEvent> events = new ArrayList<>();
        dispatcher.register(ZLinkRegistryEvent.class, event -> {
            events.add(event);
            return java.util.concurrent.CompletableFuture.completedFuture(null);
        });

        try (ZLinkMonitoringRuntime runtime = new ZLinkMonitoringRuntime(
                 options,
                 socket -> null,
                 Map.of(),
                 Map.of("registry", registry),
                 Map.of(),
                 dispatcher)) {
            runtime.pollSnapshots();
            runtime.pollSnapshots();
            registry.entryCount = 2;
            runtime.pollSnapshots();
        }

        assertEquals(List.of(
                ZLinkRegistryEventKind.STATUS_CHANGED,
                ZLinkRegistryEventKind.TOPOLOGY_CHANGED,
                ZLinkRegistryEventKind.SERVICE_SUMMARY_CHANGED,
                ZLinkRegistryEventKind.STATUS_CHANGED,
                ZLinkRegistryEventKind.TOPOLOGY_CHANGED,
                ZLinkRegistryEventKind.SERVICE_SUMMARY_CHANGED),
            events.stream().map(ZLinkRegistryEvent::event).toList());
    }

    @Test
    void spotMonitoring_emitsSubjectsChanged_whenSpotIsCreated() {
        DefaultZLinkMonitoringOptions options = new DefaultZLinkMonitoringOptions();
        options.addSpotEvents("play", java.time.Duration.ofSeconds(1));
        FakeSpotNode spotNode = new FakeSpotNode();
        ZLinkRuntimeEventDispatcher dispatcher = new ZLinkRuntimeEventDispatcher();
        List<ZLinkSpotEvent> events = new ArrayList<>();
        dispatcher.register(ZLinkSpotEvent.class, event -> {
            events.add(event);
            return java.util.concurrent.CompletableFuture.completedFuture(null);
        });

        try (ZLinkMonitoringRuntime runtime = new ZLinkMonitoringRuntime(
                 options,
                 socket -> null,
                 Map.of(),
                 Map.of(),
                 Map.of("play", spotNode),
                 dispatcher)) {
            runtime.pollSnapshots();
            runtime.pollSnapshots();
            spotNode.subjects = List.of(new ZLinkBackendSpotNodeSubjectEntry(
                "room-1",
                "spot",
                true));
            runtime.pollSnapshots();
        }

        assertEquals(List.of(
                ZLinkSpotEventKind.STATUS_CHANGED,
                ZLinkSpotEventKind.PEERS_CHANGED,
                ZLinkSpotEventKind.SUBJECTS_CHANGED,
                ZLinkSpotEventKind.STATUS_CHANGED,
                ZLinkSpotEventKind.SUBJECTS_CHANGED),
            events.stream().map(ZLinkSpotEvent::event).toList());
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

    private static final class FakeRegistry implements ZLinkBackendRegistry {
        int entryCount = 1;

        @Override
        public void setId(int registryId) {
        }

        @Override
        public void bind(String pubEndpoint, String routerEndpoint) {
        }

        @Override
        public void connectPeer(String pubEndpoint, String routerEndpoint) {
        }

        @Override
        public ZLinkBackendRegistryStatus status() {
            return new ZLinkBackendRegistryStatus("BOUND", entryCount);
        }

        @Override
        public List<ZLinkBackendRegistryServiceSummaryEntry> serviceSummary(
            ZLinkBackendRegistryQueryFilter filter) {
            return List.of(new ZLinkBackendRegistryServiceSummaryEntry(
                "CLIENT_SERVER",
                "DEALER",
                "profile",
                entryCount,
                0,
                entryCount,
                0,
                0,
                11));
        }

        @Override
        public List<ZLinkBackendRegistryTopologyEntry> topology(
            ZLinkBackendRegistryQueryFilter filter) {
            return List.of(new ZLinkBackendRegistryTopologyEntry(
                "CLIENT_SERVER",
                systems.zlink.contracts.core.RoutingId.from("profile-" + entryCount),
                "SOCKET",
                "SERVER",
                "profile",
                "inproc://profile-" + entryCount,
                "MANUAL",
                "READY",
                1,
                1,
                0,
                12,
                systems.zlink.framework.spots.ZLinkSpotKind.INVALID));
        }

        @Override
        public List<ZLinkBackendRegistryMemberPeerEntry> memberPeers(String channelName) {
            return List.of(new ZLinkBackendRegistryMemberPeerEntry(
                "CLIENT_SERVER",
                "ROUTER",
                channelName,
                "inproc://" + channelName,
                systems.zlink.contracts.core.RoutingId.from(channelName + "-peer"),
                1,
                1));
        }

        @Override
        public String name() {
            return "registry";
        }

        @Override
        public void close() {
        }
    }

    private static final class FakeSpotNode implements ZLinkBackendSpotNode {
        List<ZLinkBackendSpotNodeSubjectEntry> subjects = List.of();

        @Override
        public systems.zlink.contracts.core.RoutingId routingId() {
            return systems.zlink.contracts.core.RoutingId.from("play");
        }

        @Override
        public void setRoutingId(systems.zlink.contracts.core.RoutingId routingId) {
        }

        @Override
        public void setRouterBind(String endpoint) {
        }

        @Override
        public void setPubBind(String endpoint) {
        }

        @Override
        public void attachDiscovery(ZLinkBackendDiscovery discovery) {
        }

        @Override
        public void connectPeer(String endpoint) {
        }

        @Override
        public void connectRouterChannelPeer(String channelName, String endpoint) {
        }

        @Override
        public void connectRouterChannelPeerRid(
            String channelName,
            systems.zlink.contracts.core.RoutingId peerRid,
            String endpoint) {
        }

        @Override
        public void attachSpotRouteChannelDiscovery(
            String channelName,
            ZLinkBackendDiscovery discovery) {
        }

        @Override
        public void attachChannelDealer(ZLinkBackendDiscovery discovery, ZLinkBackendDealerSocket dealer) {
        }

        @Override
        public void attachChannelDealerManual(String channelName, ZLinkBackendDealerSocket dealer) {
        }

        @Override
        public ZLinkBackendSpot createSpot() {
            return null;
        }

        @Override
        public ZLinkBackendSpot entrySpot() {
            return null;
        }

        @Override
        public ZLinkBackendActorRef createActor(String actorId) {
            return null;
        }

        @Override
        public ZLinkBackendActorRef actorLookup(String actorId) {
            return null;
        }

        @Override
        public java.util.concurrent.CompletionStage<ZLinkBackendActorJoinResult> joinActor(
            ZLinkBackendActorRef actor,
            systems.zlink.contracts.core.RoutingId targetNodeRid,
            systems.zlink.contracts.core.RoutingId targetSpotRid,
            List<systems.zlink.contracts.messaging.Message> parts,
            java.time.Duration timeout) {
            return java.util.concurrent.CompletableFuture.failedFuture(
                new UnsupportedOperationException("join actor is not used by this test"));
        }

        @Override
        public java.util.concurrent.CompletionStage<ZLinkBackendActorJoinEntrySpotResult> joinActorEntrySpot(
            ZLinkBackendActorRef actor,
            systems.zlink.contracts.core.RoutingId targetNodeRid,
            java.time.Duration timeout) {
            return java.util.concurrent.CompletableFuture.failedFuture(
                new UnsupportedOperationException("join entry spot is not used by this test"));
        }

        @Override
        public boolean sendActorBoundSession(
            ZLinkBackendActorRef actor,
            List<systems.zlink.contracts.messaging.Message> parts,
            systems.zlink.contracts.sockets.SendFlags flags) {
            return false;
        }

        @Override
        public void closeActorBoundSession(
            ZLinkBackendActorRef actor,
            java.time.Duration timeout) {
        }

        @Override
        public ZLinkBackendSpotNodeStatus status() {
            return new ZLinkBackendSpotNodeStatus("RUNNING", 0, subjects.size());
        }

        @Override
        public List<ZLinkBackendSpotNodePeerEntry> peers() {
            return List.of();
        }

        @Override
        public List<ZLinkBackendSpotNodeSubjectEntry> subjects() {
            return subjects;
        }

        @Override
        public String name() {
            return "play";
        }

        @Override
        public void close() {
        }
    }
}
