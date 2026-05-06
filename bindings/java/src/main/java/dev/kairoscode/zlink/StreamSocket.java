/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.ActorInterop;
import dev.kairoscode.zlink.internal.InternalAccess;
import dev.kairoscode.zlink.internal.Native;
import dev.kairoscode.zlink.internal.NativeHelpers;
import dev.kairoscode.zlink.internal.NativeLayouts;
import dev.kairoscode.zlink.internal.NativeMsg;
import dev.kairoscode.zlink.service.spot.ActorRef;
import dev.kairoscode.zlink.service.spot.SpotNode;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.time.Duration;
import java.util.List;
import java.util.Objects;

public final class StreamSocket extends Socket {
    private final StreamSocketOptions options = new StreamSocketOptions(this);

    public StreamSocket(Context ctx) {
        super(ctx, SocketType.STREAM);
    }

    public void bind(String endpoint) { super.bind(endpoint); }
    public void unbind(String endpoint) { super.unbind(endpoint); }
    boolean send(int rid, Message part) {
        return super.send(RoutingId.fromU32(rid), part, SendFlag.NONE);
    }
    boolean send(int rid, Message part, SendFlags flags) {
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
    int send(int rid, MemorySegment payload, int length, SendFlags flags) {
        return super.send(rid, payload, length, flags.value());
    }
    int sendCopied(int rid, MemorySegment payload, int length,
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
    void onPacketNative(StreamUInt32RawNativeHandler handler) {
        super.attachStreamRaw(handler);
    }
    void onFramedPacket(StreamFramedPacketHandler handler) {
        super.attachStreamPacket(handler);
    }
    void onFramedPacket(StreamUInt32FramedPacketHandler handler) {
        super.attachStreamPacket(handler);
    }
    void onFramedPacketNative(StreamUInt32FramedNativeHandler handler) {
        super.attachStreamPacket(handler);
    }
    void detachStream() { super.detachStream(); }
    public void bindActor(SpotNode node, RoutingId sessionRid, ActorRef actor,
                          Duration timeout) {
        Objects.requireNonNull(node, "node");
        Objects.requireNonNull(sessionRid, "sessionRid");
        Objects.requireNonNull(actor, "actor");
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.streamBindActor(
              InternalAccess.spotNodeHandle(node),
              handle(),
              ActorInterop.nativeRoutingId(arena, sessionRid),
              ActorInterop.actorRefToNative(arena, actor),
              timeoutMillis(timeout));
            if (rc != 0) {
                throw new RequestException(RequestResult.fromValue(rc));
            }
        }
    }

    public void bindActor(SpotNode node, RoutingId sessionRid, ActorRef actor) {
        bindActor(node, sessionRid, actor, Duration.ofMillis(5_000L));
    }

    public void unbindActor(SpotNode node, RoutingId sessionRid,
                            String actorId, Duration timeout) {
        Objects.requireNonNull(node, "node");
        Objects.requireNonNull(sessionRid, "sessionRid");
        Objects.requireNonNull(actorId, "actorId");
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.streamUnbindActor(
              InternalAccess.spotNodeHandle(node),
              handle(),
              ActorInterop.nativeRoutingId(arena, sessionRid),
              NativeHelpers.toCString(arena, actorId),
              timeoutMillis(timeout));
            if (rc != 0) {
                throw new RequestException(RequestResult.fromValue(rc));
            }
        }
    }

    public void unbindActor(SpotNode node, RoutingId sessionRid,
                            String actorId) {
        unbindActor(node, sessionRid, actorId, Duration.ofMillis(5_000L));
    }

    public boolean sendBoundActor(SpotNode node, RoutingId sessionRid,
                                  String actorId, Message part,
                                  SendFlags flags) {
        Objects.requireNonNull(node, "node");
        Objects.requireNonNull(sessionRid, "sessionRid");
        Objects.requireNonNull(actorId, "actorId");
        Objects.requireNonNull(part, "part");
        Objects.requireNonNull(flags, "flags");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeMsg = arena.allocate(NativeLayouts.MSG_LAYOUT);
            InternalAccess.messageCopyTo(part, nativeMsg);
            int rc = Native.streamSendBoundActorPart(
              InternalAccess.spotNodeHandle(node),
              handle(),
              ActorInterop.nativeRoutingId(arena, sessionRid),
              NativeHelpers.toCString(arena, actorId),
              nativeMsg, flags.value(), Native.PART_FINAL);
            if (rc != 0) {
                NativeMsg.msgClose(nativeMsg);
                throw new SubmitException(SubmitResult.fromValue(rc));
            }
            return true;
        }
    }

    public boolean sendBoundActor(SpotNode node, RoutingId sessionRid,
                                  String actorId, Message part) {
        return sendBoundActor(node, sessionRid, actorId, part, SendFlags.NONE);
    }
    @Override public StreamSocketOptions options() { return options; }

    private static int timeoutMillis(Duration timeout) {
        if (timeout == null || timeout.isZero()) {
            return 0;
        }
        long millis = Math.max(1L, timeout.toMillis());
        return millis >= Integer.MAX_VALUE ? Integer.MAX_VALUE : (int) millis;
    }
}
