/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import java.util.List;
import java.util.Optional;

public final class StreamSocket extends Socket {
    private final StreamSocketOptions options = new StreamSocketOptions(this);

    public StreamSocket(Context ctx) {
        super(ctx, SocketType.STREAM);
    }

    public void bind(String endpoint) { super.bind(endpoint); }
    public void unbind(String endpoint) { super.unbind(endpoint); }
    public void send(RoutingId rid, Message part) { super.send(rid, part); }
    public void send(RoutingId rid, List<Message> parts) { super.send(rid, parts); }
    public SendResult trySend(RoutingId rid, Message part) { return super.trySend(rid, part); }
    public SendResult trySend(RoutingId rid, List<Message> parts) { return super.trySend(rid, parts); }
    public Received recv() { return super.recv(); }
    public Optional<Received> tryRecv() { return super.tryRecv(); }
    public void onReceive(SocketMessageHandler handler) { super.onReceive(handler); }
    public void onSendReady(SendReadyHandler handler) { super.onSendReady(handler); }
    public void attachStreamRaw(StreamPacketHandler handler) { super.attachStreamRaw(handler); }
    public void detachStream() { super.detachStream(); }
    @Override public StreamSocketOptions options() { return options; }
}
