/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import java.util.List;

public final class StreamSocket extends Socket {
    public StreamSocket(Context ctx) {
        super(ctx, SocketType.STREAM);
    }

    public void bind(String endpoint) { super.bind(endpoint); }
    public void unbind(String endpoint) { super.unbind(endpoint); }
    public void send(RoutingId rid, Message part) { super.send(rid, part); }
    public void send(RoutingId rid, Message part, SendFlag flags) { super.send(rid, part, flags); }
    public void send(RoutingId rid, List<Message> parts) { super.send(rid, parts); }
    public void send(RoutingId rid, List<Message> parts, SendFlag flags) { super.send(rid, parts, flags); }
    public Received recv() { return super.recv(); }
    public Received recv(ReceiveFlag flags) { return super.recv(flags); }
    public void onReceive(SocketMessageHandler handler) { super.onReceive(handler); }
    public void onSendReady(SendReadyHandler handler) { super.onSendReady(handler); }
}
