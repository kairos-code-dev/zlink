package systems.zlink.framework.testkit;

import java.time.Duration;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.framework.runtime.ZLinkBackendActorBindOperation;
import systems.zlink.framework.runtime.ZLinkBackendActorJoinRequest;
import systems.zlink.framework.runtime.ZLinkBackendActorRef;
import systems.zlink.framework.runtime.ZLinkBackendActorRoute;
import systems.zlink.framework.runtime.ZLinkBackendActorUnbindOperation;
import systems.zlink.framework.runtime.ZLinkBackendAdapterFactory;
import systems.zlink.framework.runtime.ZLinkBackendAdapterOptions;
import systems.zlink.framework.runtime.ZLinkBackendAutoConnectType;
import systems.zlink.framework.runtime.ZLinkBackendContext;
import systems.zlink.framework.runtime.ZLinkBackendDealerSocket;
import systems.zlink.framework.runtime.ZLinkBackendDiscovery;
import systems.zlink.framework.runtime.ZLinkBackendDiscoveryRoute;
import systems.zlink.framework.runtime.ZLinkBackendObject;
import systems.zlink.framework.runtime.ZLinkBackendPublisherSocket;
import systems.zlink.framework.runtime.ZLinkBackendReceived;
import systems.zlink.framework.runtime.ZLinkBackendRecvMode;
import systems.zlink.framework.runtime.ZLinkBackendRegistry;
import systems.zlink.framework.runtime.ZLinkBackendRegistryQueryClient;
import systems.zlink.framework.runtime.ZLinkBackendRegistryQueryFilter;
import systems.zlink.framework.runtime.ZLinkBackendRegistryServiceSummaryEntry;
import systems.zlink.framework.runtime.ZLinkBackendRegistryStatus;
import systems.zlink.framework.runtime.ZLinkBackendRegistryTopologyEntry;
import systems.zlink.framework.runtime.ZLinkBackendRequestCallback;
import systems.zlink.framework.runtime.ZLinkBackendRouterSocket;
import systems.zlink.framework.runtime.ZLinkBackendSocket;
import systems.zlink.framework.runtime.ZLinkBackendSocketMonitor;
import systems.zlink.framework.runtime.ZLinkBackendSocketMonitorHandler;
import systems.zlink.framework.runtime.ZLinkBackendSocketMonitorEvent;
import systems.zlink.framework.runtime.ZLinkBackendSpot;
import systems.zlink.framework.runtime.ZLinkBackendSpotNodeMode;
import systems.zlink.framework.runtime.ZLinkBackendSpotNode;
import systems.zlink.framework.runtime.ZLinkBackendSpotNodePeerEntry;
import systems.zlink.framework.runtime.ZLinkBackendSpotNodeStatus;
import systems.zlink.framework.runtime.ZLinkBackendSpotNodeSubjectEntry;
import systems.zlink.framework.runtime.ZLinkBackendSpotRoute;
import systems.zlink.framework.runtime.ZLinkBackendStreamErrorHandler;
import systems.zlink.framework.runtime.ZLinkBackendStreamPacketHandler;
import systems.zlink.framework.runtime.ZLinkBackendStreamSocket;
import systems.zlink.framework.runtime.ZLinkBackendSubscriberSocket;
import systems.zlink.framework.runtime.ZLinkBackendTopicMessage;
import systems.zlink.framework.runtime.ZLinkChannelBackendAdapter;
import systems.zlink.framework.runtime.ZLinkMonitoringBackendAdapter;
import systems.zlink.framework.runtime.ZLinkRegistryBackendAdapter;
import systems.zlink.framework.runtime.ZLinkSpotBackendAdapter;
import systems.zlink.framework.runtime.ZLinkStreamBackendAdapter;

public final class FakeZLinkBackendAdapterFactory implements ZLinkBackendAdapterFactory {
    private final List<String> calls = new ArrayList<>();
    private final List<FakeStreamSocket> streams = new ArrayList<>();

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

    public void dispatchStreamTransportError(int nativeCode, String message) {
        if (streams.isEmpty()) {
            throw new IllegalStateException("no fake stream socket is available");
        }
        streams.get(0).dispatchTransportError(
            RoutingId.from("fake-session"),
            nativeCode,
            message);
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
        return new FakeSpotBackendAdapter(calls);
    }

    @Override
    public ZLinkStreamBackendAdapter createStreamAdapter(ZLinkBackendAdapterOptions options) {
        calls.add("factory.stream");
        return new FakeStreamBackendAdapter(calls, streams);
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

        FakeSpotBackendAdapter(List<String> calls) {
            this.calls = calls;
        }

        @Override
        public ZLinkBackendSpotNode createSpotNode(ZLinkBackendContext context, ZLinkBackendSpotNodeMode mode) {
            return new FakeSpotNode(calls);
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
            return new ZLinkBackendSpotRoute(RoutingId.from("node"), spotRid);
        }

        @Override
        public ZLinkBackendActorRoute resolveActor(String actorId) {
            return new ZLinkBackendActorRoute(RoutingId.from("node"), actorId);
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
        @Override public boolean send(RoutingId routingId, List<Message> parts, SendFlags flags) { record("send"); return true; }
        @Override public boolean request(RoutingId routingId, List<Message> parts, ZLinkBackendRequestCallback callback, SendFlags flags, Duration timeout) { record("request"); return true; }
        @Override public void reply(RoutingId routingId, long requestSeq, List<Message> parts) { record("reply"); }
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
            return List.of(new ZLinkBackendRegistryTopologyEntry("profile", "SERVER", "inproc://profile-server"));
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
            return List.of(new ZLinkBackendRegistryTopologyEntry("profile", "SERVER", "tcp://127.0.0.1:7100"));
        }
    }

    private static final class FakeSpotNode extends FakeBackendObject implements ZLinkBackendSpotNode {
        private int nextSpotId = 1;

        FakeSpotNode(List<String> calls) {
            super(calls, "spotNode");
        }

        @Override public RoutingId routingId() { return RoutingId.from("spot-node"); }
        @Override public void setRoutingId(RoutingId routingId) { record("setRoutingId"); }
        @Override public void setRouterBind(String endpoint) { record("setRouterBind." + endpoint); }
        @Override public void setPubBind(String endpoint) { record("setPubBind." + endpoint); }
        @Override public void attachDiscovery(ZLinkBackendDiscovery discovery) { record("attachDiscovery." + discovery.name()); }
        @Override public void connectPeer(String endpoint) { record("connectPeer." + endpoint); }
        @Override public void attachChannelDealer(ZLinkBackendDiscovery discovery, ZLinkBackendDealerSocket dealer) { record("attachChannelDealer"); }
        @Override public void attachChannelDealerManual(String channelName, ZLinkBackendDealerSocket dealer) { record("attachChannelDealerManual." + channelName); }
        @Override public ZLinkBackendSpot createSpot() {
            record("createSpot");
            return new FakeSpot(calls(), "spot." + nextSpotId++);
        }
        @Override public ZLinkBackendSpot entrySpot() { return new FakeSpot(List.of(), "entrySpot"); }
        @Override public ZLinkBackendActorRef createActor(String actorId) {
            record("createActor." + actorId);
            return new ZLinkBackendActorRef(routingId(), actorId, 0);
        }
        @Override public ZLinkBackendActorRef actorLookup(String actorId) {
            record("actorLookup." + actorId);
            return new ZLinkBackendActorRef(routingId(), actorId, 0);
        }
        @Override public boolean sendActorBoundSession(ZLinkBackendActorRef actor, List<Message> parts, SendFlags flags) { record("sendActorBoundSession." + actor.actorId()); return true; }
        @Override public ZLinkBackendSpotNodeStatus status() { return null; }
        @Override public List<ZLinkBackendSpotNodePeerEntry> peers() { return List.of(); }
        @Override public List<ZLinkBackendSpotNodeSubjectEntry> subjects() { return List.of(); }
    }

    private static final class FakeSpot extends FakeBackendObject implements ZLinkBackendSpot {
        FakeSpot(List<String> calls, String name) {
            super(calls, name);
        }

        @Override public RoutingId routingId() { return RoutingId.from(name()); }
        @Override public void setRoutingId(RoutingId routingId) { record("setRoutingId"); }
        @Override public void setSubscription(String topic) { record("setSubscription." + topic); }
        @Override public ZLinkBackendTopicMessage subscribe(ZLinkBackendRecvMode mode) { return null; }
        @Override public ZLinkBackendReceived recvRoute(ZLinkBackendRecvMode mode) { return null; }
        @Override public boolean sendToChannel(String channelName, List<Message> parts, SendFlags flags) { record("sendToChannel." + channelName); return true; }
        @Override public boolean requestToChannel(String channelName, List<Message> parts, ZLinkBackendRequestCallback callback, SendFlags flags, Duration timeout) { record("requestToChannel." + channelName); return true; }
        @Override public boolean publish(String topic, List<Message> parts, SendFlags flags) { record("publish." + topic); return true; }
        @Override public ZLinkBackendActorJoinRequest recvActorJoin(ZLinkBackendRecvMode mode) { return null; }
    }

    private static final class FakeStreamSocket extends FakeSocket implements ZLinkBackendStreamSocket {
        private ZLinkBackendStreamPacketHandler packetHandler;
        private ZLinkBackendStreamErrorHandler errorHandler;

        FakeStreamSocket(List<String> calls) {
            super(calls, "stream");
        }

        @Override public void onPacket(ZLinkBackendStreamPacketHandler handler) { packetHandler = handler; record("onPacket"); }
        @Override public void onTransportError(ZLinkBackendStreamErrorHandler handler) { errorHandler = handler; record("onTransportError"); }
        @Override public boolean send(RoutingId routingId, List<Message> parts, SendFlags flags) { record("send"); return true; }
        @Override public void attachActorGateway(ZLinkBackendSpotNode node) { record("attachActorGateway." + node.name()); }
        @Override public ZLinkBackendActorBindOperation bindActor(RoutingId sessionRid, ZLinkBackendActorRef actor) { record("bindActor." + actor.actorId()); return timeout -> CompletableFuture.completedFuture(null); }
        @Override public ZLinkBackendActorUnbindOperation unbindActor(RoutingId sessionRid, String actorId) { record("unbindActor." + actorId); return timeout -> CompletableFuture.completedFuture(null); }
        @Override public boolean sendBoundActor(RoutingId sessionRid, String actorId, List<Message> parts, SendFlags flags) { record("sendBoundActor." + actorId); return true; }

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
