package systems.zlink.framework.testkit;

import java.time.Duration;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
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
import systems.zlink.framework.runtime.backend.ZLinkBackendSpotNodePeerEntry;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpotNodeStatus;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpotNodeSubjectEntry;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpotRoute;
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
import systems.zlink.framework.spots.ZLinkSpotKind;

public final class FakeZLinkBackendAdapterFactory implements ZLinkBackendAdapterFactory {
    private final List<String> calls = new ArrayList<>();
    private final List<FakeStreamSocket> streams = new ArrayList<>();
    private final List<FakeSpot> spots = new ArrayList<>();

    public List<String> calls() {
        return List.copyOf(calls);
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
        if (streams.isEmpty()) {
            throw new IllegalStateException("no fake stream socket is available");
        }
        streams.get(0).dispatchPacket(
            RoutingId.from("fake-session"),
            Message.from(encodeStreamHeader(2, 0, packetName, Optional.of(requestSeq))),
            Message.from(payload));
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

    @Override
    public ZLinkChannelBackendAdapter createChannelAdapter(ZLinkBackendAdapterOptions options) {
        calls.add("factory.channel");
        return new FakeChannelBackendAdapter(calls);
    }

    @Override
    public ZLinkRegistryBackendAdapter createRegistryAdapter(ZLinkBackendAdapterOptions options) {
        calls.add("factory.registry");
        return new FakeRegistryBackendAdapter(calls);
    }

    @Override
    public ZLinkSpotBackendAdapter createSpotAdapter(ZLinkBackendAdapterOptions options) {
        calls.add("factory.spot");
        return new FakeSpotBackendAdapter(calls, spots);
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
        byte[] name = packetName.getBytes(StandardCharsets.UTF_8);
        ByteBuffer buffer = ByteBuffer.allocate(
            3 + (requestSeq.isPresent() ? Long.BYTES : 0) + 1 + name.length);
        buffer.put((byte) kind);
        buffer.put((byte) codec);
        buffer.put((byte) (requestSeq.isPresent() ? 0x01 : 0));
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

        FakeChannelBackendAdapter(List<String> calls) {
            this.calls = calls;
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
            return new FakeDiscovery(calls, "discovery." + channelName);
        }

        @Override
        public ZLinkBackendDealerSocket createDealerSocket(ZLinkBackendContext context) {
            return new FakeDealerSocket(calls, "dealer");
        }

        @Override
        public ZLinkBackendRouterSocket createRouterSocket(ZLinkBackendContext context) {
            return new FakeRouterSocket(calls, "router");
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

        FakeSpotBackendAdapter(List<String> calls, List<FakeSpot> spots) {
            this.calls = calls;
            this.spots = spots;
        }

        @Override
        public ZLinkBackendSpotNode createSpotNode(ZLinkBackendContext context, ZLinkBackendSpotNodeMode mode) {
            return new FakeSpotNode(calls, spots);
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
        FakeDiscovery(List<String> calls, String name) {
            super(calls, name);
        }

        @Override
        public void connectRegistry(String endpoint) {
            record("connectRegistry." + endpoint);
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
        public ZLinkBackendActorRoute resolveActor(String actorId) {
            return new ZLinkBackendActorRoute(RoutingId.from("node"), actorId);
        }

        @Override
        public List<ZLinkBackendRegistryTopologyEntry> memberPeers() {
            if ("discovery.egress-discovery".equals(name())) {
                return List.of(new ZLinkBackendRegistryTopologyEntry(
                    "ingress-discovery",
                    RoutingId.from("discovery-route-peer"),
                    "ROUTER",
                    "inproc://ingress-discovery"));
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
        FakeRouterSocket(List<String> calls, String name) {
            super(calls, name);
        }

        @Override public void attachDiscovery(ZLinkBackendDiscovery discovery) { record("attachDiscovery." + discovery.name()); }
        @Override public void setRoutingId(RoutingId routingId) { record("setRoutingId"); }
        @Override public ZLinkBackendReceived recv(ZLinkBackendRecvMode mode) { return null; }
        @Override public boolean send(RoutingId routingId, List<Message> parts, SendFlags flags) { record("send." + routingId + "." + firstPart(parts)); return true; }
        @Override public boolean request(RoutingId routingId, List<Message> parts, ZLinkBackendRequestCallback callback, SendFlags flags, Duration timeout) {
            record("request." + routingId + "." + firstPart(parts));
            callback.handle(new ZLinkBackendReceived(
                Optional.empty(),
                Optional.empty(),
                Optional.empty(),
                List.of(Message.from("reply".getBytes(StandardCharsets.UTF_8)))));
            return true;
        }
        @Override public void reply(RoutingId routingId, long requestSeq, List<Message> parts) { record("reply"); }
        @Override public boolean sendToSpot(RoutingId targetNodeRid, RoutingId spotRid, List<Message> parts, SendFlags flags) {
            record("sendToSpot." + targetNodeRid + "." + spotRid + "." + firstPart(parts));
            return true;
        }
        @Override public boolean requestToSpot(RoutingId targetNodeRid, RoutingId spotRid, List<Message> parts, ZLinkBackendRequestCallback callback, SendFlags flags, Duration timeout) {
            record("requestToSpot." + targetNodeRid + "." + spotRid + "." + firstPart(parts));
            callback.handle(new ZLinkBackendReceived(
                Optional.empty(),
                Optional.empty(),
                Optional.empty(),
                List.of(Message.from("reply".getBytes(StandardCharsets.UTF_8)))));
            return true;
        }
    }

    private static final class FakePublisherSocket extends FakeSocket implements ZLinkBackendPublisherSocket {
        FakePublisherSocket(List<String> calls, String name) {
            super(calls, name);
        }

        @Override public void attachDiscovery(ZLinkBackendDiscovery discovery) { record("attachDiscovery." + discovery.name()); }
        @Override public boolean publish(String topic, List<Message> parts, SendFlags flags) { record("publish." + topic); return true; }
    }

    private static final class FakeSubscriberSocket extends FakeConnectableSocket implements ZLinkBackendSubscriberSocket {
        FakeSubscriberSocket(List<String> calls, String name) {
            super(calls, name);
        }

        @Override public void attachDiscovery(ZLinkBackendDiscovery discovery) { record("attachDiscovery." + discovery.name()); }
        @Override public void setSubscription(String topic) { record("setSubscription." + topic); }
        @Override public ZLinkBackendTopicMessage subscribe(ZLinkBackendRecvMode mode) { return null; }
    }

    private static final class FakeRegistry extends FakeBackendObject implements ZLinkBackendRegistry {
        FakeRegistry(List<String> calls) {
            super(calls, "registry");
        }

        @Override public void bind(String pubEndpoint, String routerEndpoint) { record("bind." + pubEndpoint + "." + routerEndpoint); }
        @Override public void connectPeer(String pubEndpoint, String routerEndpoint) { record("connectPeer." + pubEndpoint); }
        @Override public ZLinkBackendRegistryStatus status() { record("status"); return new ZLinkBackendRegistryStatus("BOUND", 1); }
        @Override public List<ZLinkBackendRegistryServiceSummaryEntry> serviceSummary(ZLinkBackendRegistryQueryFilter filter) {
            record("serviceSummary." + filter.channelName().orElse("*"));
            return List.of(new ZLinkBackendRegistryServiceSummaryEntry("profile", "CLIENT_SERVER", 2));
        }
        @Override public List<ZLinkBackendRegistryTopologyEntry> topology(ZLinkBackendRegistryQueryFilter filter) {
            record("topology." + filter.channelName().orElse("*"));
            return List.of(new ZLinkBackendRegistryTopologyEntry(
                "profile",
                RoutingId.from("profile-server"),
                "SERVER",
                "inproc://profile-server"));
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
                filter.channelName().orElse("profile"),
                RoutingId.from("registry-route-peer"),
                "ROUTER",
                "tcp://127.0.0.1:7100"));
        }
    }

    private static final class FakeSpotNode extends FakeBackendObject implements ZLinkBackendSpotNode {
        private int nextSpotId = 1;
        private final List<FakeSpot> spots;

        FakeSpotNode(List<String> calls, List<FakeSpot> spots) {
            super(calls, "spotNode");
            this.spots = spots;
        }

        @Override public RoutingId routingId() { return RoutingId.from("spot-node"); }
        @Override public void setRoutingId(RoutingId routingId) { record("setRoutingId"); }
        @Override public void setRouterBind(String endpoint) { record("setRouterBind." + endpoint); }
        @Override public void setPubBind(String endpoint) { record("setPubBind." + endpoint); }
        @Override public void attachDiscovery(ZLinkBackendDiscovery discovery) { record("attachDiscovery." + discovery.name()); }
        @Override public void connectPeer(String endpoint) { record("connectPeer." + endpoint); }
        @Override public void connectRouterChannelPeer(String channelName, String endpoint) { record("connectRouterChannelPeer." + channelName + "." + endpoint); }
        @Override public void attachSpotRouteChannelDiscovery(String channelName, ZLinkBackendDiscovery discovery) { record("attachSpotRouteChannelDiscovery." + channelName + "." + discovery.name()); }
        @Override public void attachChannelDealer(ZLinkBackendDiscovery discovery, ZLinkBackendDealerSocket dealer) { record("attachChannelDealer"); }
        @Override public void attachChannelDealerManual(String channelName, ZLinkBackendDealerSocket dealer) { record("attachChannelDealerManual." + channelName); }
        @Override public ZLinkBackendSpot createSpot() {
            record("createSpot");
            FakeSpot spot = new FakeSpot(calls(), "spot." + nextSpotId++);
            spots.add(spot);
            return spot;
        }
        @Override public ZLinkBackendSpot entrySpot() {
            record("entrySpot");
            FakeSpot spot = new FakeSpot(calls(), "entrySpot");
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
            return CompletableFuture.completedFuture(new ZLinkBackendActorJoinResult(
                ZLinkBackendRequestResult.OK,
                0,
                new ZLinkBackendActorRef(targetNodeRid, actor.actorId(), actor.epoch() + 1),
                targetSpotRid,
                1,
                0,
                List.of(Message.from("joined".getBytes(StandardCharsets.UTF_8)))));
        }
        @Override public CompletionStage<ZLinkBackendActorJoinEntrySpotResult> joinActorEntrySpot(ZLinkBackendActorRef actor, RoutingId targetNodeRid, Duration timeout) {
            record("joinActorEntrySpot." + actor.actorId() + "." + targetNodeRid);
            return CompletableFuture.completedFuture(new ZLinkBackendActorJoinEntrySpotResult(
                ZLinkBackendRequestResult.OK,
                new ZLinkBackendActorRef(targetNodeRid, actor.actorId(), actor.epoch() + 1),
                targetNodeRid,
                1,
                0));
        }
        @Override public boolean sendActorBoundSession(ZLinkBackendActorRef actor, List<Message> parts, SendFlags flags) {
            record("sendActorBoundSession." + actor.actorId() + "." + firstPart(parts));
            return true;
        }
        @Override public ZLinkBackendSpotNodeStatus status() { return null; }
        @Override public List<ZLinkBackendSpotNodePeerEntry> peers() { return List.of(); }
        @Override public List<ZLinkBackendSpotNodeSubjectEntry> subjects() { return List.of(); }
    }

    private static final class FakeSpot extends FakeBackendObject implements ZLinkBackendSpot {
        private final Deque<ZLinkBackendActorJoinRequest> actorJoins = new ArrayDeque<>();
        private final Deque<ZLinkBackendActorLifecycleEvent> actorLifecycles = new ArrayDeque<>();
        private ZLinkBackendSpotDispatchHandler dispatchHandler;

        FakeSpot(List<String> calls, String name) {
            super(calls, name);
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
        @Override public ZLinkBackendTopicMessage subscribe(ZLinkBackendRecvMode mode) { return null; }
        @Override public ZLinkBackendReceived recvRoute(ZLinkBackendRecvMode mode) { return null; }
        @Override public boolean sendToChannel(String channelName, List<Message> parts, SendFlags flags) { record("sendToChannel." + channelName); return true; }
        @Override public boolean requestToChannel(String channelName, List<Message> parts, ZLinkBackendRequestCallback callback, SendFlags flags, Duration timeout) { record("requestToChannel." + channelName); return true; }
        @Override public boolean publish(String topic, List<Message> parts, SendFlags flags) { record("publish." + topic); return true; }
        @Override public boolean sendToSpot(RoutingId targetNodeRid, RoutingId spotRid, List<Message> parts, SendFlags flags) { record("sendToSpot." + targetNodeRid + "." + spotRid); return true; }
        @Override public boolean requestToSpot(RoutingId targetNodeRid, RoutingId spotRid, List<Message> parts, ZLinkBackendRequestCallback callback, SendFlags flags, Duration timeout) {
            record("requestToSpot." + targetNodeRid + "." + spotRid);
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
        @Override public boolean reply(RoutingId routingId, long requestSeq, String packetName, List<Message> parts, SendFlags flags) {
            record("reply." + routingId + "." + requestSeq + "." + packetName + "." + firstPart(parts));
            return true;
        }
        @Override public void attachActorGateway(ZLinkBackendSpotNode node) { record("attachActorGateway." + node.name()); }
        @Override public ZLinkBackendActorBindOperation bindActor(RoutingId sessionRid, ZLinkBackendActorRef actor) { record("bindActor." + actor.actorId()); return timeout -> CompletableFuture.completedFuture(null); }
        @Override public ZLinkBackendActorUnbindOperation unbindActor(RoutingId sessionRid, String actorId) { record("unbindActor." + actorId); return timeout -> CompletableFuture.completedFuture(null); }
        @Override public boolean sendBoundActor(RoutingId sessionRid, String actorId, List<Message> parts, SendFlags flags) { record("sendBoundActor." + actorId); return true; }
        @Override public boolean relayBoundActor(RoutingId sessionRid, String actorId, String packetName, Optional<Long> requestSeq, List<Message> parts, SendFlags flags) { record("relayBoundActor." + actorId + "." + packetName); return true; }

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
}
