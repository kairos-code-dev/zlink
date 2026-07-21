package systems.zlink.framework.runtime.binding;

import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.eventing.MonitorEventType;
import systems.zlink.contracts.eventing.SocketMonitor;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.spot.ActorRef;
import systems.zlink.contracts.service.spot.StreamSessionBinding;
import systems.zlink.contracts.service.spot.StreamSessionService;
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
    private final ZLinkJavaMeshNode meshNode;
    private final StreamSessionService sessionService;
    private SocketMonitor monitor;
    private boolean sessionServiceStarted;

    ZLinkJavaStreamSocket(StreamSocket socket, ZLinkJavaMeshNode meshNode) {
        this.socket = socket;
        this.meshNode = meshNode;
        this.sessionService = meshNode == null
            ? null
            : StreamSessionService.create(meshNode.nativeNode(), socket);
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
        if (sessionService != null && !sessionServiceStarted) {
            sessionService.start();
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
            try {
                return meshNode.track(
                    requireSessionService().bindActor(sessionRid, toNative(actor), timeout));
            } catch (ZlinkSubmitException error) {
                throw new IllegalStateException(
                    "STREAM actor bind submit failed: result=" + error.getResult()
                        + " errno=" + error.getNativeErrno()
                        + " session=" + sessionRid
                        + " actor=" + actor.actorId()
                        + " generation=" + actor.generation(),
                    error);
            }
        };
    }
    @Override public ZLinkBackendActorUnbindOperation unbindActor(RoutingId sessionRid, String actorId) {
        return timeout -> {
            StreamSessionBinding binding = requireBinding(sessionRid, actorId);
            return meshNode.track(requireSessionService().unbindActor(
                sessionRid,
                binding.actor(),
                binding.bindingGeneration(),
                timeout));
        };
    }
    @Override public boolean sendBoundActor(RoutingId sessionRid, String actorId, List<Message> parts, SendFlags flags) {
        requireSessionService().sendToActor(
            sessionRid,
            requireBinding(sessionRid, actorId).actor(),
            parts,
            flags);
        return true;
    }
    @Override public boolean relayBoundActor(
        RoutingId sessionRid,
        String actorId,
        ZLinkStreamHeader streamHeader,
        List<Message> parts,
        SendFlags flags) {
        Message header = Message.from(ZLinkStreamHeaderCodec.encode(streamHeader));
        try {
            requireSessionService().sendToActor(
                sessionRid,
                requireBinding(sessionRid, actorId).actor(),
                prepend(header, parts),
                flags);
            return true;
        } finally {
            header.close();
        }
    }
    @Override public void close() {
        notifyAdmissionShutdown();
        closeMonitor();
        if (sessionService != null) {
            sessionService.close();
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

    private StreamSessionBinding requireBinding(RoutingId sessionRid, String actorId) {
        return requireSessionService().bindings(sessionRid).stream()
            .filter(binding -> binding.actor().actorId().equals(actorId))
            .findFirst()
            .orElseThrow(() -> new IllegalStateException(
                "STREAM session is not bound to actor: " + actorId));
    }

    private static ActorRef toNative(ZLinkBackendActorRef actor) {
        return new ActorRef(actor.nodeRid(), actor.actorId(), actor.generation());
    }

    private StreamSessionService requireSessionService() {
        if (sessionService == null) {
            throw new IllegalStateException(
                "STREAM actor dispatch is not enabled for this stream node");
        }
        return sessionService;
    }
}
