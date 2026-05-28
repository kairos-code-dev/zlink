/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.sockets;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.errors.ConfigException;
import systems.zlink.contracts.errors.ConfigResult;
import systems.zlink.contracts.errors.SubmitException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.spot.ActorRef;
import systems.zlink.contracts.service.spot.ReplyHandler;
import systems.zlink.contracts.service.spot.SpotNode;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.sockets.StreamSocket;
import systems.zlink.contracts.sockets.SubmitResult;
import systems.zlink.runtime.nativeapi.ActorInterop;
import systems.zlink.runtime.nativeapi.ActorRequestCallbacks;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.nativeapi.Native;
import systems.zlink.runtime.nativeapi.NativeHelpers;
import systems.zlink.runtime.nativeapi.NativeLayouts;
import systems.zlink.runtime.nativeapi.NativeMsg;
import systems.zlink.runtime.nativeapi.RequestProgressPump;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

final class NativeStreamActorSupport {
    private NativeStreamActorSupport() {
    }

    public static void attachActorGateway(StreamSocket socket, SpotNode node) {
        Objects.requireNonNull(socket, "socket");
        Objects.requireNonNull(node, "node");
        int rc = Native.streamAttachActorGateway(
            InternalAccess.socketHandle(socket), InternalAccess.spotNodeHandle(node));
        if (rc != 0) {
            throw new ConfigException(ConfigResult.fromValue(rc));
        }
    }

    public static List<ActorRef> boundActors(StreamSocket socket,
                                             RoutingId sessionRid) {
        Objects.requireNonNull(socket, "socket");
        Objects.requireNonNull(sessionRid, "sessionRid");
        try (Arena arena = Arena.ofConfined()) {
            long capacity = 256L;
            MemorySegment entries = arena.allocate(
              NativeLayouts.ACTOR_REF_LAYOUT, capacity);
            MemorySegment count =
              arena.allocate(java.lang.foreign.ValueLayout.JAVA_LONG);
            count.set(java.lang.foreign.ValueLayout.JAVA_LONG, 0, capacity);
            int rc = Native.streamBoundActors(
              InternalAccess.socketHandle(socket),
              ActorInterop.nativeRoutingId(arena, sessionRid), entries, count);
            if (rc != 0) {
                throw new ConfigException(ConfigResult.fromValue(rc));
            }
            long actual = count.get(java.lang.foreign.ValueLayout.JAVA_LONG, 0);
            ArrayList<ActorRef> refs = new ArrayList<>((int) actual);
            long stride = NativeLayouts.ACTOR_REF_LAYOUT.byteSize();
            for (int i = 0; i < actual; i++) {
                refs.add(ActorInterop.actorRefFromNative(
                  entries.asSlice(i * stride, stride)));
            }
            return List.copyOf(refs);
        }
    }

    public static boolean submitBind(StreamSocket socket,
                                     RoutingId sessionRid,
                                     ActorRef actor,
                                     Duration timeout,
                                     ReplyHandler callback) {
        Objects.requireNonNull(socket, "socket");
        Objects.requireNonNull(sessionRid, "sessionRid");
        Objects.requireNonNull(actor, "actor");
        Objects.requireNonNull(callback, "callback");
        ActorRequestCallbacks.PendingToken token =
          ActorRequestCallbacks.register(callback::onReply);
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.streamBindActor(InternalAccess.socketHandle(socket),
              ActorInterop.nativeRoutingId(arena, sessionRid),
              ActorInterop.actorRefToNative(arena, actor),
              ActorRequestCallbacks.REPLY_CALLBACK,
              MemorySegment.ofAddress(token.id()), timeoutMillis(timeout));
            if (rc != 0) {
                ActorRequestCallbacks.remove(token.id());
                throw new SubmitException(SubmitResult.fromValue(rc));
            }
            RequestProgressPump.trackSocketRequest(token.future(),
              InternalAccess.socketHandle(socket),
              "zlink-stream-bind-actor-progress");
            return true;
        }
    }

    public static boolean submitUnbind(StreamSocket socket,
                                       RoutingId sessionRid,
                                       String actorId,
                                       Duration timeout,
                                       ReplyHandler callback) {
        Objects.requireNonNull(socket, "socket");
        Objects.requireNonNull(sessionRid, "sessionRid");
        Objects.requireNonNull(actorId, "actorId");
        Objects.requireNonNull(callback, "callback");
        ActorRequestCallbacks.PendingToken token =
          ActorRequestCallbacks.register(callback::onReply);
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.streamUnbindActor(
              InternalAccess.socketHandle(socket),
              ActorInterop.nativeRoutingId(arena, sessionRid),
              NativeHelpers.toCString(arena, actorId),
              ActorRequestCallbacks.REPLY_CALLBACK,
              MemorySegment.ofAddress(token.id()), timeoutMillis(timeout));
            if (rc != 0) {
                ActorRequestCallbacks.remove(token.id());
                throw new SubmitException(SubmitResult.fromValue(rc));
            }
            RequestProgressPump.trackSocketRequest(token.future(),
              InternalAccess.socketHandle(socket),
              "zlink-stream-unbind-actor-progress");
            return true;
        }
    }

    public static boolean sendBoundActorParts(StreamSocket socket,
                                              RoutingId sessionRid,
                                              String actorId,
                                              List<Message> parts,
                                              SendFlags flags) {
        Objects.requireNonNull(socket, "socket");
        Objects.requireNonNull(sessionRid, "sessionRid");
        Objects.requireNonNull(actorId, "actorId");
        Objects.requireNonNull(parts, "parts");
        if (parts.isEmpty()) {
            throw new IllegalArgumentException("at least one message required");
        }
        for (int i = 0; i < parts.size(); i++) {
            Objects.requireNonNull(parts.get(i), "parts[" + i + "]");
        }
        Objects.requireNonNull(flags, "flags");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeSessionRid =
              ActorInterop.nativeRoutingId(arena, sessionRid);
            MemorySegment nativeActorId =
              NativeHelpers.toCString(arena, actorId);
            MemorySegment socketHandle = InternalAccess.socketHandle(socket);
            for (int i = 0; i < parts.size(); i++) {
                MemorySegment nativeMsg =
                  arena.allocate(NativeLayouts.MSG_LAYOUT);
                InternalAccess.messageCopyTo(parts.get(i), nativeMsg);
                int more = i + 1 < parts.size()
                  ? Native.PART_MORE
                  : Native.PART_FINAL;
                int rc = Native.streamSendBoundActorPart(socketHandle,
                  nativeSessionRid, nativeActorId, nativeMsg, flags.value(),
                  more);
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

    private static int timeoutMillis(Duration timeout) {
        if (timeout == null || timeout.isZero()) {
            return 0;
        }
        long millis = Math.max(1L, timeout.toMillis());
        return millis >= Integer.MAX_VALUE ? Integer.MAX_VALUE : (int) millis;
    }
}
