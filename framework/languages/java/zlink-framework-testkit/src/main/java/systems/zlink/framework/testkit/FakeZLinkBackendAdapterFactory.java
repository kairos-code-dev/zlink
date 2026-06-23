package systems.zlink.framework.testkit;

import java.time.Duration;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.EnumSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.registry.AutoConnectType;
import systems.zlink.contracts.service.registry.RegistryState;
import systems.zlink.contracts.service.registry.ServiceKind;
import systems.zlink.contracts.service.registry.ServiceRole;
import systems.zlink.contracts.service.registry.TopologySource;
import systems.zlink.contracts.service.registry.TopologyState;
import systems.zlink.contracts.service.spot.SpotNodePeerEntry;
import systems.zlink.contracts.service.spot.SpotNodeState;
import systems.zlink.contracts.service.spot.SpotNodeStatus;
import systems.zlink.contracts.service.spot.SpotNodeSubjectEntry;
import systems.zlink.contracts.service.spot.SpotRouteBridgeEndpointOptions;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorBindOperation;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorJoinEntrySpotResult;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorJoinResult;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorJoinRequest;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorLifecycleEvent;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorLifecycleEventKind;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorLifecycleInfo;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorReceived;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorRef;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorRoute;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorUnbindOperation;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterFactory;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterOptions;
import systems.zlink.framework.runtime.backend.ZLinkBackendAutoConnectType;
import systems.zlink.framework.runtime.backend.ZLinkBackendContext;
import systems.zlink.framework.runtime.backend.ZLinkBackendDealerSocket;
import systems.zlink.framework.runtime.backend.ZLinkBackendDiscovery;
import systems.zlink.framework.runtime.backend.ZLinkBackendDiscoveryRoute;
import systems.zlink.framework.runtime.backend.ZLinkBackendObject;
import systems.zlink.framework.runtime.backend.ZLinkBackendPublisherSocket;
import systems.zlink.framework.runtime.backend.ZLinkBackendReceived;
import systems.zlink.framework.runtime.backend.ZLinkBackendRecvMode;
import systems.zlink.framework.runtime.backend.ZLinkBackendRegistry;
import systems.zlink.framework.runtime.backend.ZLinkBackendRegistryMemberPeerEntry;
import systems.zlink.framework.runtime.backend.ZLinkBackendRegistryQueryClient;
import systems.zlink.framework.runtime.backend.ZLinkBackendRegistryQueryFilter;
import systems.zlink.framework.runtime.backend.ZLinkBackendRegistryServiceSummaryEntry;
import systems.zlink.framework.runtime.backend.ZLinkBackendRegistryStatus;
import systems.zlink.framework.runtime.backend.ZLinkBackendRegistryTopologyEntry;
import systems.zlink.framework.runtime.backend.ZLinkBackendRequestCallback;
import systems.zlink.framework.runtime.backend.ZLinkBackendRequestResult;
import systems.zlink.framework.runtime.backend.ZLinkBackendRouterSocket;
import systems.zlink.framework.runtime.backend.ZLinkBackendSocket;
import systems.zlink.framework.runtime.backend.ZLinkBackendSocketMonitor;
import systems.zlink.framework.runtime.backend.ZLinkBackendSocketMonitorHandler;
import systems.zlink.framework.runtime.backend.ZLinkBackendSocketMonitorEvent;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpot;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpotDispatchEvent;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpotDispatchHandler;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpotDispatchInfo;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpotNodeMode;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpotNode;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpotRoute;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpotRouteBridge;
import systems.zlink.framework.runtime.backend.ZLinkBackendStreamErrorHandler;
import systems.zlink.framework.runtime.backend.ZLinkBackendStreamPacketHandler;
import systems.zlink.framework.runtime.backend.ZLinkBackendStreamSocket;
import systems.zlink.framework.runtime.backend.ZLinkBackendSubscriberSocket;
import systems.zlink.framework.runtime.backend.ZLinkBackendTopicMessage;
import systems.zlink.framework.runtime.backend.ZLinkChannelBackendAdapter;
import systems.zlink.framework.runtime.backend.ZLinkMonitoringBackendAdapter;
import systems.zlink.framework.runtime.backend.ZLinkRegistryBackendAdapter;
import systems.zlink.framework.runtime.backend.ZLinkSpotBackendAdapter;
import systems.zlink.framework.runtime.backend.ZLinkStreamBackendAdapter;
import systems.zlink.framework.runtime.actors.ZLinkActorSpotRoutePackets;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeaderCodec;
import systems.zlink.framework.streams.ZLinkStreamCodec;
import systems.zlink.framework.streams.ZLinkStreamHeader;
import systems.zlink.framework.streams.ZLinkStreamHeaderFlag;
import systems.zlink.framework.streams.ZLinkStreamMessageKind;
import systems.zlink.framework.spots.ZLinkSpotKind;

public final class FakeZLinkBackendAdapterFactory implements ZLinkBackendAdapterFactory {
    private final List<String> calls = new ArrayList<>();
    private final List<FakeStreamSocket> streams = new ArrayList<>();
    private final List<FakeSpot> spots = new ArrayList<>();
    private final List<FakeRouterSocket> routers = new ArrayList<>();
    private boolean roomsDiscoveryDelaysRoutingId;

    public List<String> calls() {
        return List.copyOf(calls);
    }

    public void delayRoomsDiscoveryRoutingIdUntilSecondSnapshot() {
        roomsDiscoveryDelaysRoutingId = true;
    }

    public void dispatchStreamPacket(String packetName, String payload) {
        if (streams.isEmpty()) {
            throw new IllegalStateException("no fake stream socket is available");
        }
        streams.get(0).dispatchPacket(
            RoutingId.from("fake-session"),
            Message.from(packetName),
            Message.from(payload));
    }

    public void dispatchStreamRequest(String packetName, String payload, long requestSeq) {
        dispatchStreamRequest(packetName, Message.from(payload), requestSeq, 0);
    }

    public void dispatchStreamRequest(
        String packetName,
        Message payload,
        long requestSeq,
        int flags) {
        if (streams.isEmpty()) {
            throw new IllegalStateException("no fake stream socket is available");
        }
        streams.get(0).dispatchPacket(
            RoutingId.from("fake-session"),
            Message.from(encodeStreamHeader(2, 0, packetName, Optional.of(requestSeq), flags)),
            payload);
    }

    public void dispatchStreamTransportError(int nativeCode, String message) {
        if (streams.isEmpty()) {
            throw new IllegalStateException("no fake stream socket is available");
        }
        streams.get(0).dispatchTransportError(
            RoutingId.from("fake-session"),
            nativeCode,
            message);
    }

    public void dispatchEntrySpotActorJoinReadable(String actorId) {
        dispatchEntrySpotActorJoinReadable(actorId, null, "join");
    }

    public void dispatchEntrySpotActorJoinReadable(String actorId, String packetName, String payload) {
        FakeSpot entrySpot = spots.stream()
            .filter(spot -> "entrySpot".equals(spot.name()))
            .findFirst()
            .orElseThrow(() -> new IllegalStateException("no fake entry spot is available"));
        entrySpot.enqueueActorJoin(actorId, packetName, payload);
        entrySpot.dispatchActorJoinReadable();
    }

    public void dispatchEntrySpotActorMessage(String actorId, String packetName, String payload) {
        FakeSpot entrySpot = spots.stream()
            .filter(spot -> "entrySpot".equals(spot.name()))
            .findFirst()
            .orElseThrow(() -> new IllegalStateException("no fake entry spot is available"));
        entrySpot.dispatchActorMessage(actorId, packetName, payload, Optional.empty());
    }

    public void dispatchEntrySpotActorRequest(
        String actorId,
        String packetName,
        String payload,
        long requestSeq) {
        FakeSpot entrySpot = spots.stream()
            .filter(spot -> "entrySpot".equals(spot.name()))
            .findFirst()
            .orElseThrow(() -> new IllegalStateException("no fake entry spot is available"));
        entrySpot.dispatchActorMessage(actorId, packetName, payload, Optional.of(requestSeq));
    }

    public void dispatchEntrySpotActorStreamRequest(
        String actorId,
        String packetName,
        String payload,
        long requestSeq) {
        FakeSpot entrySpot = spots.stream()
            .filter(spot -> "entrySpot".equals(spot.name()))
            .findFirst()
            .orElseThrow(() -> new IllegalStateException("no fake entry spot is available"));
        entrySpot.dispatchActorMessage(
            actorId,
            encodeStreamHeader(2, 0, packetName, Optional.of(requestSeq)),
            payload,
            Optional.empty());
    }

    public void dispatchEntrySpotActorLifecycleLeft(String actorId) {
        FakeSpot entrySpot = spots.stream()
            .filter(spot -> "entrySpot".equals(spot.name()))
            .findFirst()
            .orElseThrow(() -> new IllegalStateException("no fake entry spot is available"));
        entrySpot.enqueueActorLifecycleLeft(actorId);
        entrySpot.dispatchActorLifecycleReadable();
    }

    public void dispatchEntrySpotActorLifecycleJoined(String actorId, RoutingId spotRid) {
        FakeSpot entrySpot = spots.stream()
            .filter(spot -> "entrySpot".equals(spot.name()))
            .findFirst()
            .orElseThrow(() -> new IllegalStateException("no fake entry spot is available"));
        entrySpot.enqueueActorLifecycleJoined(actorId, spotRid);
        entrySpot.dispatchActorLifecycleReadable();
    }

    public void dispatchSpotRoute(String packetName, String payload) {
        FakeSpot spot = firstUserSpot();
        spot.enqueueRoute(packetName, payload, Optional.empty());
        spot.dispatchRouteReadable();
    }

    public void dispatchSpotRequest(String packetName, String payload, long requestSeq) {
        FakeSpot spot = firstUserSpot();
        spot.enqueueRoute(packetName, payload, Optional.of(requestSeq));
        spot.dispatchRouteReadable();
    }

    public void dispatchRouteMeshSpotRequest(
        RoutingId sourceRid,
        RoutingId targetSpotRid,
        List<Message> spotParts,
        long requestSeq) {
        if (routers.isEmpty()) {
            throw new IllegalStateException("no fake route socket is available");
        }
        routers.get(0).enqueueReceived(sourceRid, Optional.of(requestSeq), copyMessages(spotParts));
    }

    public void dispatchSpotSubscription(String topic, String packetName, String payload) {
        FakeSpot spot = firstUserSpot();
        spot.enqueueSubscription(topic, packetName, payload);
        spot.dispatchSubscribeReadable();
    }

    public void dispatchSpotActorJoinReadable(String actorId, String packetName, String payload) {
        FakeSpot spot = firstUserSpot();
        spot.enqueueActorJoin(actorId, packetName, payload);
        spot.dispatchActorJoinReadable();
    }

    public List<String> spotReplies() {
        return spots.stream()
            .flatMap(spot -> spot.replies().stream())
            .toList();
    }

    private FakeSpot firstUserSpot() {
        return spots.stream()
            .filter(spot -> spot.name().startsWith("spot."))
            .findFirst()
            .orElseThrow(() -> new IllegalStateException("no fake user spot is available"));
    }

    @Override
    public ZLinkChannelBackendAdapter createChannelAdapter(ZLinkBackendAdapterOptions options) {
        calls.add("factory.channel");
        return new FakeChannelBackendAdapter(calls, routers, this);
    }

    @Override
    public ZLinkRegistryBackendAdapter createRegistryAdapter(ZLinkBackendAdapterOptions options) {
        calls.add("factory.registry");
        return new FakeRegistryBackendAdapter(calls);
    }

    @Override
    public ZLinkSpotBackendAdapter createSpotAdapter(ZLinkBackendAdapterOptions options) {
        calls.add("factory.spot");
        return new FakeSpotBackendAdapter(calls, spots, this);
    }

    @Override
    public ZLinkStreamBackendAdapter createStreamAdapter(ZLinkBackendAdapterOptions options) {
        calls.add("factory.stream");
        return new FakeStreamBackendAdapter(calls, streams);
    }

    private static byte[] encodeStreamHeader(
        int kind,
        int codec,
        String packetName,
        Optional<Long> requestSeq) {
        return encodeStreamHeader(kind, codec, packetName, requestSeq, 0);
    }

    private static byte[] encodeStreamHeader(
        int kind,
        int codec,
        String packetName,
        Optional<Long> requestSeq,
        int additionalFlags) {
        if (additionalFlags == 0) {
            return ZLinkStreamHeaderCodec.encode(new ZLinkStreamHeader(
                ZLinkStreamMessageKind.fromValue(kind),
                ZLinkStreamCodec.fromValue(codec),
                EnumSet.noneOf(ZLinkStreamHeaderFlag.class),
                requestSeq,
                packetName,
                Map.of()));
        }
        return encodeRawStreamHeader(kind, codec, packetName, requestSeq, additionalFlags);
    }

    private static byte[] encodeRawStreamHeader(
        int kind,
        int codec,
        String packetName,
        Optional<Long> requestSeq,
        int additionalFlags) {
        byte[] name = packetName.getBytes(StandardCharsets.UTF_8);
        ByteBuffer buffer = ByteBuffer.allocate(
            3 + (requestSeq.isPresent() ? Long.BYTES : 0) + 1 + name.length);
        buffer.put((byte) kind);
        buffer.put((byte) codec);
        buffer.put((byte) ((requestSeq.isPresent() ? 0x01 : 0) | additionalFlags));
        requestSeq.ifPresent(buffer::putLong);
        buffer.put((byte) name.length);
        buffer.put(name);
        return buffer.array();
    }

    @Override
    public ZLinkMonitoringBackendAdapter createMonitoringAdapter(ZLinkBackendAdapterOptions options) {
        calls.add("factory.monitoring");
        return new FakeMonitoringBackendAdapter(calls);
    }

    private abstract static class FakeBackendObject implements ZLinkBackendObject {
        private final List<String> calls;
        private final String name;

        FakeBackendObject(List<String> calls, String name) {
            this.calls = calls;
            this.name = name;
            calls.add("create." + name);
        }

        @Override
        public String name() {
            return name;
        }

        @Override
        public void close() {
            calls.add("close." + name);
        }

        void record(String call) {
            calls.add(name + "." + call);
        }

        List<String> calls() {
            return calls;
        }
    }

    private static final class FakeChannelBackendAdapter implements ZLinkChannelBackendAdapter {
        private final List<String> calls;
        private final List<FakeRouterSocket> routers;
        private final FakeZLinkBackendAdapterFactory owner;

        FakeChannelBackendAdapter(
            List<String> calls,
            List<FakeRouterSocket> routers,
            FakeZLinkBackendAdapterFactory owner) {
            this.calls = calls;
            this.routers = routers;
            this.owner = owner;
        }

        @Override
        public ZLinkBackendContext createContext() {
            return new FakeContext(calls);
        }

        @Override
        public ZLinkBackendDiscovery createDiscovery(
            ZLinkBackendContext context,
            ZLinkBackendAutoConnectType autoConnectType,
            String channelName) {
            return new FakeDiscovery(calls, "discovery." + channelName, owner);
        }

        @Override
        public ZLinkBackendDealerSocket createDealerSocket(ZLinkBackendContext context) {
            return new FakeDealerSocket(calls, "dealer");
        }

        @Override
        public ZLinkBackendRouterSocket createRouterSocket(ZLinkBackendContext context) {
            FakeRouterSocket router = new FakeRouterSocket(calls, "router");
            routers.add(router);
            return router;
        }

        @Override
        public ZLinkBackendPublisherSocket createPublisherSocket(ZLinkBackendContext context) {
            return new FakePublisherSocket(calls, "publisher");
        }

        @Override
        public ZLinkBackendSubscriberSocket createSubscriberSocket(ZLinkBackendContext context) {
            return new FakeSubscriberSocket(calls, "subscriber");
        }
    }

    private static final class FakeRegistryBackendAdapter implements ZLinkRegistryBackendAdapter {
        private final List<String> calls;

        FakeRegistryBackendAdapter(List<String> calls) {
            this.calls = calls;
        }

        @Override
        public ZLinkBackendRegistry createRegistry(ZLinkBackendContext context) {
            return new FakeRegistry(calls);
        }

        @Override
        public ZLinkBackendRegistryQueryClient createRegistryQueryClient(ZLinkBackendContext context) {
            return new FakeRegistryQueryClient(calls);
        }
    }

    private static final class FakeSpotBackendAdapter implements ZLinkSpotBackendAdapter {
        private final List<String> calls;
        private final List<FakeSpot> spots;
        private final FakeZLinkBackendAdapterFactory owner;

        FakeSpotBackendAdapter(
            List<String> calls,
            List<FakeSpot> spots,
            FakeZLinkBackendAdapterFactory owner) {
            this.calls = calls;
            this.spots = spots;
            this.owner = owner;
        }

        @Override
        public ZLinkBackendSpotNode createSpotNode(ZLinkBackendContext context, ZLinkBackendSpotNodeMode mode) {
            return new FakeSpotNode(calls, spots, owner);
        }
    }

    private static final class FakeStreamBackendAdapter implements ZLinkStreamBackendAdapter {
        private final List<String> calls;
        private final List<FakeStreamSocket> streams;

        FakeStreamBackendAdapter(List<String> calls, List<FakeStreamSocket> streams) {
            this.calls = calls;
            this.streams = streams;
        }

        @Override
        public ZLinkBackendStreamSocket createStreamSocket(ZLinkBackendContext context) {
            FakeStreamSocket stream = new FakeStreamSocket(calls);
            streams.add(stream);
            return stream;
        }
    }

    private static final class FakeMonitoringBackendAdapter implements ZLinkMonitoringBackendAdapter {
        private final List<String> calls;

        FakeMonitoringBackendAdapter(List<String> calls) {
            this.calls = calls;
        }

        @Override
        public ZLinkBackendSocketMonitor openSocketMonitor(ZLinkBackendSocket socket) {
            calls.add("monitoring.open." + socket.name());
            return new FakeSocketMonitor(calls);
        }
    }

    private static final class FakeContext extends FakeBackendObject implements ZLinkBackendContext {
        FakeContext(List<String> calls) {
            super(calls, "context");
        }

        @Override
        public void shutdown() {
            record("shutdown");
        }
    }

    private static final class FakeDiscovery extends FakeBackendObject implements ZLinkBackendDiscovery {
        private final FakeZLinkBackendAdapterFactory owner;
        private int memberPeerSnapshots;

        FakeDiscovery(List<String> calls, String name, FakeZLinkBackendAdapterFactory owner) {
            super(calls, name);
            this.owner = owner;
        }

        @Override
        public void connectRegistry(String endpoint) {
            record("connectRegistry." + endpoint);
        }

        @Override
        public void bindRoute(long kind, byte[] key, byte[] value) {
            record("bindRoute." + kind);
        }

        @Override
        public ZLinkBackendDiscoveryRoute resolveRoute(long kind, byte[] key) {
            return new ZLinkBackendDiscoveryRoute(Optional.empty(), Optional.empty());
        }

        @Override
        public ZLinkBackendSpotRoute resolveSpot(RoutingId spotRid) {
            return new ZLinkBackendSpotRoute(
                RoutingId.from("node"),
                spotRid,
                ZLinkSpotKind.USER);
        }

        @Override
        public void setSpotOwnerSyncEnabled(boolean enabled) {
            record("setSpotOwnerSyncEnabled." + enabled);
        }

        @Override
        public ZLinkBackendActorRoute resolveActor(String actorId) {
            return new ZLinkBackendActorRoute(RoutingId.from("node"), actorId);
        }

        @Override
        public List<ZLinkBackendRegistryMemberPeerEntry> memberPeers() {
            if ("discovery.egress-discovery".equals(name())) {
                return List.of(new ZLinkBackendRegistryMemberPeerEntry(
                    AutoConnectType.ROUTE_MESH,
                    ServiceRole.ROUTER,
                    "ingress-discovery",
                    "inproc://ingress-discovery",
                    RoutingId.from("discovery-route-peer"),
                    1,
                    1));
            }
            if ("discovery.rooms".equals(name())) {
                memberPeerSnapshots++;
                return List.of(new ZLinkBackendRegistryMemberPeerEntry(
                    AutoConnectType.SPOT_MESH,
                    ServiceRole.ROUTER,
                    "rooms",
                    "inproc://rooms-router-peer",
                    owner.roomsDiscoveryDelaysRoutingId && memberPeerSnapshots == 1
                        ? RoutingId.from(new byte[0])
                        : RoutingId.from("rooms-node-2"),
                    1,
                    1));
            }
            return List.of();
        }
    }

    private abstract static class FakeSocket extends FakeBackendObject implements ZLinkBackendSocket {
        FakeSocket(List<String> calls, String name) {
            super(calls, name);
        }

        @Override
        public void bind(String endpoint) {
            record("bind." + endpoint);
        }
    }

    private abstract static class FakeConnectableSocket extends FakeSocket {
        FakeConnectableSocket(List<String> calls, String name) {
            super(calls, name);
        }

        public void connect(String endpoint) {
            record("connect." + endpoint);
        }

        public void disconnect(String endpoint) {
            record("disconnect." + endpoint);
        }
    }

    private static final class FakeDealerSocket extends FakeConnectableSocket implements ZLinkBackendDealerSocket {
        FakeDealerSocket(List<String> calls, String name) {
            super(calls, name);
        }

        @Override public void attachDiscovery(ZLinkBackendDiscovery discovery) { record("attachDiscovery." + discovery.name()); }
        @Override public void setChannelName(String channelName) { record("setChannelName." + channelName); }
        @Override public boolean send(List<Message> parts, SendFlags flags) { record("send." + firstPart(parts)); return true; }
        @Override public boolean request(List<Message> parts, ZLinkBackendRequestCallback callback, SendFlags flags, Duration timeout) {
            record("request." + firstPart(parts));
            callback.handle(new ZLinkBackendReceived(
                Optional.empty(),
                Optional.empty(),
                Optional.empty(),
                List.of(Message.from("reply".getBytes(StandardCharsets.UTF_8)))));
            return true;
        }
        @Override public ZLinkBackendReceived recv(ZLinkBackendRecvMode mode) { return null; }
    }

    private static final class FakeRouterSocket extends FakeConnectableSocket implements ZLinkBackendRouterSocket {
        private final Deque<ZLinkBackendReceived> received = new ArrayDeque<>();

        FakeRouterSocket(List<String> calls, String name) {
            super(calls, name);
        }

        void enqueueReceived(
            RoutingId sourceRid,
            Optional<Long> requestSeq,
            List<Message> parts) {
            received.add(new ZLinkBackendReceived(
                Optional.of(sourceRid),
                Optional.empty(),
                requestSeq,
                parts,
                replyParts -> record("reply." + sourceRid + "." + (replyParts.isEmpty()
                    ? ""
                    : firstPart(replyParts)))));
        }

        @Override public void attachDiscovery(ZLinkBackendDiscovery discovery) { record("attachDiscovery." + discovery.name()); }
        @Override public void setChannelName(String channelName) { record("setChannelName." + channelName); }
        @Override public void setRoutingId(RoutingId routingId) { record("setRoutingId"); }
        @Override public ZLinkBackendReceived recv(ZLinkBackendRecvMode mode) { return received.pollFirst(); }
        @Override public boolean send(RoutingId routingId, List<Message> parts, SendFlags flags) { record("send." + routingId + "." + firstPart(parts)); return true; }
        @Override public boolean request(RoutingId routingId, List<Message> parts, ZLinkBackendRequestCallback callback, SendFlags flags, Duration timeout) {
            record("request." + routingId + "." + firstPart(parts));
            if (isRoutedActorJoinRequest(parts)) {
                ZLinkActorSpotRoutePackets.JoinRequest request =
                    ZLinkActorSpotRoutePackets.decodeJoinRequest(parts.get(1));
                List<Message> routeReply;
                Message joinedPayload = Message.from("joined".getBytes(StandardCharsets.UTF_8));
                Message rejectedPayload = Message.from(new byte[0]);
                Message joinReply = null;
                try {
                    boolean accepted = !request.actorId().contains("reject");
                    joinReply = ZLinkActorSpotRoutePackets.encodeJoinReply(
                        accepted,
                        new ZLinkBackendActorRef(
                            routingId,
                            request.actorId(),
                            request.actorGeneration() + 1),
                        accepted ? joinedPayload : rejectedPayload);
                    routeReply = List.of(Message.from(joinReply));
                } finally {
                    if (joinReply != null) {
                        joinReply.close();
                    }
                    joinedPayload.close();
                    rejectedPayload.close();
                }
                callback.handle(new ZLinkBackendReceived(
                    Optional.empty(),
                    Optional.empty(),
                    Optional.empty(),
                    routeReply));
                return true;
            }
            if (isRoutedBoundSessionSendRequest(parts)) {
                callback.handle(new ZLinkBackendReceived(
                    Optional.empty(),
                    Optional.empty(),
                    Optional.empty(),
                    List.of()));
                return true;
            }
            Message reply = Message.from("reply".getBytes(StandardCharsets.UTF_8));
            try {
                callback.handle(new ZLinkBackendReceived(
                    Optional.empty(),
                    Optional.empty(),
                    Optional.empty(),
                    List.of(Message.from(reply))));
            } finally {
                reply.close();
            }
            return true;
        }
        @Override public void reply(RoutingId routingId, long requestSeq, List<Message> parts) { record("reply"); }
        private static boolean isRoutedActorJoinRequest(List<Message> parts) {
            return parts.size() >= 2
                && ZLinkActorSpotRoutePackets.JOIN_SPOT_PACKET_NAME.equals(firstPart(parts));
        }

        private static boolean isRoutedBoundSessionSendRequest(List<Message> parts) {
            return parts.size() >= 1
                && ZLinkActorSpotRoutePackets.BOUND_SESSION_SEND_PACKET_NAME.equals(firstPart(parts));
        }
    }

    private static final class FakePublisherSocket extends FakeSocket implements ZLinkBackendPublisherSocket {
        FakePublisherSocket(List<String> calls, String name) {
            super(calls, name);
        }

        @Override public void attachDiscovery(ZLinkBackendDiscovery discovery) { record("attachDiscovery." + discovery.name()); }
        @Override public void setChannelName(String channelName) { record("setChannelName." + channelName); }
        @Override public boolean publish(String topic, List<Message> parts, SendFlags flags) { record("publish." + topic + "." + firstPart(parts)); return true; }
    }

    private static final class FakeSubscriberSocket extends FakeConnectableSocket implements ZLinkBackendSubscriberSocket {
        FakeSubscriberSocket(List<String> calls, String name) {
            super(calls, name);
        }

        @Override public void attachDiscovery(ZLinkBackendDiscovery discovery) { record("attachDiscovery." + discovery.name()); }
        @Override public void setChannelName(String channelName) { record("setChannelName." + channelName); }
        @Override public void setSubscription(String topic) { record("setSubscription." + topic); }
        @Override public ZLinkBackendTopicMessage subscribe(ZLinkBackendRecvMode mode) { return null; }
    }

    private static final class FakeRegistry extends FakeBackendObject implements ZLinkBackendRegistry {
        FakeRegistry(List<String> calls) {
            super(calls, "registry");
        }

        @Override public void setId(int registryId) { record("setId." + registryId); }
        @Override public void bind(String pubEndpoint, String routerEndpoint) { record("bind." + pubEndpoint + "." + routerEndpoint); }
        @Override public void connectPeer(String pubEndpoint, String routerEndpoint) { record("connectPeer." + pubEndpoint); }
        @Override public ZLinkBackendRegistryStatus status() { record("status"); return new ZLinkBackendRegistryStatus(RegistryState.ACTIVE, 1); }
        @Override public List<ZLinkBackendRegistryServiceSummaryEntry> serviceSummary(ZLinkBackendRegistryQueryFilter filter) {
            record("serviceSummary." + filter.channelName().orElse("*"));
            return List.of(new ZLinkBackendRegistryServiceSummaryEntry(
                AutoConnectType.CLIENT_SERVER,
                ServiceRole.DEALER,
                "profile",
                2,
                0,
                2,
                0,
                0,
                11));
        }
        @Override public List<ZLinkBackendRegistryTopologyEntry> topology(ZLinkBackendRegistryQueryFilter filter) {
            record("topology." + filter.channelName().orElse("*"));
            return List.of(new ZLinkBackendRegistryTopologyEntry(
                AutoConnectType.CLIENT_SERVER,
                RoutingId.from("profile-server"),
                ServiceKind.SOCKET,
                ServiceRole.ROUTER,
                "profile",
                "inproc://profile-server",
                TopologySource.MANUAL,
                TopologyState.READY,
                1,
                1,
                0,
                12,
                ZLinkSpotKind.INVALID));
        }
        @Override public List<ZLinkBackendRegistryMemberPeerEntry> memberPeers(String channelName) {
            record("memberPeers." + channelName);
            return List.of(new ZLinkBackendRegistryMemberPeerEntry(
                AutoConnectType.CLIENT_SERVER,
                ServiceRole.ROUTER,
                channelName,
                "inproc://" + channelName + "-member",
                RoutingId.from(channelName + "-member"),
                4,
                5));
        }
    }

    private static final class FakeRegistryQueryClient extends FakeBackendObject implements ZLinkBackendRegistryQueryClient {
        FakeRegistryQueryClient(List<String> calls) {
            super(calls, "registryQueryClient");
        }

        @Override public void connect(String endpoint) { record("connect." + endpoint); }
        @Override public List<ZLinkBackendRegistryServiceSummaryEntry> serviceSummary(ZLinkBackendRegistryQueryFilter filter) { return List.of(); }
        @Override public List<ZLinkBackendRegistryTopologyEntry> topology(ZLinkBackendRegistryQueryFilter filter) {
            record("topology." + filter.channelName().orElse("*"));
            return List.of(new ZLinkBackendRegistryTopologyEntry(
                AutoConnectType.CLIENT_SERVER,
                RoutingId.from("registry-route-peer"),
                ServiceKind.SOCKET,
                ServiceRole.ROUTER,
                filter.channelName().orElse("profile"),
                "tcp://127.0.0.1:7100",
                TopologySource.REGISTRY,
                TopologyState.READY,
                1,
                1,
                0,
                13,
                ZLinkSpotKind.INVALID));
        }
    }

    private static final class FakeSpotNode extends FakeBackendObject implements ZLinkBackendSpotNode {
        private int nextSpotId = 1;
        private final List<FakeSpot> spots;
        private final FakeZLinkBackendAdapterFactory owner;
        private RoutingId routingId = RoutingId.from("spot-node");

        FakeSpotNode(
            List<String> calls,
            List<FakeSpot> spots,
            FakeZLinkBackendAdapterFactory owner) {
            super(calls, "spotNode");
            this.spots = spots;
            this.owner = owner;
        }

        @Override public RoutingId routingId() { return routingId; }
        @Override public void setRoutingId(RoutingId routingId) { this.routingId = routingId; record("setRoutingId"); }
        @Override public void setRouterBind(String endpoint) { record("setRouterBind." + endpoint); }
        @Override public void setPubBind(String endpoint) { record("setPubBind." + endpoint); }
        @Override public void attachDiscovery(ZLinkBackendDiscovery discovery) { record("attachDiscovery." + discovery.name()); }
        @Override public void connectPeer(String endpoint) { record("connectPeer." + endpoint); }
        @Override public void connectPeer(RoutingId peerRid, String endpoint) { record("connectPeer." + peerRid + "." + endpoint); }
        @Override public ZLinkBackendSpotRouteBridge createRouteBridge() {
            record("createRouteBridge");
            return new FakeSpotRouteBridge(calls());
        }
        @Override public ZLinkBackendSpot createSpot() {
            record("createSpot");
            FakeSpot spot = new FakeSpot(calls(), "spot." + nextSpotId++, owner);
            spots.add(spot);
            return spot;
        }
        @Override public ZLinkBackendSpot entrySpot() {
            record("entrySpot");
            FakeSpot spot = new FakeSpot(calls(), "entrySpot", owner);
            spots.add(spot);
            return spot;
        }
        @Override public ZLinkBackendActorRef createActor(String actorId) {
            record("createActor." + actorId);
            return new ZLinkBackendActorRef(routingId(), actorId, 0);
        }
        @Override public ZLinkBackendActorRef actorLookup(String actorId) {
            record("actorLookup." + actorId);
            return new ZLinkBackendActorRef(routingId(), actorId, 0);
        }
        @Override public CompletionStage<ZLinkBackendActorJoinResult> joinActor(
            ZLinkBackendActorRef actor,
            RoutingId targetNodeRid,
            RoutingId targetSpotRid,
            List<Message> parts,
            Duration timeout) {
            record("joinActor." + actor.actorId() + "." + targetNodeRid + "." + targetSpotRid);
            RoutingId joinedNodeRid = targetSpotRid.toString().contains("native-remote")
                ? RoutingId.from("native-remote-node")
                : targetNodeRid;
            return CompletableFuture.completedFuture(new ZLinkBackendActorJoinResult(
                ZLinkBackendRequestResult.OK,
                0,
                new ZLinkBackendActorRef(joinedNodeRid, actor.actorId(), actor.epoch() + 1),
                targetSpotRid,
                1,
                0,
                List.of(Message.from("joined".getBytes(StandardCharsets.UTF_8)))));
        }
        @Override public CompletionStage<ZLinkBackendActorJoinEntrySpotResult> joinActorEntrySpot(ZLinkBackendActorRef actor, RoutingId targetNodeRid, Message request, Duration timeout) {
            record("joinActorEntrySpot." + actor.actorId() + "." + targetNodeRid);
            return CompletableFuture.completedFuture(new ZLinkBackendActorJoinEntrySpotResult(
                ZLinkBackendRequestResult.OK,
                0,
                new ZLinkBackendActorRef(targetNodeRid, actor.actorId(), actor.epoch() + 1),
                targetNodeRid,
                targetNodeRid,
                1,
                0,
                List.of(Message.from("entry-joined".getBytes(StandardCharsets.UTF_8)))));
        }
        @Override public CompletionStage<Void> destroyActor(ZLinkBackendActorRef actor, Duration timeout) {
            record("destroyActor." + actor.actorId());
            return CompletableFuture.completedFuture(null);
        }
        @Override public boolean sendActorBoundSession(ZLinkBackendActorRef actor, List<Message> parts, SendFlags flags) {
            record("sendActorBoundSession." + actor.actorId() + "." + firstPart(parts));
            return true;
        }
        @Override public boolean forwardActorBoundSession(
            ZLinkBackendActorRef actor,
            RoutingId sourceNodeRid,
            RoutingId sourceSessionRid,
            List<Message> parts,
            SendFlags flags) {
            record("forwardActorBoundSession."
                + actor.actorId()
                + "."
                + sourceNodeRid
                + "."
                + sourceSessionRid
                + "."
                + firstPart(parts));
            return true;
        }
        @Override public void closeActorBoundSession(ZLinkBackendActorRef actor, Duration timeout) {
            record("closeActorBoundSession." + actor.actorId());
        }
        @Override public SpotNodeStatus status() {
            return new SpotNodeStatus(
                "fake",
                "inproc://fake-spot",
                RoutingId.from("fake-node"),
                SpotNodeState.READY,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0);
        }
        @Override public List<SpotNodePeerEntry> peers() { return List.of(); }
        @Override public List<SpotNodeSubjectEntry> subjects() { return List.of(); }
    }

    private static final class FakeSpotRouteBridge extends FakeBackendObject implements ZLinkBackendSpotRouteBridge {
        private final java.util.Map<String, RoutingId> targetNodes = new java.util.HashMap<>();

        FakeSpotRouteBridge(List<String> calls) {
            super(calls, "spotRouteBridge");
        }

        @Override public void attachDealerChannel(String channelName, ZLinkBackendDealerSocket dealer, SpotRouteBridgeEndpointOptions options) {
            record("bridge.attachDealerChannel." + channelName);
        }

        @Override public void attachRouterChannel(String channelName, ZLinkBackendRouterSocket router, SpotRouteBridgeEndpointOptions options) {
            record("bridge.attachRouterChannel." + channelName);
        }

        @Override public void setTargetNode(String channelName, RoutingId targetNodeRid) {
            targetNodes.put(channelName, targetNodeRid);
            record("bridge.setTargetNode." + channelName + "." + targetNodeRid);
        }

        @Override public boolean send(String channelName, RoutingId targetSpotRid, List<Message> parts, SendFlags flags) {
            record("bridge.send." + channelName + "." + targetSpotRid + "." + firstPart(parts));
            return true;
        }

        @Override public boolean request(String channelName, RoutingId targetSpotRid, List<Message> parts, ZLinkBackendRequestCallback callback, SendFlags flags, Duration timeout) {
            record("bridge.request." + channelName + "." + targetSpotRid + "." + firstPart(parts));
            if (isRoutedActorJoinRequest(parts)) {
                ZLinkActorSpotRoutePackets.JoinRequest request =
                    ZLinkActorSpotRoutePackets.decodeJoinRequest(parts.get(1));
                Message joinedPayload = Message.from("joined".getBytes(StandardCharsets.UTF_8));
                Message rejectedPayload = Message.from(new byte[0]);
                Message joinReply = null;
                try {
                    boolean accepted = !request.actorId().contains("reject");
                    joinReply = ZLinkActorSpotRoutePackets.encodeJoinReply(
                        accepted,
                        new ZLinkBackendActorRef(
                            targetNodes.getOrDefault(channelName, RoutingId.from("remote-node")),
                            request.actorId(),
                            request.actorGeneration() + 1),
                        accepted ? joinedPayload : rejectedPayload);
                    callback.handle(new ZLinkBackendReceived(
                        Optional.empty(),
                        Optional.empty(),
                        Optional.empty(),
                        List.of(Message.from(joinReply))));
                    return true;
                } finally {
                    if (joinReply != null) {
                        joinReply.close();
                    }
                    joinedPayload.close();
                    rejectedPayload.close();
                }
            }
            if (isRoutedBoundSessionSendRequest(parts)) {
                callback.handle(new ZLinkBackendReceived(
                    Optional.empty(),
                    Optional.empty(),
                    Optional.empty(),
                    List.of()));
                return true;
            }
            callback.handle(new ZLinkBackendReceived(
                Optional.empty(),
                Optional.empty(),
                Optional.empty(),
                List.of(Message.from("reply".getBytes(StandardCharsets.UTF_8)))));
            return true;
        }

        @Override public boolean handleRouterReceived(String channelName, RoutingId sourceNodeRid, long requestSeq, List<Message> parts) {
            record("bridge.handleRouterReceived." + channelName + "." + firstPart(parts));
            return false;
        }

        private static boolean isRoutedActorJoinRequest(List<Message> parts) {
            return parts.size() >= 2
                && ZLinkActorSpotRoutePackets.JOIN_SPOT_PACKET_NAME.equals(firstPart(parts));
        }

        private static boolean isRoutedBoundSessionSendRequest(List<Message> parts) {
            return parts.size() >= 1
                && ZLinkActorSpotRoutePackets.BOUND_SESSION_SEND_PACKET_NAME.equals(firstPart(parts));
        }

        @Override public void close() {
            record("bridge.close");
        }
    }

    private static final class FakeSpot extends FakeBackendObject implements ZLinkBackendSpot {
        private final FakeZLinkBackendAdapterFactory owner;
        private final Deque<ZLinkBackendActorJoinRequest> actorJoins = new ArrayDeque<>();
        private final Deque<ZLinkBackendActorLifecycleEvent> actorLifecycles = new ArrayDeque<>();
        private final Deque<ZLinkBackendReceived> routes = new ArrayDeque<>();
        private final Deque<ZLinkBackendTopicMessage> subscriptions = new ArrayDeque<>();
        private final List<String> replies = new ArrayList<>();
        private ZLinkBackendSpotDispatchHandler dispatchHandler;

        FakeSpot(
            List<String> calls,
            String name,
            FakeZLinkBackendAdapterFactory owner) {
            super(calls, name);
            this.owner = owner;
        }

        void enqueueActorJoin(String actorId, String packetName, String payload) {
            List<Message> parts = packetName == null
                ? List.of(Message.from(payload.getBytes(StandardCharsets.UTF_8)))
                : List.of(
                    Message.from(packetName.getBytes(StandardCharsets.UTF_8)),
                    Message.from(payload.getBytes(StandardCharsets.UTF_8)));
            actorJoins.add(new ZLinkBackendActorJoinRequest(
                new ZLinkBackendActorRef(RoutingId.from("source-node"), actorId, 1),
                new ZLinkBackendActorRef(RoutingId.from("spot-node"), actorId, 1),
                parts,
                null));
        }

        void dispatchActorJoinReadable() {
            if (dispatchHandler == null) {
                throw new IllegalStateException("fake spot dispatch handler is not registered");
            }
            dispatchHandler.handle(new ZLinkBackendSpotDispatchInfo(
                ZLinkBackendSpotDispatchEvent.ACTOR_JOIN_READABLE,
                List.of()));
        }

        void enqueueActorLifecycleLeft(String actorId) {
            ZLinkBackendActorRef actor =
                new ZLinkBackendActorRef(RoutingId.from("spot-node"), actorId, 1);
            actorLifecycles.add(new ZLinkBackendActorLifecycleEvent(
                ZLinkBackendActorLifecycleEventKind.LEFT,
                new ZLinkBackendActorLifecycleInfo(
                    actor,
                    actor,
                    Optional.of(RoutingId.from("entrySpot")),
                    Optional.empty(),
                    1,
                    0)));
        }

        void enqueueActorLifecycleJoined(String actorId, RoutingId spotRid) {
            ZLinkBackendActorRef actor =
                new ZLinkBackendActorRef(RoutingId.from("spot-node"), actorId, 1);
            actorLifecycles.add(new ZLinkBackendActorLifecycleEvent(
                ZLinkBackendActorLifecycleEventKind.JOINED,
                new ZLinkBackendActorLifecycleInfo(
                    actor,
                    actor,
                    Optional.empty(),
                    Optional.of(spotRid),
                    0,
                    1)));
        }

        void dispatchActorLifecycleReadable() {
            if (dispatchHandler == null) {
                throw new IllegalStateException("fake spot dispatch handler is not registered");
            }
            dispatchHandler.handle(new ZLinkBackendSpotDispatchInfo(
                ZLinkBackendSpotDispatchEvent.ACTOR_LIFECYCLE_READABLE,
                List.of()));
        }

        void enqueueRoute(String packetName, String payload, Optional<Long> requestSeq) {
            routes.add(new ZLinkBackendReceived(
                Optional.of(RoutingId.from("source")),
                Optional.of(RoutingId.from(name())),
                requestSeq,
                List.of(Message.from(packetName), Message.from(payload)),
                replyParts -> replies.add(replyParts.isEmpty()
                    ? ""
                    : replyParts.get(0).toUtf8String())));
        }

        void dispatchRouteReadable() {
            if (dispatchHandler == null) {
                throw new IllegalStateException("fake spot dispatch handler is not registered");
            }
            dispatchHandler.handle(new ZLinkBackendSpotDispatchInfo(
                ZLinkBackendSpotDispatchEvent.ROUTED_READABLE,
                List.of()));
        }

        void enqueueSubscription(String topic, String packetName, String payload) {
            subscriptions.add(new ZLinkBackendTopicMessage(
                Optional.of(RoutingId.from("publisher")),
                topic,
                List.of(Message.from(packetName), Message.from(payload))));
        }

        void dispatchSubscribeReadable() {
            if (dispatchHandler == null) {
                throw new IllegalStateException("fake spot dispatch handler is not registered");
            }
            dispatchHandler.handle(new ZLinkBackendSpotDispatchInfo(
                ZLinkBackendSpotDispatchEvent.SUBSCRIBE_READABLE,
                List.of()));
        }

        List<String> replies() {
            return List.copyOf(replies);
        }

        void dispatchActorMessage(
            String actorId,
            String packetName,
            String payload,
            Optional<Long> requestSeq) {
            dispatchActorMessage(
                actorId,
                packetName.getBytes(StandardCharsets.UTF_8),
                payload,
                requestSeq);
        }

        void dispatchActorMessage(
            String actorId,
            byte[] header,
            String payload,
            Optional<Long> requestSeq) {
            if (dispatchHandler == null) {
                throw new IllegalStateException("fake spot dispatch handler is not registered");
            }
            ZLinkBackendActorRef actor =
                new ZLinkBackendActorRef(RoutingId.from("spot-node"), actorId, 1);
            dispatchHandler.handle(new ZLinkBackendSpotDispatchInfo(
                ZLinkBackendSpotDispatchEvent.ACTOR_READABLE,
                List.of(
                    new ZLinkBackendActorReceived(
                        actor,
                        RoutingId.from("source-node"),
                        RoutingId.from("source-session"),
                        requestSeq,
                        0,
                        Message.from(header),
                        true),
                    new ZLinkBackendActorReceived(
                        actor,
                        RoutingId.from("source-node"),
                        RoutingId.from("source-session"),
                        requestSeq,
                        0,
                        Message.from(payload.getBytes(StandardCharsets.UTF_8)),
                        false))));
        }

        @Override public RoutingId routingId() { return RoutingId.from(name()); }
        @Override public void setRoutingId(RoutingId routingId) { record("setRoutingId"); }
        @Override public void setSubscription(String topic) { record("setSubscription." + topic); }
        @Override public ZLinkBackendTopicMessage subscribe(ZLinkBackendRecvMode mode) { return subscriptions.pollFirst(); }
        @Override public ZLinkBackendReceived recvRoute(ZLinkBackendRecvMode mode) { return routes.pollFirst(); }
        @Override public boolean publish(String topic, List<Message> parts, SendFlags flags) { record("publish." + topic + "." + firstPart(parts)); return true; }
        @Override public boolean sendToSpot(RoutingId targetNodeRid, RoutingId spotRid, List<Message> parts, SendFlags flags) { record("sendToSpot." + targetNodeRid + "." + spotRid + "." + firstPart(parts)); return true; }
        @Override public boolean requestToSpot(RoutingId targetNodeRid, RoutingId spotRid, List<Message> parts, ZLinkBackendRequestCallback callback, SendFlags flags, Duration timeout) {
            record("requestToSpot." + targetNodeRid + "." + spotRid + "." + firstPart(parts));
            callback.handle(new ZLinkBackendReceived(
                Optional.empty(),
                Optional.empty(),
                Optional.empty(),
                List.of(Message.from("reply".getBytes(StandardCharsets.UTF_8)))));
            return true;
        }
        @Override public void onDispatchEvent(ZLinkBackendSpotDispatchHandler handler) {
            dispatchHandler = handler;
            record("onDispatchEvent");
        }
        @Override public ZLinkBackendActorJoinRequest recvActorJoin(ZLinkBackendRecvMode mode) {
            record("recvActorJoin." + mode);
            return actorJoins.pollFirst();
        }
        @Override public void replyActorJoin(
            ZLinkBackendActorJoinRequest request,
            int joinResultCode,
            List<Message> parts) {
            record("replyActorJoin." + request.targetActor().actorId() + "." + joinResultCode);
            record("replyActorJoinPayload." + request.targetActor().actorId() + "." + joinResultCode
                + "." + firstPart(parts));
        }
        @Override public ZLinkBackendActorLifecycleEvent recvActorLifecycle(
            ZLinkBackendRecvMode mode) {
            record("recvActorLifecycle." + mode);
            return actorLifecycles.pollFirst();
        }
    }

    private static final class FakeStreamSocket extends FakeSocket implements ZLinkBackendStreamSocket {
        private ZLinkBackendStreamPacketHandler packetHandler;
        private ZLinkBackendStreamErrorHandler errorHandler;

        FakeStreamSocket(List<String> calls) {
            super(calls, "stream");
        }

        @Override public void onPacket(ZLinkBackendStreamPacketHandler handler) { packetHandler = handler; record("onPacket"); }
        @Override public void onTransportError(ZLinkBackendStreamErrorHandler handler) { errorHandler = handler; record("onTransportError"); }
        @Override public boolean send(RoutingId routingId, List<Message> parts, SendFlags flags) {
            record("send." + routingId + "." + firstPart(parts));
            return true;
        }
        @Override public boolean send(RoutingId routingId, String packetName, List<Message> parts, SendFlags flags) {
            record("send." + routingId + "." + packetName + "." + firstPart(parts));
            return true;
        }
        @Override public boolean send(RoutingId routingId, ZLinkStreamHeader header, List<Message> parts, SendFlags flags) {
            record("send." + routingId + "." + header.packetName() + "." + header.flags() + "." + header.metadata() + "." + firstPart(parts));
            return true;
        }
        @Override public boolean reply(RoutingId routingId, long requestSeq, String packetName, List<Message> parts, SendFlags flags) {
            record("reply." + routingId + "." + requestSeq + "." + packetName + "." + firstPart(parts));
            return true;
        }
        @Override public boolean reply(RoutingId routingId, ZLinkStreamHeader header, List<Message> parts, SendFlags flags) {
            record("reply." + routingId + "." + header.requestSequence().orElse(0L) + "." + header.packetName() + "." + header.flags() + "." + header.metadata() + "." + firstPart(parts));
            return true;
        }
        @Override public void attachActorGateway(ZLinkBackendSpotNode node) { record("attachActorGateway." + node.name()); }
        @Override public ZLinkBackendActorBindOperation bindActor(RoutingId sessionRid, ZLinkBackendActorRef actor) { record("bindActor." + actor.actorId()); return timeout -> CompletableFuture.completedFuture(null); }
        @Override public ZLinkBackendActorUnbindOperation unbindActor(RoutingId sessionRid, String actorId) { record("unbindActor." + actorId); return timeout -> CompletableFuture.completedFuture(null); }
        @Override public boolean sendBoundActor(RoutingId sessionRid, String actorId, List<Message> parts, SendFlags flags) { record("sendBoundActor." + actorId); return true; }
        @Override public boolean relayBoundActor(RoutingId sessionRid, String actorId, ZLinkStreamHeader header, List<Message> parts, SendFlags flags) { record("relayBoundActor." + actorId + "." + header.codec() + "." + header.packetName()); return true; }

        void dispatchPacket(RoutingId routingId, Message header, Message payload) {
            if (packetHandler == null) {
                throw new IllegalStateException("stream packet handler is not registered");
            }
            packetHandler.handle(routingId, header, payload);
        }

        void dispatchTransportError(RoutingId routingId, int nativeCode, String message) {
            if (errorHandler == null) {
                throw new IllegalStateException("stream error handler is not registered");
            }
            errorHandler.handle(routingId, nativeCode, message);
        }
    }

    private static final class FakeSocketMonitor extends FakeBackendObject implements ZLinkBackendSocketMonitor {
        FakeSocketMonitor(List<String> calls) {
            super(calls, "socketMonitor");
        }

        @Override public void onEvent(ZLinkBackendSocketMonitorHandler handler) { record("onEvent"); }
        @Override public ZLinkBackendSocketMonitorEvent recv() { return null; }
    }

    private static String firstPart(List<Message> parts) {
        return parts.isEmpty() ? "" : parts.get(0).toUtf8String();
    }

    private static List<Message> copyMessages(List<Message> parts) {
        return parts.stream()
            .map(Message::from)
            .toList();
    }
}
