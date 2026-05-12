/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink;

import systems.zlink.internal.ActorInterop;
import systems.zlink.internal.InternalAccess;
import systems.zlink.internal.Native;
import systems.zlink.internal.NativeHelpers;
import systems.zlink.internal.NativeLayouts;
import systems.zlink.internal.NativeMsg;
import systems.zlink.service.spot.ActorRef;
import systems.zlink.service.spot.SpotNode;
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
    public void setRoutingId(RoutingId rid) { super.setRoutingId(rid); }
    public RoutingId routingId() { return super.routingId(); }
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
    /** Canonical caller-provided storage recv. See doc/spec/bindings/README.md. */
    public boolean recv(Received result, RecvFlags flags) {
        Objects.requireNonNull(result, "result");
        Objects.requireNonNull(flags, "flags");
        Received fresh = super.recv(ReceiveFlag.fromValue(flags.value()));
        if (fresh == null) return false;
        result.adoptFrom(fresh);
        result.routingId().ifPresent(rid ->
            result.setSendSender((parts, sendFlags) -> send(rid, parts,
                sendFlags)));
        return true;
    }
    public void onSendReady(SendReadyHandler handler) { super.onSendReady(handler); }
    public void onPacket(StreamPacketHandler handler) {
        Objects.requireNonNull(handler, "handler");
        super.attachStreamPacket((StreamFramedPacketHandler)
            (routingId, header, body) -> handler.onPacket(routingId, header,
                body));
    }
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
                if (flags == SendFlags.DONT_WAIT
                    && SubmitResult.fromValue(rc) == SubmitResult.BACKPRESSURED) {
                    return false;
                }
                throw new SubmitException(SubmitResult.fromValue(rc));
            }
            return true;
        }
    }

    public boolean sendBoundActor(SpotNode node, RoutingId sessionRid,
                                  String actorId, Message part) {
        return sendBoundActor(node, sessionRid, actorId, part, SendFlags.NONE);
    }

    public boolean sendBoundActor(SpotNode node, RoutingId sessionRid,
                                  String actorId, List<Message> parts,
                                  SendFlags flags) {
        Objects.requireNonNull(parts, "parts");
        if (parts.isEmpty()) {
            throw new IllegalArgumentException("parts must not be empty");
        }
        for (int i = 0; i < parts.size(); i++) {
            Objects.requireNonNull(parts.get(i), "parts[" + i + "]");
        }
        Objects.requireNonNull(node, "node");
        Objects.requireNonNull(sessionRid, "sessionRid");
        Objects.requireNonNull(actorId, "actorId");
        Objects.requireNonNull(flags, "flags");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeSessionRid =
              ActorInterop.nativeRoutingId(arena, sessionRid);
            MemorySegment nativeActorId =
              NativeHelpers.toCString(arena, actorId);
            MemorySegment nodeHandle = InternalAccess.spotNodeHandle(node);
            MemorySegment socketHandle = handle();
            for (int i = 0; i < parts.size(); i++) {
                MemorySegment nativeMsg = arena.allocate(NativeLayouts.MSG_LAYOUT);
                InternalAccess.messageCopyTo(parts.get(i), nativeMsg);
                int more = i + 1 < parts.size()
                  ? Native.PART_MORE
                  : Native.PART_FINAL;
                int rc = Native.streamSendBoundActorPart(nodeHandle,
                  socketHandle, nativeSessionRid, nativeActorId, nativeMsg,
                  flags.value(), more);
                if (rc != 0) {
                    NativeMsg.msgClose(nativeMsg);
                    if (flags == SendFlags.DONT_WAIT
                        && SubmitResult.fromValue(rc) == SubmitResult.BACKPRESSURED) {
                        return false;
                    }
                    throw new SubmitException(SubmitResult.fromValue(rc));
                }
            }
            return true;
        }
    }

    public boolean sendBoundActor(SpotNode node, RoutingId sessionRid,
                                  String actorId, List<Message> parts) {
        return sendBoundActor(node, sessionRid, actorId, parts, SendFlags.NONE);
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
