/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.service.spot;

import systems.zlink.Message;
import systems.zlink.RecvException;
import systems.zlink.RecvFlags;
import systems.zlink.RecvResult;
import systems.zlink.RequestCallback;
import systems.zlink.RequestException;
import systems.zlink.RequestResult;
import systems.zlink.SendFlags;
import systems.zlink.SubmitException;
import systems.zlink.SubmitResult;
import systems.zlink.internal.ActorInterop;
import systems.zlink.internal.InternalAccess;
import systems.zlink.internal.Native;
import systems.zlink.internal.NativeLayouts;
import systems.zlink.internal.NativeMsg;
import systems.zlink.internal.RequestProgressPump;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.time.Duration;
import java.util.List;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.function.BiConsumer;

public final class Actor implements AutoCloseable {
    private SpotNode node;
    private ActorRef ref;

    Actor(SpotNode node, ActorRef ref) {
        this.node = Objects.requireNonNull(node, "node");
        this.ref = Objects.requireNonNull(ref, "ref");
    }

    ActorRef refInternal() {
        return ref;
    }

    public ActorRef ref() {
        ensureOpen();
        return ref;
    }

    public CompletableFuture<List<Message>> join(Spot spot, Message message) {
        return join(spot, message, Duration.ofMillis(5_000L));
    }

    public CompletableFuture<List<Message>> join(Spot spot, Message message,
                                                 Duration timeout) {
        CompletableFuture<List<Message>> future = new CompletableFuture<>();
        join(spot, message,
          (BiConsumer<RequestResult, List<Message>>) (result, parts) -> {
            if (result == RequestResult.OK) {
                future.complete(parts);
            } else {
                future.completeExceptionally(new RequestException(result));
            }
        }, timeout);
        return future;
    }

    public boolean join(Spot spot, Message message,
                        RequestCallback callback) {
        return join(spot, message, callback, Duration.ofMillis(5_000L));
    }

    public boolean join(Spot spot, Message message,
                        RequestCallback callback,
                        Duration timeout) {
        return join(spot, message, callback, SendFlags.NONE, timeout);
    }

    public boolean join(Spot spot, Message message,
                        RequestCallback callback,
                        SendFlags flags) {
        return join(spot, message, callback, flags, Duration.ofMillis(5_000L));
    }

    public boolean join(Spot spot, Message message,
                        RequestCallback callback,
                        SendFlags flags,
                        Duration timeout) {
        return joinInternal(spot, message, callback::onComplete, flags, timeout);
    }

    boolean join(Spot spot,
                        Message message,
                        BiConsumer<RequestResult, List<Message>> callback,
                        Duration timeout) {
        return joinInternal(spot, message, callback, SendFlags.NONE, timeout);
    }

    private boolean joinInternal(Spot spot,
                        Message message,
                        BiConsumer<RequestResult, List<Message>> callback,
                        SendFlags flags,
                        Duration timeout) {
        Objects.requireNonNull(spot, "spot");
        Objects.requireNonNull(message, "message");
        Objects.requireNonNull(callback, "callback");
        Objects.requireNonNull(flags, "flags");
        ensureOpen();
        ActorRequestCallbacks.PendingToken pending =
          ActorRequestCallbacks.register(callback);
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeMsg = arena.allocate(NativeLayouts.MSG_LAYOUT);
            InternalAccess.messageCopyTo(message, nativeMsg);
            int rc = Native.spotNodeActorJoinSpot(nodeHandle(),
              ActorInterop.actorRefToNative(arena, ref),
              ActorInterop.nativeRoutingId(arena, node.routingId()),
              ActorInterop.nativeRoutingId(arena, spot.routingId()), nativeMsg,
              1L, ActorRequestCallbacks.REPLY_CALLBACK,
              MemorySegment.ofAddress(pending.id()), flags.value(),
              timeoutMillis(timeout));
            if (rc != 0) {
                ActorRequestCallbacks.remove(pending.id());
                NativeMsg.msgClose(nativeMsg);
                throw new SubmitException(SubmitResult.fromValue(rc));
            }
            RequestProgressPump.trackSpotRequest(pending.future(),
              InternalAccess.spotHandle(spot), "zlink-actor-join-progress");
            return true;
        }
    }

    boolean join(Spot spot,
                        Message message,
                        BiConsumer<RequestResult, List<Message>> callback) {
        return join(spot, message, callback, Duration.ofMillis(5_000L));
    }

    public void leave(Spot spot) {
        leave(spot, Duration.ofMillis(5_000L));
    }

    public void leave(Spot spot, Duration timeout) {
        Objects.requireNonNull(spot, "spot");
        ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotNodeActorLeaveSpot(nodeHandle(),
              ActorInterop.actorRefToNative(arena, ref),
              ActorInterop.nativeRoutingId(arena, spot.routingId()),
              timeoutMillis(timeout));
            if (rc != 0) {
                throw new RequestException(RequestResult.fromValue(rc));
            }
        }
    }

    public ActorPart recvPart(RecvFlags flags) {
        Objects.requireNonNull(flags, "flags");
        ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment infoOut = arena.allocate(
              NativeLayouts.ACTOR_RECV_INFO_LAYOUT);
            MemorySegment hasMoreOut = arena.allocate(ValueLayout.JAVA_INT);
            Message message = new Message();
            boolean success = false;
            try {
                int rc = Native.spotNodeActorRecvPart(nodeHandle(),
                  ActorInterop.actorRefToNative(arena, ref), infoOut,
                  InternalAccess.messageNativeHandle(message), hasMoreOut,
                  flags.value());
                if (rc != 0) {
                    if (flags == RecvFlags.DONT_WAIT
                        && rc == RecvResult.NO_DATA.value()) {
                        return null;
                    }
                    throw new RecvException(RecvResult.fromValue(rc));
                }
                boolean hasMore = hasMoreOut.get(ValueLayout.JAVA_INT, 0) != 0;
                InternalAccess.messageFinishReceive(message, hasMore);
                success = true;
                return new ActorPart(ActorInterop.actorRecvInfoFromNative(
                  infoOut), message, hasMore);
            } finally {
                if (!success) {
                    message.close();
                }
            }
        }
    }

    public ActorPart recvPart() {
        return recvPart(RecvFlags.NONE);
    }

    public boolean sendBoundSession(Message message, SendFlags flags) {
        Objects.requireNonNull(message, "message");
        Objects.requireNonNull(flags, "flags");
        ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeMsg = arena.allocate(NativeLayouts.MSG_LAYOUT);
            InternalAccess.messageCopyTo(message, nativeMsg);
            int rc = Native.spotNodeActorSendBoundSessionMsg(nodeHandle(),
              ActorInterop.actorRefToNative(arena, ref), nativeMsg, flags.value());
            if (rc != 0) {
                NativeMsg.msgClose(nativeMsg);
                throw new SubmitException(SubmitResult.fromValue(rc));
            }
            return true;
        }
    }

    public boolean sendBoundSession(Message message) {
        return sendBoundSession(message, SendFlags.NONE);
    }

    public void closeBoundSession(Duration timeout) {
        ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotNodeActorCloseBoundSession(nodeHandle(),
              ActorInterop.actorRefToNative(arena, ref), timeoutMillis(timeout));
            if (rc != 0) {
                throw new RequestException(RequestResult.fromValue(rc));
            }
        }
    }

    public void closeBoundSession() {
        closeBoundSession(Duration.ZERO);
    }

    @Override
    public void close() {
        close(Duration.ZERO);
    }

    public void close(Duration timeout) {
        if (node == null) {
            return;
        }
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotNodeActorDestroy(nodeHandle(),
              ActorInterop.actorRefToNative(arena, ref), timeoutMillis(timeout));
            if (rc != 0) {
                throw new RequestException(RequestResult.fromValue(rc));
            }
        }
        node = null;
        ref = null;
    }

    private void ensureOpen() {
        if (node == null || ref == null) {
            throw new IllegalStateException("actor is closed");
        }
    }

    private MemorySegment nodeHandle() {
        return InternalAccess.spotNodeHandle(node);
    }

    private static int timeoutMillis(Duration timeout) {
        if (timeout == null || timeout.isZero()) {
            return 0;
        }
        long millis = Math.max(1L, timeout.toMillis());
        return millis >= Integer.MAX_VALUE ? Integer.MAX_VALUE : (int) millis;
    }
}
