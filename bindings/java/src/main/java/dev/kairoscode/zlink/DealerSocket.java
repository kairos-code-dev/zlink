/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.service.discovery.Discovery;
import java.util.List;

public final class DealerSocket extends Socket {
    public DealerSocket(Context ctx) {
        super(ctx, SocketType.DEALER);
    }

    public void bind(String endpoint) { super.bind(endpoint); }
    public void connect(String endpoint) { super.connect(endpoint); }
    public void unbind(String endpoint) { super.unbind(endpoint); }
    public void disconnect(String endpoint) { super.disconnect(endpoint); }
    public void attachDiscovery(Discovery discovery) { super.attachDiscovery(discovery); }
    public void setRoutingId(RoutingId rid) { super.setRoutingId(rid); }
    public RoutingId routingId() { return super.routingId(); }
    public void send(Message part) { super.send(part); }
    public void send(Message part, SendFlag flags) { super.send(part, flags); }
    public void send(List<Message> parts) { super.send(parts); }
    public void send(List<Message> parts, SendFlag flags) { super.send(parts, flags); }
    public Received recv() { return super.recv(); }
    public Received recv(ReceiveFlag flags) { return super.recv(flags); }
    public void onReceive(SocketMessageHandler handler) { super.onReceive(handler); }
    public void onSendReady(SendReadyHandler handler) { super.onSendReady(handler); }
}
