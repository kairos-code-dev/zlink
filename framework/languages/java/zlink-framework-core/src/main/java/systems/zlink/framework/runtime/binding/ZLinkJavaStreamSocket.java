package systems.zlink.framework.runtime.binding;

import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ConcurrentHashMap;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.eventing.MonitorEventType;
import systems.zlink.contracts.eventing.SocketMonitor;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.sockets.Socket;
import systems.zlink.contracts.sockets.StreamSocket;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorBindOperation;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorRef;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorUnbindOperation;
import systems.zlink.framework.runtime.backend.ZLinkBackendStreamErrorHandler;
import systems.zlink.framework.runtime.backend.ZLinkBackendStreamPacketHandler;
import systems.zlink.framework.runtime.backend.ZLinkBackendStreamSocket;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeader;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeaderCodec;

final class ZLinkJavaStreamSocket implements ZLinkBackendStreamSocket, ZLinkJavaSocketBacked {
    private final StreamSocket socket;
    private final ZLinkJavaRawMeshNode meshNode;
    private final Map<BindingKey, ZLinkBackendActorRef> bindings =
        new ConcurrentHashMap<>();
    private SocketMonitor monitor;
    private boolean sessionServiceStarted;

    ZLinkJavaStreamSocket(StreamSocket socket, ZLinkJavaRawMeshNode meshNode) {
        this.socket = socket;
        this.meshNode = meshNode;
    }

    @Override public Socket nativeSocket() { return socket; }
    @Override public String name() { return "stream"; }
    @Override public void setTlsServer(
        String certificatePath,
        String keyPath,
        boolean requireClientCertificate) {
        socket.setTlsServer(certificatePath, keyPath, requireClientCertificate);
    }
    @Override public void bind(String endpoint) { socket.bind(endpoint); }
    @Override public void onPacket(ZLinkBackendStreamPacketHandler handler) {
        socket.options().notify(true);
        socket.onPacket(handler::handle);
    }
    @Override public void onTransportError(ZLinkBackendStreamErrorHandler handler) {
        closeMonitor();
        monitor = socket.monitorOpen(MonitorEventType.DISCONNECTED);
        monitor.onEvent(event -> event.routingId().ifPresent(routingId ->
            handler.handle(routingId, 0, event.event().name())));
    }
    @Override public void startSessionService() {
        if (meshNode != null && !sessionServiceStarted) {
            sessionServiceStarted = true;
        }
    }
    @Override public boolean send(RoutingId routingId, List<Message> parts, SendFlags flags) {
        return ZLinkJavaStreamFraming.submit(socket.send(routingId), 1, null, null, parts, flags);
    }
    @Override public boolean send(RoutingId routingId, String packetName, List<Message> parts, SendFlags flags) {
        return ZLinkJavaStreamFraming.submit(socket.send(routingId), 1, null, packetName, parts, flags);
    }
    @Override public boolean send(RoutingId routingId, ZLinkStreamHeader header, List<Message> parts, SendFlags flags) {
        return ZLinkJavaStreamFraming.submit(socket.send(routingId), header, parts, flags);
    }
    @Override public boolean reply(RoutingId routingId, long requestSeq, String packetName, List<Message> parts, SendFlags flags) {
        return ZLinkJavaStreamFraming.submit(socket.send(routingId), 3, requestSeq, packetName, parts, flags);
    }
    @Override public boolean reply(RoutingId routingId, ZLinkStreamHeader header, List<Message> parts, SendFlags flags) {
        return ZLinkJavaStreamFraming.submit(socket.send(routingId), header, parts, flags);
    }
    @Override public ZLinkBackendActorBindOperation bindActor(RoutingId sessionRid, ZLinkBackendActorRef actor) {
        return timeout -> {
            requireSessionRuntime();
            BindingKey key = new BindingKey(sessionRid, actor.actorId());
            ZLinkBackendActorRef current = bindings.putIfAbsent(key, actor);
            if (current != null && !current.equals(actor)) {
                return CompletableFuture.failedFuture(
                    new IllegalStateException(
                        "STREAM session has a stale Actor binding"));
            }
            try {
                rawSpotNode().bindStreamSession(
                    sessionRid, actor, this);
                return CompletableFuture.completedFuture(null);
            } catch (RuntimeException failure) {
                bindings.remove(key, actor);
                return CompletableFuture.failedFuture(failure);
            }
        };
    }
    @Override public ZLinkBackendActorUnbindOperation unbindActor(RoutingId sessionRid, String actorId) {
        return timeout -> {
            ZLinkBackendActorRef actor = requireBinding(sessionRid, actorId);
            rawSpotNode().unbindStreamSession(sessionRid, actor, this);
            bindings.remove(new BindingKey(sessionRid, actorId), actor);
            return CompletableFuture.completedFuture(null);
        };
    }
    @Override public boolean sendBoundActor(RoutingId sessionRid, String actorId, List<Message> parts, SendFlags flags) {
        ZLinkBackendActorRef actor = requireBinding(sessionRid, actorId);
        return rawSpotNode().forwardActorBoundSession(
            actor,
            meshNode.routingId(),
            sessionRid,
            parts,
            flags);
    }
    @Override public boolean relayBoundActor(
        RoutingId sessionRid,
        String actorId,
        ZLinkStreamHeader streamHeader,
        List<Message> parts,
        SendFlags flags) {
        Message header = Message.from(ZLinkStreamHeaderCodec.encode(streamHeader));
        try {
            ZLinkBackendActorRef actor = requireBinding(sessionRid, actorId);
            return rawSpotNode().forwardActorBoundSession(
                actor,
                meshNode.routingId(),
                sessionRid,
                prepend(header, parts),
                flags);
        } finally {
            header.close();
        }
    }
    @Override public void close() {
        notifyAdmissionShutdown();
        closeMonitor();
        if (meshNode != null) {
            bindings.forEach((key, actor) ->
                rawSpotNode().unbindStreamSession(
                    key.sessionRid(), actor, this));
            bindings.clear();
        }
        socket.close();
    }

    private void closeMonitor() {
        if (monitor != null) {
            monitor.close();
            monitor = null;
        }
    }

    private static List<Message> prepend(Message first, List<Message> rest) {
        ArrayList<Message> result = new ArrayList<>(rest.size() + 1);
        result.add(first);
        result.addAll(rest);
        return result;
    }

    private ZLinkBackendActorRef requireBinding(
        RoutingId sessionRid,
        String actorId) {
        requireSessionRuntime();
        ZLinkBackendActorRef actor =
            bindings.get(new BindingKey(sessionRid, actorId));
        if (actor == null) {
            throw new IllegalStateException(
                "STREAM session is not bound to actor: " + actorId);
        }
        return actor;
    }

    private ZLinkJavaRawSpotNode rawSpotNode() {
        return (ZLinkJavaRawSpotNode) meshNode.spotNode();
    }

    private void requireSessionRuntime() {
        if (meshNode == null || !sessionServiceStarted) {
            throw new IllegalStateException(
                "STREAM actor dispatch is not enabled for this stream node");
        }
    }

    private record BindingKey(RoutingId sessionRid, String actorId) {
    }
}
