package systems.zlink.framework.runtime.binding;

import java.time.Duration;
import java.util.List;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.sockets.DealerSocket;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.sockets.Socket;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendDealerSocket;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendReceived;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendRecvMode;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendRequestCallback;

record ZLinkJavaDealerSocket(DealerSocket socket)
    implements ZLinkBackendDealerSocket, ZLinkJavaSocketBacked {
    @Override public Socket nativeSocket() { return socket; }
    @Override public String name() { return "dealer"; }
    @Override public void bind(String endpoint) { socket.bind(endpoint); }
    @Override public void connect(String endpoint) { socket.connect(endpoint); }
    @Override public void disconnect(String endpoint) { socket.disconnect(endpoint); }
    @Override public void setChannelName(String channelName) { ZLinkJavaSocketSupport.validateChannelName(channelName); }

    @Override
    public boolean send(List<Message> parts, SendFlags flags) {
        synchronized (socket) {
            return ZLinkJavaSocketSupport.submit(socket.send(), parts, flags);
        }
    }

    @Override
    public boolean request(
        List<Message> parts,
        ZLinkBackendRequestCallback callback,
        SendFlags flags,
        Duration timeout) {
        synchronized (socket) {
            return ZLinkJavaSocketSupport.submitRequest(socket.request(), parts, callback, flags, timeout);
        }
    }

    @Override
    public ZLinkBackendReceived recv(ZLinkBackendRecvMode mode) {
        synchronized (socket) {
            try (Received result = new Received()) {
                return ZLinkJavaSocketSupport.recvOrNoData(
                        () -> socket.recv(result, ZLinkJavaSocketSupport.map(mode)))
                    ? ZLinkJavaSocketSupport.fromReceived(result)
                    : null;
            }
        }
    }

    @Override
    public void close() {
        synchronized (socket) {
            notifyAdmissionShutdown();
            socket.close();
        }
    }
}
