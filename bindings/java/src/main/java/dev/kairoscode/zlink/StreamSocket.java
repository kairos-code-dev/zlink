/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import java.lang.foreign.MemorySegment;
import java.util.List;
public final class StreamSocket extends Socket {
    private final StreamSocketOptions options = new StreamSocketOptions(this);

    public StreamSocket(Context ctx) {
        super(ctx, SocketType.STREAM);
    }

    public void bind(String endpoint) { super.bind(endpoint); }
    public void unbind(String endpoint) { super.unbind(endpoint); }
    public boolean send(int rid, Message part) {
        return super.send(RoutingId.fromU32(rid), part, SendFlag.NONE);
    }
    public boolean send(int rid, Message part, SendFlags flags) {
        return super.send(RoutingId.fromU32(rid), part, SendFlag.fromValue(flags.value()));
    }
    SendResult sendNoWaitResult(int rid, Message part) {
        return super.sendNoWaitResult(RoutingId.fromU32(rid), part);
    }
    public boolean send(RoutingId rid, Message part) { return super.send(rid, part); }
    public boolean send(RoutingId rid, Message part, SendFlags flags) { return super.send(rid, part, SendFlag.fromValue(flags.value())); }
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
    public boolean send(RoutingId rid, List<Message> parts) { return super.send(rid, parts); }
    public boolean send(RoutingId rid, List<Message> parts, SendFlags flags) { return super.send(rid, parts, SendFlag.fromValue(flags.value())); }
    SendResult sendNoWaitResult(RoutingId rid, List<Message> parts) {
        return super.sendNoWaitResult(rid, parts);
    }
    public Received recv() { return super.recv(); }
    public Received recv(RecvFlags flags) { return super.recv(ReceiveFlag.fromValue(flags.value())); }
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
