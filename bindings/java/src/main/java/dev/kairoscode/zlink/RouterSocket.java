/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.service.discovery.Discovery;
import java.util.List;
import java.util.Optional;

public final class RouterSocket extends Socket {
    private final RouterSocketOptions options = new RouterSocketOptions(this);

    public RouterSocket(Context ctx) {
        super(ctx, SocketType.ROUTER);
    }

    public void bind(String endpoint) { super.bind(endpoint); }
    public void connect(String endpoint) { super.connect(endpoint); }
    public void unbind(String endpoint) { super.unbind(endpoint); }
    public void disconnect(String endpoint) { super.disconnect(endpoint); }
    public void attachDiscovery(Discovery discovery) { super.attachDiscovery(discovery); }
    public void setRoutingId(RoutingId rid) { super.setRoutingId(rid); }
    public RoutingId routingId() { return super.routingId(); }
    public void send(RoutingId rid, Message part) { super.send(rid, part); }
    public void send(RoutingId rid, List<Message> parts) { super.send(rid, parts); }
    public SendResult trySend(RoutingId rid, Message part) { return super.trySend(rid, part); }
    public SendResult trySend(RoutingId rid, List<Message> parts) { return super.trySend(rid, parts); }
    public Received recv() { return super.recv(); }
    public Optional<Received> tryRecv() { return super.tryRecv(); }
    public void onReceive(SocketMessageHandler handler) { super.onReceive(handler); }
    public void onSendReady(SendReadyHandler handler) { super.onSendReady(handler); }
    @Override public RouterSocketOptions options() { return options; }
}
