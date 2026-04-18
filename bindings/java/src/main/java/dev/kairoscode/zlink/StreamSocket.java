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
    public void send(int rid, Message part) {
        super.send(rid, part, SendFlag.NONE);
    }
    public void send(int rid, Message part, SendFlags flags) {
        super.send(rid, part, SendFlag.fromValue(flags.value()));
    }
    public void send(RoutingId rid, Message part) { super.send(rid, part); }
    public void send(RoutingId rid, Message part, SendFlags flags) { super.send(rid, part, SendFlag.fromValue(flags.value())); }
    public int send(int rid, MemorySegment payload, int length, SendFlags flags) {
        return super.send(rid, payload, length, flags.value());
    }
    public int sendCopied(int rid, MemorySegment payload, int length,
                          SendFlags flags) {
        return super.sendCopied(rid, payload, length, flags.value());
    }
    public void send(RoutingId rid, List<Message> parts) { super.send(rid, parts); }
    public void send(RoutingId rid, List<Message> parts, SendFlags flags) { super.send(rid, parts, SendFlag.fromValue(flags.value())); }
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
