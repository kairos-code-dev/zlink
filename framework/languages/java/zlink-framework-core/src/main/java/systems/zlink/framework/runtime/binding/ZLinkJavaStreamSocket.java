package systems.zlink.framework.runtime.binding;

import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.eventing.MonitorEventType;
import systems.zlink.contracts.eventing.SocketMonitor;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.spot.ActorBindOperation;
import systems.zlink.contracts.service.spot.ActorRef;
import systems.zlink.contracts.service.spot.ActorUnbindOperation;
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
    private SocketMonitor monitor;

    ZLinkJavaStreamSocket(StreamSocket socket) {
        this.socket = socket;
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
        ActorBindOperation operation = socket.bindActor(sessionRid, new ActorRef(actor.nodeRid(), actor.actorId(), actor.generation()));
        return timeout -> toVoid(operation.timeout(timeout).submit());
    }
    @Override public ZLinkBackendActorUnbindOperation unbindActor(RoutingId sessionRid, String actorId) {
        ActorUnbindOperation operation = socket.unbindActor(sessionRid, actorId);
        return timeout -> toVoid(operation.timeout(timeout).submit());
    }
    @Override public boolean sendBoundActor(RoutingId sessionRid, String actorId, List<Message> parts, SendFlags flags) {
        return ZLinkJavaSocketSupport.submit(socket.sendBoundActor(sessionRid, actorId), parts, flags);
    }
    @Override public boolean relayBoundActor(
        RoutingId sessionRid,
        String actorId,
        ZLinkStreamHeader streamHeader,
        List<Message> parts,
        SendFlags flags) {
        Message header = Message.from(ZLinkStreamHeaderCodec.encode(streamHeader));
        try {
            return ZLinkJavaSocketSupport.submit(
                socket.sendBoundActor(sessionRid, actorId),
                prepend(header, parts),
                flags);
        } finally {
            header.close();
        }
    }
    @Override public void close() {
        closeMonitor();
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

    private static CompletionStage<Void> toVoid(CompletionStage<List<Message>> stage) {
        return stage.thenAccept(parts -> parts.forEach(Message::close));
    }
}
