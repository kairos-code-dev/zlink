/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import java.lang.foreign.MemorySegment;
import java.util.List;
import java.util.Optional;

public final class StreamSocket extends Socket {
    private final StreamSocketOptions options = new StreamSocketOptions(this);

    public StreamSocket(Context ctx) {
        super(ctx, SocketType.STREAM);
    }

    public void bind(String endpoint) { super.bind(endpoint); }
    public void unbind(String endpoint) { super.unbind(endpoint); }
    public void send(int rid, Message part) {
        super.send(rid, part, SendFlag.NONE);
    }
    public void send(int rid, Message part, SendFlags flags) {
        super.send(rid, part, SendFlag.fromValue(flags.value()));
    }
    SendResult sendNoWaitResult(int rid, Message part) {
        return super.sendNoWaitResult(RoutingId.fromU32(rid), part);
    }
    public void send(RoutingId rid, Message part) { super.send(rid, part); }
    public void send(RoutingId rid, Message part, SendFlags flags) { super.send(rid, part, SendFlag.fromValue(flags.value())); }
    public boolean trySend(int rid, Message part) {
        return super.trySend(RoutingId.fromU32(rid), part);
    }
    public boolean trySend(RoutingId rid, Message part) {
        return super.trySend(rid, part);
    }
    SendResult sendNoWaitResult(RoutingId rid, Message part) {
        return super.sendNoWaitResult(rid, part);
    }
    public int send(int rid, MemorySegment payload, int length, SendFlags flags) {
        return super.send(rid, payload, length, flags.value());
    }
    public int sendCopied(int rid, MemorySegment payload, int length,
                          SendFlags flags) {
        return super.sendCopied(rid, payload, length, flags.value());
    }
    public void send(RoutingId rid, List<Message> parts) { super.send(rid, parts); }
    public void send(RoutingId rid, List<Message> parts, SendFlags flags) { super.send(rid, parts, SendFlag.fromValue(flags.value())); }
    public boolean trySend(RoutingId rid, List<Message> parts) {
        return super.trySend(rid, parts);
    }
    SendResult sendNoWaitResult(RoutingId rid, List<Message> parts) {
        return super.sendNoWaitResult(rid, parts);
    }
    public Received recv() { return super.recv(); }
    public Received recv(RecvFlags flags) { return super.recv(ReceiveFlag.fromValue(flags.value())); }
    public Received tryRecv() { return super.tryRecv(); }
    Optional<Received> recvNoWait() { return super.recvNoWait(); }
    public void onSendReady(SendReadyHandler handler) { super.onSendReady(handler); }
    public void onPacket(StreamPacketHandler handler) { super.attachStreamRaw(handler); }
    public void onPacketNative(StreamUInt32RawNativeHandler handler) {
        super.attachStreamRaw(handler);
    }
    public void onFramedPacket(StreamFramedPacketHandler handler) {
        super.attachStreamPacket(handler);
    }
    public void onFramedPacket(StreamUInt32FramedPacketHandler handler) {
        super.attachStreamPacket(handler);
    }
    public void onFramedPacketNative(StreamUInt32FramedNativeHandler handler) {
        super.attachStreamPacket(handler);
    }
    public void detachStream() { super.detachStream(); }
    @Override public StreamSocketOptions options() { return options; }
}
