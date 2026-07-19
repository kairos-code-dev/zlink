package systems.zlink.framework.runtime;

import systems.zlink.framework.runtime.internal.backend.ZLinkInternalSpotNode;
import systems.zlink.framework.runtime.internal.backend.ZLinkInternalMeshNode;
import systems.zlink.framework.runtime.internal.backend.ZLinkMeshDispatchRecord;

import systems.zlink.framework.runtime.backend.*;

import static org.junit.jupiter.api.Assertions.assertEquals;

import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;
import java.util.function.Consumer;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.spot.MeshNodeState;
import systems.zlink.contracts.service.spot.MeshNodeStatus;
import systems.zlink.contracts.service.spot.MeshPeerEntry;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.framework.locations.ZLinkLocationAutoConnectType;
import systems.zlink.framework.locations.ZLinkLocationOptions;
import systems.zlink.framework.locations.ZLinkLocationRole;
import systems.zlink.framework.locations.ZLinkLocationWriteIntent;
import systems.zlink.framework.locations.ZLinkPeerLocation;
import systems.zlink.framework.monitoring.ZLinkLocationRuntimeEvent;
import systems.zlink.framework.monitoring.ZLinkLocationRuntimeEventKind;
import systems.zlink.framework.monitoring.ZLinkRuntimeEventDispatcher;
import systems.zlink.framework.monitoring.ZLinkSocketEvent;
import systems.zlink.framework.monitoring.ZLinkSocketEventKind;
import systems.zlink.framework.monitoring.ZLinkSpotEvent;
import systems.zlink.framework.monitoring.ZLinkSpotEventKind;
import systems.zlink.framework.runtime.monitoring.DefaultZLinkMonitoringOptions;
import systems.zlink.framework.runtime.monitoring.ZLinkMonitoringRuntime;
import systems.zlink.framework.runtime.locations.ZLinkInMemoryLocationStore;
import systems.zlink.framework.runtime.locations.ZLinkLocationRuntime;
import systems.zlink.framework.runtime.locations.ZLinkLocationRuntimeQueryService;
import systems.zlink.framework.runtime.locations.ZLinkRegisteredLocationStores;

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
    void spotMonitoring_emitsStatusAndPeerSnapshotsFromMeshNode() {
        DefaultZLinkMonitoringOptions options = new DefaultZLinkMonitoringOptions();
        options.addSpotEvents("play", java.time.Duration.ofSeconds(1));
        FakeMeshNode spotNode = new FakeMeshNode();
        ZLinkRuntimeEventDispatcher dispatcher = new ZLinkRuntimeEventDispatcher();
        List<ZLinkSpotEvent> events = new ArrayList<>();
        dispatcher.register(ZLinkSpotEvent.class, event -> {
            events.add(event);
        });

        try (ZLinkMonitoringRuntime runtime = new ZLinkMonitoringRuntime(
                 options,
                 socket -> null,
                 Map.of(),
                 Map.of("play", spotNode),
                 null,
                 dispatcher)) {
            runtime.pollSnapshots();
            runtime.pollSnapshots();
            spotNode.revision = 2L;
            runtime.pollSnapshots();
        }

        assertEquals(List.of(
                ZLinkSpotEventKind.STATUS_CHANGED,
                ZLinkSpotEventKind.PEERS_CHANGED,
                ZLinkSpotEventKind.STATUS_CHANGED),
            events.stream().map(ZLinkSpotEvent::event).toList());
    }

    @Test
    void locationRuntimeMonitoring_emitsTopologyChangedFromRuntimeQuery() throws Exception {
        DefaultZLinkMonitoringOptions options = new DefaultZLinkMonitoringOptions();
        options.addLocationRuntimeEvents("locations", Duration.ofMillis(1));
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore();
        ZLinkLocationOptions locationOptions = new ZLinkLocationOptions();
        locationOptions.setPollingInterval(Duration.ofMillis(1));
        ZLinkRegisteredLocationStores stores = ZLinkRegisteredLocationStores.fromUnified(store);
        ZLinkLocationRuntime locationRuntime = new ZLinkLocationRuntime(
            stores,
            locationOptions.ownerLeaseTtl(),
            locationOptions.heartbeatInterval());
        locationRuntime.start(RoutingId.from("node-a")).toCompletableFuture().get();
        store.updatePeer(peer(locationRuntime.ownerId()), ZLinkLocationWriteIntent.NEW_CLAIM)
            .toCompletableFuture()
            .get();
        ZLinkRuntimeEventDispatcher dispatcher = new ZLinkRuntimeEventDispatcher();
        CompletableFuture<ZLinkLocationRuntimeEvent> topologyChanged = new CompletableFuture<>();
        dispatcher.register(ZLinkLocationRuntimeEvent.class, event -> {
            if (event.event() == ZLinkLocationRuntimeEventKind.TOPOLOGY_CHANGED) {
                topologyChanged.complete(event);
            }
        });

        try (ZLinkMonitoringRuntime runtime = new ZLinkMonitoringRuntime(
                 options,
                 socket -> null,
                 Map.of(),
                 Map.of(),
                 new ZLinkLocationRuntimeQueryService(stores, locationRuntime, locationOptions),
                 dispatcher)) {
            runtime.pollSnapshots();
            ZLinkLocationRuntimeEvent event = topologyChanged.get(2, TimeUnit.SECONDS);

            assertEquals("locations", event.sourceName());
            assertEquals(1, event.topology().size());
            assertEquals("mesh", event.topology().get(0).meshName());
        } finally {
            locationRuntime.close();
        }
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

    private static ZLinkPeerLocation peer(String ownerId) {
        return new ZLinkPeerLocation(
            ZLinkLocationAutoConnectType.ROUTE_MESH,
            "mesh",
            RoutingId.from("node-a"),
            ZLinkLocationRole.ROUTER,
            "tcp://127.0.0.1:6000",
            1,
            false,
            0,
            Map.of(),
            List.of(),
            ownerId,
            0,
            java.time.Instant.EPOCH);
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

    private static final class FakeMeshNode implements ZLinkInternalMeshNode {
        long revision = 1L;

        @Override
        public void setBind(String endpoint) {
        }

        @Override
        public void addChannel(String channelName) {
        }

        @Override
        public void setChannelWeight(String channelName, int weight) {
        }

        @Override
        public void setRoutingId(RoutingId routingId) {
        }

        @Override
        public void start() {
        }

        @Override
        public long connectPeer(String endpoint) {
            return 1L;
        }

        @Override
        public long connectPeer(String endpoint, RoutingId expectedRoutingId) {
            return 1L;
        }

        @Override
        public MeshNodeStatus status() {
            return new MeshNodeStatus(
                MeshNodeState.READY,
                RoutingId.from("play"),
                "play",
                "inproc://play",
                1L,
                revision,
                0,
                0,
                0,
                0,
                0L,
                0L,
                0L,
                0L,
                0L,
                0,
                10L);
        }

        @Override
        public List<MeshPeerEntry> peers() {
            return List.of();
        }

        @Override
        public List<Long> connectionIntentIds() {
            return List.of();
        }

        @Override
        public void startDispatch(Consumer<ZLinkMeshDispatchRecord> receiver) {
        }

        @Override
        public String name() {
            return "play";
        }

        @Override
        public void close() {
        }
    }

    private static final class FakeSpotNode implements ZLinkInternalSpotNode {

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
        public void connectPeer(String endpoint) {
        }

        @Override
        public void connectPeer(RoutingId peerRid, String endpoint) {
        }

        @Override
        public void disconnectPeer(String endpoint) {
        }

        @Override
        public void disconnectPeer(RoutingId peerRid) {
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
        public java.util.concurrent.CompletionStage<List<Message>> leaveActor(
            ZLinkBackendActorRef actor,
            RoutingId currentSpotRid,
            java.time.Duration timeout) {
            return java.util.concurrent.CompletableFuture.failedFuture(
                new UnsupportedOperationException("leave actor is not used by this test"));
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
        public void replyActorNoBind(
            ZLinkBackendActorRef actor,
            RoutingId sourceNodeRid,
            RoutingId sourceSessionRid,
            long requestId,
            int flags,
            List<Message> parts) {
            throw new UnsupportedOperationException("reply actor no-bind is not used by this test");
        }

        @Override
        public boolean sendToActor(
            ZLinkBackendActorRef actor,
            List<Message> parts,
            SendFlags flags) {
            return false;
        }

        @Override
        public java.util.concurrent.CompletionStage<List<Message>> requestToActor(
            ZLinkBackendActorRef actor,
            List<Message> parts,
            SendFlags flags,
            java.time.Duration timeout) {
            return java.util.concurrent.CompletableFuture.failedFuture(
                new UnsupportedOperationException("request to actor is not used by this test"));
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
