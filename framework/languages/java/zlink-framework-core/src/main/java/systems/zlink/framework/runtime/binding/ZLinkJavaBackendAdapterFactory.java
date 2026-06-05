package systems.zlink.framework.runtime.binding;

import java.time.Duration;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.eventing.MonitorEvent;
import systems.zlink.contracts.eventing.SocketMonitor;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.messaging.TopicMessage;
import systems.zlink.contracts.service.discovery.Discovery;
import systems.zlink.contracts.service.registry.AutoConnectType;
import systems.zlink.contracts.service.registry.Registry;
import systems.zlink.contracts.service.registry.RegistryQueryClient;
import systems.zlink.contracts.service.registry.RegistryServiceSummaryFilter;
import systems.zlink.contracts.service.registry.RegistryTopologyFilter;
import systems.zlink.contracts.service.registry.ServiceKind;
import systems.zlink.contracts.service.registry.ServiceRole;
import systems.zlink.contracts.service.registry.TopologySource;
import systems.zlink.contracts.service.registry.TopologyState;
import systems.zlink.contracts.service.spot.ActorBindOperation;
import systems.zlink.contracts.service.spot.ActorJoinCompletion;
import systems.zlink.contracts.service.spot.ActorJoinRequest;
import systems.zlink.contracts.service.spot.ActorJoinSubmitOperation;
import systems.zlink.contracts.service.spot.ActorReceived;
import systems.zlink.contracts.service.spot.ActorRef;
import systems.zlink.contracts.service.spot.ActorUnbindOperation;
import systems.zlink.contracts.service.spot.ReplyOperation;
import systems.zlink.contracts.service.spot.RequestOperation;
import systems.zlink.contracts.service.spot.SendOperation;
import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.contracts.service.spot.SpotActorLifecycleEvent;
import systems.zlink.contracts.service.spot.SpotKind;
import systems.zlink.contracts.service.spot.SpotNode;
import systems.zlink.contracts.service.spot.SpotNodeMode;
import systems.zlink.contracts.sockets.DealerSocket;
import systems.zlink.contracts.sockets.PubSocket;
import systems.zlink.contracts.sockets.RecvFlags;
import systems.zlink.contracts.sockets.RouterSocket;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.sockets.Socket;
import systems.zlink.contracts.sockets.SpotDispatchEvent;
import systems.zlink.contracts.sockets.SpotDispatchInfo;
import systems.zlink.contracts.sockets.StreamSocket;
import systems.zlink.contracts.sockets.SubSocket;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeaderCodec;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorReceived;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorBindOperation;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorJoinEntrySpotResult;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorJoinResult;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorJoinRequest;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorLifecycleEvent;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorLifecycleEventKind;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorLifecycleInfo;
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
import systems.zlink.framework.runtime.backend.ZLinkBackendSocketMonitorEvent;
import systems.zlink.framework.runtime.backend.ZLinkBackendSocketMonitorHandler;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpot;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpotDispatchEvent;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpotDispatchHandler;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpotDispatchInfo;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpotNode;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpotNodeMode;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpotNodePeerEntry;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpotNodeStatus;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpotNodeSubjectEntry;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpotRoute;
import systems.zlink.framework.runtime.backend.ZLinkBackendStreamPacketHandler;
import systems.zlink.framework.runtime.backend.ZLinkBackendStreamErrorHandler;
import systems.zlink.framework.runtime.backend.ZLinkBackendStreamSocket;
import systems.zlink.framework.runtime.backend.ZLinkBackendSubscriberSocket;
import systems.zlink.framework.runtime.backend.ZLinkBackendTopicMessage;
import systems.zlink.framework.runtime.backend.ZLinkChannelBackendAdapter;
import systems.zlink.framework.runtime.backend.ZLinkMonitoringBackendAdapter;
import systems.zlink.framework.runtime.backend.ZLinkRegistryBackendAdapter;
import systems.zlink.framework.runtime.backend.ZLinkSpotBackendAdapter;
import systems.zlink.framework.runtime.backend.ZLinkStreamBackendAdapter;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeaderCodec;
import systems.zlink.framework.spots.ZLinkSpotKind;
import systems.zlink.framework.streams.ZLinkStreamHeader;

public final class ZLinkJavaBackendAdapterFactory implements ZLinkBackendAdapterFactory {
    @Override
    public ZLinkChannelBackendAdapter createChannelAdapter(ZLinkBackendAdapterOptions options) {
        return new JavaChannelBackendAdapter();
    }

    @Override
    public ZLinkRegistryBackendAdapter createRegistryAdapter(ZLinkBackendAdapterOptions options) {
        return new JavaRegistryBackendAdapter();
    }

    @Override
    public ZLinkSpotBackendAdapter createSpotAdapter(ZLinkBackendAdapterOptions options) {
        return new JavaSpotBackendAdapter();
    }

    @Override
    public ZLinkStreamBackendAdapter createStreamAdapter(ZLinkBackendAdapterOptions options) {
        return new JavaStreamBackendAdapter();
    }

    @Override
    public ZLinkMonitoringBackendAdapter createMonitoringAdapter(ZLinkBackendAdapterOptions options) {
        return socket -> new JavaSocketMonitor(((JavaSocketBacked) socket).nativeSocket().monitorOpen());
    }

    private static final class JavaChannelBackendAdapter implements ZLinkChannelBackendAdapter {
        @Override
        public ZLinkBackendContext createContext() {
            return new JavaContext(Zlink.createContext());
        }

        @Override
        public ZLinkBackendDiscovery createDiscovery(
            ZLinkBackendContext context,
            ZLinkBackendAutoConnectType autoConnectType,
            String channelName) {
            return new JavaDiscovery(nativeContext(context).createDiscovery(map(autoConnectType), channelName));
        }

        @Override public ZLinkBackendDealerSocket createDealerSocket(ZLinkBackendContext context) { return new JavaDealerSocket(nativeContext(context).createDealerSocket()); }
        @Override public ZLinkBackendRouterSocket createRouterSocket(ZLinkBackendContext context) { return new JavaRouterSocket(nativeContext(context).createRouterSocket()); }
        @Override public ZLinkBackendPublisherSocket createPublisherSocket(ZLinkBackendContext context) { return new JavaPublisherSocket(nativeContext(context).createPubSocket()); }
        @Override public ZLinkBackendSubscriberSocket createSubscriberSocket(ZLinkBackendContext context) { return new JavaSubscriberSocket(nativeContext(context).createSubSocket()); }
    }

    private static final class JavaRegistryBackendAdapter implements ZLinkRegistryBackendAdapter {
        @Override public ZLinkBackendRegistry createRegistry(ZLinkBackendContext context) { return new JavaRegistry(nativeContext(context).createRegistry()); }
        @Override public ZLinkBackendRegistryQueryClient createRegistryQueryClient(ZLinkBackendContext context) { return new JavaRegistryQueryClient(nativeContext(context).createRegistryQueryClient()); }
    }

    private static final class JavaSpotBackendAdapter implements ZLinkSpotBackendAdapter {
        @Override
        public ZLinkBackendSpotNode createSpotNode(ZLinkBackendContext context, ZLinkBackendSpotNodeMode mode) {
            return new JavaSpotNode(nativeContext(context).createSpotNode(map(mode)));
        }
    }

    private static final class JavaStreamBackendAdapter implements ZLinkStreamBackendAdapter {
        @Override public ZLinkBackendStreamSocket createStreamSocket(ZLinkBackendContext context) { return new JavaStreamSocket(nativeContext(context).createStreamSocket()); }
    }

    private interface JavaSocketBacked {
        Socket nativeSocket();
    }

    private record JavaContext(Context nativeContext) implements ZLinkBackendContext {
        @Override public String name() { return "context"; }
        @Override public void shutdown() { nativeContext.shutdown(); }
        @Override
        public void close() {
            try {
                nativeContext.shutdown();
            } finally {
                nativeContext.close();
            }
        }
    }

    private record JavaDiscovery(Discovery nativeDiscovery) implements ZLinkBackendDiscovery {
        @Override public String name() { return "discovery"; }
        @Override public void connectRegistry(String endpoint) { nativeDiscovery.connectRegistry(endpoint); }
        @Override public void bindRoute(long kind, byte[] key, byte[] value) {
            nativeDiscovery.bindRoute((int) kind, key, value);
        }
        @Override public ZLinkBackendDiscoveryRoute resolveRoute(long kind, byte[] key) {
            try (var route = nativeDiscovery.resolveRoute((int) kind, key)) {
                return new ZLinkBackendDiscoveryRoute(
                    Optional.of(route.ownerRoutingId()),
                    Optional.of(new String(
                        route.value().toByteArray(),
                        java.nio.charset.StandardCharsets.UTF_8)));
            }
        }
        @Override public ZLinkBackendSpotRoute resolveSpot(RoutingId spotRid) {
            var route = nativeDiscovery.resolveSpot(spotRid);
            return new ZLinkBackendSpotRoute(
                route.ownerNodeRid(),
                route.spotRid(),
                toFrameworkSpotKind(route.spotKind()));
        }
        @Override public ZLinkBackendActorRoute resolveActor(String actorId) {
            var route = nativeDiscovery.resolveActor(actorId);
            return new ZLinkBackendActorRoute(route.actor().nodeRid(), route.actor().actorId());
        }
        @Override public List<ZLinkBackendRegistryMemberPeerEntry> memberPeers() {
            return nativeDiscovery.memberPeers().stream()
                .map(peer -> new ZLinkBackendRegistryMemberPeerEntry(
                    peer.autoConnectType().name(),
                    peer.serviceRole().name(),
                    peer.channelName(),
                    peer.endpoint(),
                    peer.routingId(),
                    peer.value(),
                    peer.weight()))
                .toList();
        }
        @Override public void close() { nativeDiscovery.close(); }
    }

    private static ZLinkSpotKind toFrameworkSpotKind(SpotKind kind) {
        return switch (kind) {
            case ENTRY -> ZLinkSpotKind.ENTRY;
            case USER -> ZLinkSpotKind.USER;
            case INVALID -> ZLinkSpotKind.INVALID;
        };
    }

    private record JavaDealerSocket(DealerSocket socket) implements ZLinkBackendDealerSocket, JavaSocketBacked {
        @Override public Socket nativeSocket() { return socket; }
        @Override public String name() { return "dealer"; }
        @Override public void bind(String endpoint) { socket.bind(endpoint); }
        @Override public void connect(String endpoint) { socket.connect(endpoint); }
        @Override public void disconnect(String endpoint) { socket.disconnect(endpoint); }
        @Override public void attachDiscovery(ZLinkBackendDiscovery discovery) { socket.attachDiscovery(((JavaDiscovery) discovery).nativeDiscovery()); }
        @Override public void setChannelName(String channelName) { socket.setChannelName(channelName); }
        @Override public boolean send(List<Message> parts, SendFlags flags) { return submit(socket.send(), parts, flags); }
        @Override public boolean request(List<Message> parts, ZLinkBackendRequestCallback callback, SendFlags flags, Duration timeout) {
            return submitRequest(socket.request(), parts, callback, flags, timeout);
        }
        @Override public ZLinkBackendReceived recv(ZLinkBackendRecvMode mode) {
            try (Received result = new Received()) {
                return socket.recv(result, map(mode)) ? fromReceived(result) : null;
            }
        }
        @Override public void close() { socket.close(); }
    }

    private record JavaRouterSocket(RouterSocket socket) implements ZLinkBackendRouterSocket, JavaSocketBacked {
        @Override public Socket nativeSocket() { return socket; }
        @Override public String name() { return "router"; }
        @Override public void bind(String endpoint) { socket.bind(endpoint); }
        @Override public void connect(String endpoint) { socket.connect(endpoint); }
        @Override public void disconnect(String endpoint) { socket.disconnect(endpoint); }
        @Override public void attachDiscovery(ZLinkBackendDiscovery discovery) { socket.attachDiscovery(((JavaDiscovery) discovery).nativeDiscovery()); }
        @Override public void setChannelName(String channelName) { socket.setChannelName(channelName); }
        @Override public void setRoutingId(RoutingId routingId) { socket.setRoutingId(routingId); }
        @Override public ZLinkBackendReceived recv(ZLinkBackendRecvMode mode) {
            try (Received result = new Received()) {
                return socket.recv(result, map(mode)) ? fromReceived(result) : null;
            }
        }
        @Override public boolean send(RoutingId routingId, List<Message> parts, SendFlags flags) { return submit(socket.send(routingId), parts, flags); }
        @Override public boolean request(RoutingId routingId, List<Message> parts, ZLinkBackendRequestCallback callback, SendFlags flags, Duration timeout) {
            return submitRequest(socket.request(routingId), parts, callback, flags, timeout);
        }
        @Override public void reply(RoutingId routingId, long requestSeq, List<Message> parts) { submitReply(socket.reply(routingId, requestSeq), parts); }
        @Override public boolean sendToSpot(RoutingId targetNodeRid, RoutingId spotRid, List<Message> parts, SendFlags flags) {
            return submit(socket.sendToSpot(targetNodeRid, spotRid), parts, flags);
        }
        @Override public boolean requestToSpot(RoutingId targetNodeRid, RoutingId spotRid, List<Message> parts, ZLinkBackendRequestCallback callback, SendFlags flags, Duration timeout) {
            return submitRequest(socket.requestToSpot(targetNodeRid, spotRid), parts, callback, flags, timeout);
        }
        @Override public void close() { socket.close(); }
    }

    private record JavaPublisherSocket(PubSocket socket) implements ZLinkBackendPublisherSocket, JavaSocketBacked {
        @Override public Socket nativeSocket() { return socket; }
        @Override public String name() { return "publisher"; }
        @Override public void bind(String endpoint) { socket.bind(endpoint); }
        @Override public void attachDiscovery(ZLinkBackendDiscovery discovery) { socket.attachDiscovery(((JavaDiscovery) discovery).nativeDiscovery()); }
        @Override public void setChannelName(String channelName) { socket.setChannelName(channelName); }
        @Override public boolean publish(String topic, List<Message> parts, SendFlags flags) { return submit(socket.publish(topic), parts, flags); }
        @Override public void close() { socket.close(); }
    }

    private record JavaSubscriberSocket(SubSocket socket) implements ZLinkBackendSubscriberSocket, JavaSocketBacked {
        @Override public Socket nativeSocket() { return socket; }
        @Override public String name() { return "subscriber"; }
        @Override public void bind(String endpoint) { socket.bind(endpoint); }
        @Override public void connect(String endpoint) { socket.connect(endpoint); }
        @Override public void disconnect(String endpoint) { socket.disconnect(endpoint); }
        @Override public void attachDiscovery(ZLinkBackendDiscovery discovery) { socket.attachDiscovery(((JavaDiscovery) discovery).nativeDiscovery()); }
        @Override public void setChannelName(String channelName) { socket.setChannelName(channelName); }
        @Override public void setSubscription(String topic) { socket.setSubscription(topic); }
        @Override public ZLinkBackendTopicMessage subscribe(ZLinkBackendRecvMode mode) {
            try (TopicMessage result = new TopicMessage()) {
                return socket.subscribe(result, map(mode))
                    ? new ZLinkBackendTopicMessage(result.getRoutingId(), result.topic(), copyParts(result.parts()))
                    : null;
            }
        }
        @Override public void close() { socket.close(); }
    }

    private record JavaStreamSocket(StreamSocket socket) implements ZLinkBackendStreamSocket, JavaSocketBacked {
        @Override public Socket nativeSocket() { return socket; }
        @Override public String name() { return "stream"; }
        @Override public void bind(String endpoint) { socket.bind(endpoint); }
        @Override public void onPacket(ZLinkBackendStreamPacketHandler handler) {
            socket.options().notify(true);
            socket.onPacket(handler::handle);
        }
        @Override public void onTransportError(ZLinkBackendStreamErrorHandler handler) { }
        @Override public boolean send(RoutingId routingId, List<Message> parts, SendFlags flags) {
            return submitFramedStream(socket.send(routingId), 1, null, null, parts, flags);
        }
        @Override public boolean send(RoutingId routingId, String packetName, List<Message> parts, SendFlags flags) {
            return submitFramedStream(socket.send(routingId), 1, null, packetName, parts, flags);
        }
        @Override public boolean send(RoutingId routingId, ZLinkStreamHeader header, List<Message> parts, SendFlags flags) {
            return submitFramedStream(socket.send(routingId), header, parts, flags);
        }
        @Override public boolean reply(RoutingId routingId, long requestSeq, String packetName, List<Message> parts, SendFlags flags) {
            return submitFramedStream(socket.send(routingId), 3, requestSeq, packetName, parts, flags);
        }
        @Override public boolean reply(RoutingId routingId, ZLinkStreamHeader header, List<Message> parts, SendFlags flags) {
            return submitFramedStream(socket.send(routingId), header, parts, flags);
        }
        @Override public void attachActorGateway(ZLinkBackendSpotNode node) { socket.attachActorGateway(((JavaSpotNode) node).spotNode()); }
        @Override public ZLinkBackendActorBindOperation bindActor(RoutingId sessionRid, ZLinkBackendActorRef actor) {
            ActorBindOperation operation = socket.bindActor(sessionRid, new ActorRef(actor.nodeRid(), actor.actorId(), actor.epoch()));
            return timeout -> toVoid(operation.timeout(timeout).submitAsync());
        }
        @Override public ZLinkBackendActorUnbindOperation unbindActor(RoutingId sessionRid, String actorId) {
            ActorUnbindOperation operation = socket.unbindActor(sessionRid, actorId);
            return timeout -> toVoid(operation.timeout(timeout).submitAsync());
        }
        @Override public boolean sendBoundActor(RoutingId sessionRid, String actorId, List<Message> parts, SendFlags flags) {
            return submit(socket.sendBoundActor(sessionRid, actorId), parts, flags);
        }
        @Override public boolean relayBoundActor(
            RoutingId sessionRid,
            String actorId,
            ZLinkStreamHeader streamHeader,
            List<Message> parts,
            SendFlags flags) {
            Message header = Message.from(ZLinkStreamHeaderCodec.encode(streamHeader));
            try {
                return submit(socket.sendBoundActor(sessionRid, actorId), prepend(header, parts), flags);
            } finally {
                header.close();
            }
        }
        @Override public void close() { socket.close(); }
    }

    private record JavaRegistry(Registry registry) implements ZLinkBackendRegistry {
        @Override public String name() { return "registry"; }
        @Override public void setId(int registryId) { registry.setId(registryId); }
        @Override public void bind(String pubEndpoint, String routerEndpoint) { registry.bind(pubEndpoint, routerEndpoint); }
        @Override public void connectPeer(String pubEndpoint, String routerEndpoint) { registry.addPeer(pubEndpoint); }
        @Override public ZLinkBackendRegistryStatus status() {
            var status = registry.status();
            return new ZLinkBackendRegistryStatus(
                status.registryId(),
                status.bindEndpoint(),
                status.state().name(),
                status.topologyEntryCount(),
                status.peerRegistryCount(),
                status.connectedPeerRegistryCount(),
                status.listSeq(),
                status.lastError(),
                status.lastChangedMs());
        }
        @Override public List<ZLinkBackendRegistryServiceSummaryEntry> serviceSummary(ZLinkBackendRegistryQueryFilter filter) {
            return registry.serviceSummary(serviceSummaryFilter(filter)).stream()
                .map(entry -> new ZLinkBackendRegistryServiceSummaryEntry(
                    entry.autoConnectType().name(),
                    entry.serviceRole().name(),
                    entry.channelName(),
                    entry.totalCount(),
                    entry.connectingCount(),
                    entry.readyCount(),
                    entry.errorCount(),
                    entry.stoppedCount(),
                    entry.lastReportedMs()))
                .toList();
        }
        @Override public List<ZLinkBackendRegistryTopologyEntry> topology(ZLinkBackendRegistryQueryFilter filter) {
            return registry.topology(topologyFilter(filter)).stream()
                .map(entry -> new ZLinkBackendRegistryTopologyEntry(
                    entry.autoConnectType().name(),
                    entry.routingId(),
                    entry.serviceKind().name(),
                    entry.serviceRole().name(),
                    entry.channelName(),
                    entry.endpoint(),
                    entry.source().name(),
                    entry.state().name(),
                    entry.desiredCount(),
                    entry.readyCount(),
                    entry.errorCode(),
                    entry.lastReportedMs(),
                    toFrameworkSpotKind(entry.spotKind())))
                .toList();
        }
        @Override public List<ZLinkBackendRegistryMemberPeerEntry> memberPeers(String channelName) {
            return registry.memberPeers(channelName).stream()
                .map(peer -> new ZLinkBackendRegistryMemberPeerEntry(
                    peer.autoConnectType().name(),
                    peer.serviceRole().name(),
                    peer.channelName(),
                    peer.endpoint(),
                    peer.routingId(),
                    peer.value(),
                    peer.weight()))
                .toList();
        }
        @Override public void close() { registry.close(); }
    }

    private record JavaRegistryQueryClient(RegistryQueryClient client) implements ZLinkBackendRegistryQueryClient {
        @Override public String name() { return "registryQueryClient"; }
        @Override public void connect(String endpoint) { client.connect(endpoint); }
        @Override public List<ZLinkBackendRegistryServiceSummaryEntry> serviceSummary(ZLinkBackendRegistryQueryFilter filter) { return List.of(); }
        @Override public List<ZLinkBackendRegistryTopologyEntry> topology(ZLinkBackendRegistryQueryFilter filter) {
            return client.topology(topologyFilter(filter)).stream()
                .map(entry -> new ZLinkBackendRegistryTopologyEntry(
                    entry.autoConnectType().name(),
                    entry.routingId(),
                    entry.serviceKind().name(),
                    entry.serviceRole().name(),
                    entry.channelName(),
                    entry.endpoint(),
                    entry.source().name(),
                    entry.state().name(),
                    entry.desiredCount(),
                    entry.readyCount(),
                    entry.errorCode(),
                    entry.lastReportedMs(),
                    toFrameworkSpotKind(entry.spotKind())))
                .toList();
        }
        @Override public void close() { client.close(); }
    }

    private record JavaSpotNode(SpotNode spotNode) implements ZLinkBackendSpotNode {
        @Override public String name() { return "spotNode"; }
        @Override public RoutingId routingId() { return spotNode.getRoutingId(); }
        @Override public void setRoutingId(RoutingId routingId) { spotNode.setRoutingId(routingId); }
        @Override public void setRouterBind(String endpoint) { spotNode.setRouterBind(endpoint); }
        @Override public void setPubBind(String endpoint) { spotNode.setPubBind(endpoint); }
        @Override public void attachDiscovery(ZLinkBackendDiscovery discovery) { spotNode.attachDiscovery(((JavaDiscovery) discovery).nativeDiscovery()); }
        @Override public void connectPeer(String endpoint) { spotNode.connectPeer(endpoint); }
        @Override public void connectRouterChannelPeer(String channelName, String endpoint) { spotNode.connectRouterChannelPeer(channelName, endpoint); }
        @Override public void connectRouterChannelPeerRid(String channelName, RoutingId peerRid, String endpoint) { spotNode.connectRouterChannelPeerRid(channelName, peerRid, endpoint); }
        @Override public void attachSpotRouteChannelDiscovery(String channelName, ZLinkBackendDiscovery discovery) { spotNode.attachSpotRouteChannelDiscovery(channelName, ((JavaDiscovery) discovery).nativeDiscovery()); }
        @Override public void attachChannelDealer(ZLinkBackendDiscovery discovery, ZLinkBackendDealerSocket dealer) { spotNode.attachChannelDealer(((JavaDiscovery) discovery).nativeDiscovery(), ((JavaDealerSocket) dealer).socket()); }
        @Override public void attachChannelDealerManual(String channelName, ZLinkBackendDealerSocket dealer) { spotNode.attachChannelDealerManual(channelName, ((JavaDealerSocket) dealer).socket()); }
        @Override public ZLinkBackendSpot createSpot() { return new JavaSpot(spotNode.createSpot()); }
        @Override public ZLinkBackendSpot entrySpot() { return new JavaSpot(spotNode.entrySpot()); }
        @Override public ZLinkBackendActorRef createActor(String actorId) { return fromActorRef(spotNode.createActor(actorId).ref()); }
        @Override public ZLinkBackendActorRef actorLookup(String actorId) { return fromActorRef(spotNode.actorLookup(actorId)); }
        @Override public CompletionStage<ZLinkBackendActorJoinResult> joinActor(ZLinkBackendActorRef actor, RoutingId targetNodeRid, RoutingId targetSpotRid, List<Message> parts, Duration timeout) {
            if (parts.isEmpty()) {
                throw new IllegalArgumentException("actor join request must contain at least one part");
            }
            ActorJoinSubmitOperation operation = spotNode.joinActor(
                new ActorRef(actor.nodeRid(), actor.actorId(), actor.epoch()),
                targetNodeRid,
                targetSpotRid)
                .message(parts.get(0));
            for (int i = 1; i < parts.size(); i++) {
                Message part = parts.get(i);
                operation = operation.message(part);
            }
            return operation.timeout(timeout)
                .submitAsync()
                .thenApply(JavaSpotNode::fromActorJoinCompletion);
        }
        @Override public CompletionStage<ZLinkBackendActorJoinEntrySpotResult> joinActorEntrySpot(ZLinkBackendActorRef actor, RoutingId targetNodeRid, Duration timeout) {
            return spotNode.joinActorEntrySpot(
                    new ActorRef(actor.nodeRid(), actor.actorId(), actor.epoch()),
                    targetNodeRid)
                .timeout(timeout)
                .submitAsync()
                .thenApply(completion -> {
                    var result = completion.result();
                    return new ZLinkBackendActorJoinEntrySpotResult(
                        ZLinkBackendRequestResult.valueOf(result.result().name()),
                        fromActorRef(result.actor()),
                        result.targetNodeRid(),
                        result.joinEpoch(),
                        result.flags());
                });
        }
        @Override public boolean sendActorBoundSession(ZLinkBackendActorRef actor, List<Message> parts, SendFlags flags) { return submit(spotNode.sendActorBoundSession(new ActorRef(actor.nodeRid(), actor.actorId(), actor.epoch())), parts, flags); }
        @Override public void closeActorBoundSession(ZLinkBackendActorRef actor, Duration timeout) { spotNode.closeActorBoundSession(new ActorRef(actor.nodeRid(), actor.actorId(), actor.epoch()), timeout); }
        @Override public ZLinkBackendSpotNodeStatus status() { var status = spotNode.status(); return new ZLinkBackendSpotNodeStatus(status.state().name(), status.activePeerCount(), status.subjectCount()); }
        @Override public List<ZLinkBackendSpotNodePeerEntry> peers() { return spotNode.peers().stream().map(peer -> new ZLinkBackendSpotNodePeerEntry(null, peer.peerEndpoint(), peer.state().name())).toList(); }
        @Override public List<ZLinkBackendSpotNodeSubjectEntry> subjects() { return spotNode.subjects().stream().map(subject -> new ZLinkBackendSpotNodeSubjectEntry(subject.subject(), subject.subjectKind().name(), subject.readyPeerCount() > 0)).toList(); }
        @Override public void close() { spotNode.close(); }

        private static ZLinkBackendActorJoinResult fromActorJoinCompletion(
            ActorJoinCompletion completion) {
            var result = completion.result();
            return new ZLinkBackendActorJoinResult(
                ZLinkBackendRequestResult.valueOf(result.result().name()),
                result.joinResultCode(),
                fromActorRef(result.actor()),
                result.joinedSpotRid(),
                result.joinEpoch(),
                result.flags(),
                completion.replyParts());
        }
    }

    private record JavaSpot(Spot spot) implements ZLinkBackendSpot {
        @Override public String name() { return "spot"; }
        @Override public RoutingId routingId() { return spot.getRoutingId(); }
        @Override public void setRoutingId(RoutingId routingId) { spot.setRoutingId(routingId); }
        @Override public void setSubscription(String topic) { spot.setSubscription(topic); }
        @Override public ZLinkBackendTopicMessage subscribe(ZLinkBackendRecvMode mode) {
            try (TopicMessage result = new TopicMessage()) {
                return spot.subscribe(result, map(mode))
                    ? new ZLinkBackendTopicMessage(result.getRoutingId(), result.topic(), copyParts(result.parts()))
                    : null;
            }
        }
        @Override public ZLinkBackendReceived recvRoute(ZLinkBackendRecvMode mode) {
            Received result = new Received();
            if (spot.recvRouted(result, map(mode))) {
                return fromReceived(result);
            }
            result.close();
            return null;
        }
        @Override public boolean sendToChannel(String channelName, List<Message> parts, SendFlags flags) { return submit(spot.sendToChannel(channelName), parts, flags); }
        @Override public boolean requestToChannel(String channelName, List<Message> parts, ZLinkBackendRequestCallback callback, SendFlags flags, Duration timeout) { return submitRequest(spot.requestToChannel(channelName), parts, callback, flags, timeout); }
        @Override public boolean publish(String topic, List<Message> parts, SendFlags flags) { return submit(spot.publish(topic), parts, flags); }
        @Override public boolean sendToSpot(RoutingId targetNodeRid, RoutingId spotRid, List<Message> parts, SendFlags flags) { return submit(spot.sendToSpot(targetNodeRid, spotRid), parts, flags); }
        @Override public boolean requestToSpot(RoutingId targetNodeRid, RoutingId spotRid, List<Message> parts, ZLinkBackendRequestCallback callback, SendFlags flags, Duration timeout) { return submitRequest(spot.requestToSpot(targetNodeRid, spotRid), parts, callback, flags, timeout); }
        @Override public void onDispatchEvent(ZLinkBackendSpotDispatchHandler handler) {
            spot.setDispatchHandler(info -> handler.handle(fromSpotDispatchInfo(info)));
        }
        @Override public ZLinkBackendActorJoinRequest recvActorJoin(ZLinkBackendRecvMode mode) {
            ActorJoinRequest request = spot.recvActorJoin(map(mode));
            if (request == null) {
                return null;
            }
            return fromActorJoinRequest(request);
        }
        @Override public void replyActorJoin(
            ZLinkBackendActorJoinRequest request,
            int joinResultCode,
            List<Message> parts) {
            ActorJoinRequest nativeRequest = (ActorJoinRequest) request.nativeRequest();
            var operation = spot.replyActorJoin(nativeRequest, joinResultCode);
            for (Message part : parts) {
                operation.message(part);
            }
            operation.submit();
        }
        @Override public ZLinkBackendActorLifecycleEvent recvActorLifecycle(
            ZLinkBackendRecvMode mode) {
            SpotActorLifecycleEvent event = spot.recvActorLifecycle(map(mode));
            return event == null ? null : fromActorLifecycleEvent(event);
        }
        @Override public void close() { spot.close(); }
    }

    private record JavaSocketMonitor(SocketMonitor monitor) implements ZLinkBackendSocketMonitor {
        @Override public String name() { return "socketMonitor"; }
        @Override public void onEvent(ZLinkBackendSocketMonitorHandler handler) { monitor.onEvent(event -> handler.handle(fromMonitorEvent(event))); }
        @Override public ZLinkBackendSocketMonitorEvent recv() { return fromMonitorEvent(monitor.recv()); }
        @Override public void close() { monitor.close(); }
    }

    private static Context nativeContext(ZLinkBackendContext context) {
        return ((JavaContext) context).nativeContext();
    }

    private static AutoConnectType map(ZLinkBackendAutoConnectType type) {
        return switch (type) {
            case ROUTE_MESH -> AutoConnectType.ROUTE_MESH;
            case CLIENT_SERVER -> AutoConnectType.CLIENT_SERVER;
            case DEALER_MESH -> AutoConnectType.DEALER_MESH;
            case FANOUT -> AutoConnectType.FANOUT;
            case SPOT_MESH -> AutoConnectType.SPOT_MESH;
        };
    }

    private static ZLinkBackendSpotDispatchInfo fromSpotDispatchInfo(SpotDispatchInfo info) {
        return new ZLinkBackendSpotDispatchInfo(
            map(info.event()),
            info.actorMessages().stream()
                .map(ZLinkJavaBackendAdapterFactory::fromActorReceived)
                .toList());
    }

    private static ZLinkBackendSpotDispatchEvent map(SpotDispatchEvent event) {
        return switch (event) {
            case SUBSCRIBE_READABLE -> ZLinkBackendSpotDispatchEvent.SUBSCRIBE_READABLE;
            case ROUTED_READABLE -> ZLinkBackendSpotDispatchEvent.ROUTED_READABLE;
            case TIMER_READABLE -> ZLinkBackendSpotDispatchEvent.TIMER_READABLE;
            case CHANNEL_REPLY_READABLE -> ZLinkBackendSpotDispatchEvent.CHANNEL_REPLY_READABLE;
            case ACTOR_READABLE -> ZLinkBackendSpotDispatchEvent.ACTOR_READABLE;
            case ACTOR_JOIN_READABLE -> ZLinkBackendSpotDispatchEvent.ACTOR_JOIN_READABLE;
            case ACTOR_LIFECYCLE_READABLE -> ZLinkBackendSpotDispatchEvent.ACTOR_LIFECYCLE_READABLE;
        };
    }

    private static ZLinkBackendActorReceived fromActorReceived(ActorReceived received) {
        var info = received.info();
        return new ZLinkBackendActorReceived(
            fromActorRef(info.actor()),
            info.sourceNodeRid(),
            info.sourceSessionRid(),
            Optional.empty(),
            info.flags(),
            Message.from(received.message()),
            received.hasMore());
    }

    private static ZLinkBackendActorJoinRequest fromActorJoinRequest(ActorJoinRequest request) {
        return new ZLinkBackendActorJoinRequest(
            fromActorRef(request.info().sourceActor()),
            fromActorRef(request.info().targetActor()),
            request.parts().stream().map(Message::from).toList(),
            request);
    }

    private static ZLinkBackendActorLifecycleEvent fromActorLifecycleEvent(
        SpotActorLifecycleEvent event) {
        var info = event.info();
        return new ZLinkBackendActorLifecycleEvent(
            ZLinkBackendActorLifecycleEventKind.valueOf(event.kind().name()),
            new ZLinkBackendActorLifecycleInfo(
                fromActorRef(info.previousActor()),
                fromActorRef(info.currentActor()),
                info.previousSpotRid(),
                info.currentSpotRid(),
                info.joinEpoch(),
                info.flags()));
    }

    private static SpotNodeMode map(ZLinkBackendSpotNodeMode mode) {
        return switch (mode) {
            case PUBSUB -> SpotNodeMode.PUBSUB;
            case ROUTED -> SpotNodeMode.ROUTED;
            case ALL -> SpotNodeMode.ALL;
        };
    }

    private static RegistryServiceSummaryFilter serviceSummaryFilter(
        ZLinkBackendRegistryQueryFilter filter) {
        return new RegistryServiceSummaryFilter(
            enumValue(AutoConnectType.class, filter.autoConnectType()),
            enumValue(ServiceRole.class, filter.serviceRole()),
            filter.channelName().orElse(null));
    }

    private static RegistryTopologyFilter topologyFilter(
        ZLinkBackendRegistryQueryFilter filter) {
        return new RegistryTopologyFilter(
            enumValue(AutoConnectType.class, filter.autoConnectType()),
            enumValue(ServiceKind.class, filter.serviceKind()),
            enumValue(ServiceRole.class, filter.serviceRole()),
            filter.channelName().orElse(null),
            filter.routingId().orElse(null),
            enumValue(TopologyState.class, filter.state()),
            enumValue(TopologySource.class, filter.source()));
    }

    private static <T extends Enum<T>> T enumValue(Class<T> enumType, Optional<String> value) {
        return value.map(name -> Enum.valueOf(enumType, name)).orElse(null);
    }

    private static RecvFlags map(ZLinkBackendRecvMode mode) {
        return mode == ZLinkBackendRecvMode.DONT_WAIT ? RecvFlags.DONT_WAIT : RecvFlags.NONE;
    }

    private static boolean submit(SendOperation operation, List<Message> parts, SendFlags flags) {
        var submit = operation.message(parts.get(0));
        for (int i = 1; i < parts.size(); i++) {
            submit.message(parts.get(i));
        }
        return submit.flags(flags).submit();
    }

    private static byte[] encodeStreamHeader(
        int kind,
        int codec,
        String packetName,
        Long requestSeq) {
        byte[] name = packetName.getBytes(StandardCharsets.UTF_8);
        int flags = requestSeq == null ? 0 : 0x01;
        ByteBuffer buffer = ByteBuffer.allocate(3 + (requestSeq == null ? 0 : Long.BYTES) + 1 + name.length);
        buffer.put((byte) kind);
        buffer.put((byte) codec);
        buffer.put((byte) flags);
        if (requestSeq != null) {
            buffer.putLong(requestSeq);
        }
        buffer.put((byte) name.length);
        buffer.put(name);
        return buffer.array();
    }

    private static List<Message> prepend(Message first, List<Message> rest) {
        ArrayList<Message> result = new ArrayList<>(rest.size() + 1);
        result.add(first);
        result.addAll(rest);
        return result;
    }

    private static boolean submitFramedStream(
        SendOperation operation,
        int kind,
        Long requestSeq,
        String packetName,
        List<Message> parts,
        SendFlags flags) {
        StreamPayload payload = streamPayload(packetName, parts);
        Message frame = Message.from(encodeStreamFrame(
            encodeStreamHeader(kind, 0, payload.packetName(), requestSeq),
            payload.body()));
        try {
            return operation.message(frame).flags(flags).submit();
        } finally {
            frame.close();
        }
    }

    private static boolean submitFramedStream(
        SendOperation operation,
        ZLinkStreamHeader header,
        List<Message> parts,
        SendFlags flags) {
        if (parts == null || parts.size() != 1) {
            throw new IllegalArgumentException("stream frame requires exactly one payload part");
        }
        Message frame = Message.from(encodeStreamFrame(
            ZLinkStreamHeaderCodec.encode(header),
            parts.get(0).toByteArray()));
        try {
            return operation.message(frame).flags(flags).submit();
        } finally {
            frame.close();
        }
    }

    private static StreamPayload streamPayload(String packetName, List<Message> parts) {
        if (parts == null || parts.isEmpty()) {
            throw new IllegalArgumentException("stream payload requires at least one part");
        }
        if (packetName != null) {
            return new StreamPayload(packetName, parts.get(0).toByteArray());
        }
        if (parts.size() == 1) {
            return new StreamPayload("", parts.get(0).toByteArray());
        }
        return new StreamPayload(parts.get(0).toUtf8String(), parts.get(1).toByteArray());
    }

    private static byte[] encodeStreamFrame(byte[] header, byte[] body) {
        if (header.length > 0xFFFF) {
            throw new IllegalArgumentException("stream header exceeds u16 header size");
        }
        ByteBuffer buffer = ByteBuffer.allocate(6 + header.length + body.length);
        buffer.putShort((short) header.length);
        buffer.putInt(body.length);
        buffer.put(header);
        buffer.put(body);
        return buffer.array();
    }

    private record StreamPayload(String packetName, byte[] body) { }

    private static List<Message> copyParts(List<Message> parts) {
        return parts.stream()
            .map(Message::from)
            .toList();
    }

    private static void submitReply(ReplyOperation operation, List<Message> parts) {
        var submit = operation.message(parts.get(0));
        for (int i = 1; i < parts.size(); i++) {
            submit.message(parts.get(i));
        }
        submit.submit();
    }

    private static boolean submitRequest(
        RequestOperation operation,
        List<Message> parts,
        ZLinkBackendRequestCallback callback,
        SendFlags flags,
        Duration timeout) {
        var submit = operation.message(parts.get(0)).timeout(timeout).flags(flags);
        for (int i = 1; i < parts.size(); i++) {
            submit.message(parts.get(i));
        }
        try {
            return submit.submit((result, replyParts) -> callback.handle(new ZLinkBackendReceived(
                Optional.empty(),
                Optional.empty(),
                Optional.empty(),
                replyParts)));
        } catch (ZlinkSubmitException ex) {
            throw new IllegalStateException(
                "zlink request submit failed: result=" + ex.getResult()
                    + ", errno=" + ex.getNativeErrno(), ex);
        }
    }

    private static ZLinkBackendReceived fromReceived(Received received) {
        return new ZLinkBackendReceived(
            received.getRoutingId(),
            received.spotRid(),
            received.requestSeq(),
            received.parts().stream().map(Message::from).toList(),
            replyParts -> submitReply(received.reply(), replyParts),
            received::close);
    }

    private static ZLinkBackendActorRef fromActorRef(ActorRef actorRef) {
        return new ZLinkBackendActorRef(actorRef.nodeRid(), actorRef.actorId(), actorRef.generation());
    }

    private static CompletionStage<Void> toVoid(CompletionStage<List<Message>> stage) {
        return stage.thenAccept(parts -> parts.forEach(Message::close));
    }

    private static ZLinkBackendSocketMonitorEvent fromMonitorEvent(MonitorEvent event) {
        return new ZLinkBackendSocketMonitorEvent(
            event.event().name(),
            event.routingId(),
            event.localAddr(),
            event.remoteAddr());
    }
}
