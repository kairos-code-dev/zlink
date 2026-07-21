package systems.zlink.framework.runtime.binding;

import java.time.Duration;
import java.util.List;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.sockets.RouterSocket;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.sockets.Socket;
import systems.zlink.framework.runtime.backend.ZLinkBackendReceived;
import systems.zlink.framework.runtime.backend.ZLinkBackendRecvMode;
import systems.zlink.framework.runtime.backend.ZLinkBackendRequestCallback;
import systems.zlink.framework.runtime.backend.ZLinkBackendRouterSocket;

record ZLinkJavaRouterSocket(RouterSocket socket)
    implements ZLinkBackendRouterSocket, ZLinkJavaSocketBacked {
    @Override public Socket nativeSocket() { return socket; }
    @Override public String name() { return "router"; }
    @Override public void bind(String endpoint) { socket.bind(endpoint); }
    @Override public void connect(String endpoint) { socket.connect(endpoint); }
    @Override public void disconnect(String endpoint) { socket.disconnect(endpoint); }
    @Override public void setChannelName(String channelName) { socket.setChannelName(channelName); }
    @Override public void setRoutingId(RoutingId routingId) { socket.setRoutingId(routingId); }
    @Override public void setConnectRoutingId(RoutingId routingId) { socket.options().setConnectRoutingId(routingId); }
    @Override public void setProbe(boolean enabled) { socket.options().probe(enabled); }
    @Override public long maxMessageSize() { return Math.max(0, socket.options().maxMessageSize()); }
    @Override public void setMaxMessageSize(long value) { socket.options().maxMessageSize(value == 0 ? -1 : value); }
    @Override public int peerWeight() { return socket.options().peerWeight(); }
    @Override public void setPeerWeight(int weight) { socket.options().peerWeight(weight); }
    @Override public String lastEndpoint() { return socket.options().lastEndpoint(); }

    @Override
    public ZLinkBackendReceived recv(ZLinkBackendRecvMode mode) {
        try (Received result = new Received()) {
            return ZLinkJavaSocketSupport.recvOrNoData(
                    () -> socket.recv(result, ZLinkJavaSocketSupport.map(mode)))
                ? ZLinkJavaSocketSupport.fromReceived(result)
                : null;
        }
    }

    @Override
    public boolean send(RoutingId routingId, List<Message> parts, SendFlags flags) {
        return ZLinkJavaSocketSupport.submit(socket.send(routingId), parts, flags);
    }

    @Override
    public boolean request(
        RoutingId routingId,
        List<Message> parts,
        ZLinkBackendRequestCallback callback,
        SendFlags flags,
        Duration timeout) {
        return ZLinkJavaSocketSupport.submitRequest(
            socket.request(routingId),
            parts,
            callback,
            flags,
            timeout);
    }

    @Override
    public void reply(RoutingId routingId, long requestSeq, List<Message> parts) {
        ZLinkJavaSocketSupport.submitReply(socket.reply(routingId, requestSeq), parts);
    }

    @Override public void close() {
        notifyAdmissionShutdown();
        socket.close();
    }
}
