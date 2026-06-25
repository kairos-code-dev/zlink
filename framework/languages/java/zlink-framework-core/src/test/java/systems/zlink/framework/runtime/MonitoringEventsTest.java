package systems.zlink.framework.runtime;

import systems.zlink.framework.runtime.backend.*;

import static org.junit.jupiter.api.Assertions.assertEquals;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.registry.AutoConnectType;
import systems.zlink.contracts.service.registry.RegistryState;
import systems.zlink.contracts.service.registry.ServiceKind;
import systems.zlink.contracts.service.registry.ServiceRole;
import systems.zlink.contracts.service.registry.SubjectKind;
import systems.zlink.contracts.service.registry.TopologySource;
import systems.zlink.contracts.service.registry.TopologyState;
import systems.zlink.contracts.service.spot.SpotNodePeerEntry;
import systems.zlink.contracts.service.spot.SpotNodeState;
import systems.zlink.contracts.service.spot.SpotNodeStatus;
import systems.zlink.contracts.service.spot.SpotNodeSubjectEntry;
import systems.zlink.contracts.service.spot.SpotRole;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.framework.monitoring.ZLinkRuntimeEventDispatcher;
import systems.zlink.framework.monitoring.ZLinkRegistryEvent;
import systems.zlink.framework.monitoring.ZLinkRegistryEventKind;
import systems.zlink.framework.monitoring.ZLinkSocketEvent;
import systems.zlink.framework.monitoring.ZLinkSocketEventKind;
import systems.zlink.framework.monitoring.ZLinkSpotEvent;
import systems.zlink.framework.monitoring.ZLinkSpotEventKind;
import systems.zlink.framework.spots.ZLinkSpotKind;
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
    void socketMonitoring_mapsNativeEventsAndAppliesConfiguredFilter() {
        DefaultZLinkMonitoringOptions options = new DefaultZLinkMonitoringOptions();
        options.addSocketEvents(
            "profile",
            ZLinkSocketEventKind.CONNECTED,
            ZLinkSocketEventKind.HANDSHAKE_FAILED,
            ZLinkSocketEventKind.PEER_ADMISSION_CHANGED);
        FakeSocket socket = new FakeSocket("profile");
        FakeMonitoringBackend backend = new FakeMonitoringBackend();
        ZLinkRuntimeEventDispatcher dispatcher = new ZLinkRuntimeEventDispatcher();
        List<ZLinkSocketEvent> events = new ArrayList<>();
        dispatcher.register(ZLinkSocketEvent.class, events::add);

        try (ZLinkMonitoringRuntime ignored = new ZLinkMonitoringRuntime(
                 options,
                 backend,
                 Map.of("profile", socket),
                 dispatcher)) {
            backend.monitor.emit(monitorEvent("ACCEPTED"));
            backend.monitor.emit(monitorEvent("LISTENING"));
            backend.monitor.emit(monitorEvent("HANDSHAKE_FAILED_PROTOCOL"));
            backend.monitor.emit(monitorEvent("PEER_WEIGHT_CHANGED"));
            backend.monitor.emit(monitorEvent("DISCONNECTED"));
        }

        assertEquals(List.of(
                ZLinkSocketEventKind.CONNECTED,
                ZLinkSocketEventKind.CONNECTED,
                ZLinkSocketEventKind.HANDSHAKE_FAILED,
                ZLinkSocketEventKind.PEER_ADMISSION_CHANGED),
            events.stream().map(ZLinkSocketEvent::event).toList());
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
            spotNode.subjects = List.of(new SpotNodeSubjectEntry(
                SpotRole.PUB,
                "room-1",
                SubjectKind.TOPIC,
                1,
                1,
                10));
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

    private static ZLinkBackendSocketMonitorEvent monitorEvent(String event) {
        return new ZLinkBackendSocketMonitorEvent(
            event,
            Optional.empty(),
            "tcp://127.0.0.1:7000",
            "tcp://127.0.0.1:7100");
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
            return new ZLinkBackendRegistryStatus(RegistryState.ACTIVE, entryCount);
        }

        @Override
        public List<ZLinkBackendRegistryServiceSummaryEntry> serviceSummary(
            ZLinkBackendRegistryQueryFilter filter) {
            return List.of(new ZLinkBackendRegistryServiceSummaryEntry(
                AutoConnectType.CLIENT_SERVER,
                ServiceRole.DEALER,
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
                AutoConnectType.CLIENT_SERVER,
                RoutingId.from("profile-" + entryCount),
                ServiceKind.SOCKET,
                ServiceRole.ROUTER,
                "profile",
                "inproc://profile-" + entryCount,
                TopologySource.MANUAL,
                TopologyState.READY,
                1,
                1,
                0,
                12,
                ZLinkSpotKind.INVALID));
        }

        @Override
        public List<ZLinkBackendRegistryMemberPeerEntry> memberPeers(String channelName) {
            return List.of(new ZLinkBackendRegistryMemberPeerEntry(
                AutoConnectType.CLIENT_SERVER,
                ServiceRole.ROUTER,
                channelName,
                "inproc://" + channelName,
                RoutingId.from(channelName + "-peer"),
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
        List<SpotNodeSubjectEntry> subjects = List.of();

        @Override
        public RoutingId routingId() {
            return RoutingId.from("play");
        }

        @Override
        public void setRoutingId(RoutingId routingId) {
        }

        @Override
        public void setPublisherRoutingId(RoutingId routingId) {
        }

        @Override
        public void setSubscriberRoutingId(RoutingId routingId) {
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
        public void connectPeer(RoutingId peerRid, String endpoint) {
        }

        @Override
        public ZLinkBackendSpotRouteBridge createRouteBridge() {
            return new FakeSpotRouteBridge();
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
        public ZLinkBackendActorRef createActor(String actorId, Message createRequest) {
            if (createRequest != null) {
                createRequest.close();
            }
            return null;
        }

        @Override
        public ZLinkBackendActorRef actorLookup(String actorId) {
            return null;
        }

        @Override
        public java.util.concurrent.CompletionStage<ZLinkBackendActorJoinResult> joinActor(
            ZLinkBackendActorRef actor,
            RoutingId targetNodeRid,
            RoutingId targetSpotRid,
            List<Message> parts,
            java.time.Duration timeout) {
            return java.util.concurrent.CompletableFuture.failedFuture(
                new UnsupportedOperationException("join actor is not used by this test"));
        }

        @Override
        public java.util.concurrent.CompletionStage<ZLinkBackendActorJoinEntrySpotResult> joinActorEntrySpot(
            ZLinkBackendActorRef actor,
            RoutingId targetNodeRid,
            Message request,
            java.time.Duration timeout) {
            return java.util.concurrent.CompletableFuture.failedFuture(
                new UnsupportedOperationException("join entry spot is not used by this test"));
        }

        @Override
        public java.util.concurrent.CompletionStage<Void> destroyActor(
            ZLinkBackendActorRef actor,
            java.time.Duration timeout) {
            return java.util.concurrent.CompletableFuture.failedFuture(
                new UnsupportedOperationException("destroy actor is not used by this test"));
        }

        @Override
        public boolean sendActorBoundSession(
            ZLinkBackendActorRef actor,
            List<Message> parts,
            SendFlags flags) {
            return false;
        }

        @Override
        public boolean forwardActorBoundSession(
            ZLinkBackendActorRef actor,
            RoutingId sourceNodeRid,
            RoutingId sourceSessionRid,
            List<Message> parts,
            SendFlags flags) {
            return false;
        }

        @Override
        public void bindRemoteActorBoundSession(
            ZLinkBackendActorRef actor,
            RoutingId sourceNodeRid,
            RoutingId sourceSessionRid) {
        }

        @Override
        public void closeActorBoundSession(
            ZLinkBackendActorRef actor,
            java.time.Duration timeout) {
        }

        @Override
        public SpotNodeStatus status() {
            return new SpotNodeStatus(
                "play",
                "inproc://play",
                RoutingId.from("play"),
                SpotNodeState.READY,
                0,
                0,
                0,
                subjects.size(),
                subjects.size(),
                0,
                0,
                0,
                10);
        }

        @Override
        public List<SpotNodePeerEntry> peers() {
            return List.of();
        }

        @Override
        public List<SpotNodeSubjectEntry> subjects() {
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

    private static final class FakeSpotRouteBridge implements ZLinkBackendSpotRouteBridge {
        @Override
        public void attachRouterChannel(
            String channelName,
            ZLinkBackendRouterSocket router) {
            throw unused();
        }

        @Override
        public boolean send(
            String channelName,
            RoutingId targetNodeRid,
            RoutingId targetSpotRid,
            List<Message> parts,
            SendFlags flags) {
            throw unused();
        }

        @Override
        public boolean request(
            String channelName,
            RoutingId targetNodeRid,
            RoutingId targetSpotRid,
            List<Message> parts,
            ZLinkBackendRequestCallback callback,
            SendFlags flags,
            java.time.Duration timeout) {
            throw unused();
        }

        @Override
        public boolean handleRouterReceived(
            String channelName,
            RoutingId sourceNodeRid,
            long requestSeq,
            List<Message> parts) {
            throw unused();
        }

        @Override
        public int drain() {
            throw unused();
        }

        @Override
        public String name() {
            return "spot-route-bridge";
        }

        @Override
        public void close() {
        }

        private static UnsupportedOperationException unused() {
            return new UnsupportedOperationException("spot route bridge is not used by this test");
        }
    }
}
