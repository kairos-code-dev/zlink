/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.Message;
import systems.zlink.contracts.RecvException;
import systems.zlink.contracts.RecvFlags;
import systems.zlink.contracts.RecvResult;
import systems.zlink.contracts.RequestException;
import systems.zlink.contracts.RequestResult;
import systems.zlink.contracts.SendFlags;
import systems.zlink.contracts.SubmitException;
import systems.zlink.contracts.SubmitResult;
import systems.zlink.runtime.nativebridge.ActorInterop;
import systems.zlink.runtime.nativebridge.InternalAccess;
import systems.zlink.runtime.nativebridge.MessagePartsBuffer;
import systems.zlink.runtime.nativebridge.Native;
import systems.zlink.runtime.nativebridge.NativeLayouts;
import systems.zlink.runtime.nativebridge.NativeMsg;
import systems.zlink.runtime.nativebridge.RequestProgressPump;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.time.Duration;
import java.util.List;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;

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

    /**
     * Async user-Spot join operation builder. Completion delivers an
     * {@link ActorJoinResult} plus reply parts. {@code spot} must be a user
     * Spot.
     */
    public ActorJoinOp join(Spot spot) {
        Objects.requireNonNull(spot, "spot");
        ensureOpen();
        return new ActorJoinBuilder(spot);
    }

    /** Async leave operation builder for the supplied Spot. */
    public ActorLeaveOp leave(Spot spot) {
        Objects.requireNonNull(spot, "spot");
        ensureOpen();
        return new ActorLeaveBuilder(spot);
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

    /** Actor-to-session relay operation builder. */
    public SendOp sendBoundSession() {
        ensureOpen();
        return new SendBoundSessionBuilder();
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
        ActorRequestCallbacks.PendingToken pending =
          ActorRequestCallbacks.register((result, reply) -> {
              try {
                  if (result != RequestResult.OK) {
                      throw new RequestException(result);
                  }
              } finally {
                  reply.forEach(Message::close);
              }
          });
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotNodeActorDestroy(nodeHandle(),
              ActorInterop.actorRefToNative(arena, ref),
              ActorRequestCallbacks.REPLY_CALLBACK,
              MemorySegment.ofAddress(pending.id()), timeoutMillis(timeout));
            if (rc != 0) {
                ActorRequestCallbacks.remove(pending.id());
                throw new RequestException(RequestResult.fromValue(rc));
            }
            ActorRequestCallbacks.await(pending);
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

    private final class ActorJoinBuilder
      implements ActorJoinOp, ActorJoinSubmitOp {
        private final Spot spot;
        private final MessagePartsBuffer parts = new MessagePartsBuffer();
        private Duration timeout = Duration.ofMillis(5_000L);
        private SendFlags flags = SendFlags.NONE;
        private boolean submitted;

        ActorJoinBuilder(Spot spot) {
            this.spot = spot;
        }

        @Override
        public ActorJoinSubmitOp message(Message part) {
            ensureNotSubmitted();
            parts.add(Objects.requireNonNull(part, "part"));
            return this;
        }

        @Override
        public ActorJoinSubmitOp timeout(Duration value) {
            ensureNotSubmitted();
            timeout = Objects.requireNonNull(value, "timeout");
            return this;
        }

        @Override
        public ActorJoinCallbackSubmitOp flags(SendFlags value) {
            ensureNotSubmitted();
            flags = Objects.requireNonNull(value, "flags");
            return new ActorJoinCallbackStage(this);
        }

        @Override
        public CompletableFuture<ActorJoinCompletion> submitAsync() {
            CompletableFuture<ActorJoinCompletion> future = new CompletableFuture<>();
            submit((result, replyParts) -> {
                if (result.result() == RequestResult.OK) {
                    future.complete(new ActorJoinCompletion(result, replyParts));
                } else {
                    future.completeExceptionally(new RequestException(result.result()));
                }
            });
            return future;
        }

        @Override
        public boolean submit(ActorJoinHandler callback) {
            Objects.requireNonNull(callback, "callback");
            ensureNotSubmitted();
            if (parts.isEmpty())
                throw new IllegalArgumentException("at least one message required");
            submitted = true;
            ActorRequestCallbacks.JoinPendingToken token =
              ActorRequestCallbacks.registerJoin(callback);
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment partsArr = parts.copyToNativeArray(arena);
                int rc = Native.spotNodeActorJoinSpot(nodeHandle(),
                  ActorInterop.actorRefToNative(arena, ref),
                  ActorInterop.nativeRoutingId(arena, node.routingId()),
                  ActorInterop.nativeRoutingId(arena, spot.routingId()),
                  partsArr, parts.size(),
                  ActorRequestCallbacks.ACTOR_JOIN_CALLBACK,
                  MemorySegment.ofAddress(token.id()), flags.value(),
                  timeoutMillis(timeout));
                if (rc != 0) {
                    ActorRequestCallbacks.remove(token.id());
                    MessagePartsBuffer.closeNativeArray(partsArr, parts.size());
                    throw new SubmitException(SubmitResult.fromValue(rc));
                }
                RequestProgressPump.trackSpotRequest(token.future(),
                  InternalAccess.spotHandle(spot), "zlink-actor-join-progress");
            }
            return true;
        }

        private void ensureNotSubmitted() {
            if (submitted)
                throw new IllegalStateException("operation already submitted");
        }
    }

    private final class ActorJoinCallbackStage
      implements ActorJoinCallbackSubmitOp {
        private final ActorJoinBuilder builder;

        ActorJoinCallbackStage(ActorJoinBuilder builder) {
            this.builder = builder;
        }

        @Override
        public ActorJoinCallbackSubmitOp message(Message part) {
            builder.message(part);
            return this;
        }

        @Override
        public ActorJoinCallbackSubmitOp timeout(Duration timeout) {
            builder.timeout(timeout);
            return this;
        }

        @Override
        public ActorJoinCallbackSubmitOp flags(SendFlags flags) {
            builder.flags(flags);
            return this;
        }

        @Override
        public boolean submit(ActorJoinHandler callback) {
            return builder.submit(callback);
        }
    }

    private final class ActorLeaveBuilder implements ActorLeaveOp {
        private final Spot spot;
        private Duration timeout = Duration.ofMillis(5_000L);
        private boolean submitted;

        ActorLeaveBuilder(Spot spot) {
            this.spot = spot;
        }

        @Override
        public ActorLeaveOp timeout(Duration value) {
            ensureNotSubmitted();
            timeout = Objects.requireNonNull(value, "timeout");
            return this;
        }

        @Override
        public CompletableFuture<List<Message>> submitAsync() {
            CompletableFuture<List<Message>> future = new CompletableFuture<>();
            submit((result, parts) -> {
                if (result == RequestResult.OK) {
                    future.complete(parts);
                } else {
                    future.completeExceptionally(new RequestException(result));
                }
            });
            return future;
        }

        @Override
        public boolean submit(ReplyHandler callback) {
            Objects.requireNonNull(callback, "callback");
            ensureNotSubmitted();
            submitted = true;
            ActorRequestCallbacks.PendingToken token =
              ActorRequestCallbacks.register(callback::onReply);
            try (Arena arena = Arena.ofConfined()) {
                int rc = Native.spotNodeActorLeaveSpot(nodeHandle(),
                  ActorInterop.actorRefToNative(arena, ref),
                  ActorInterop.nativeRoutingId(arena, spot.routingId()),
                  ActorRequestCallbacks.REPLY_CALLBACK,
                  MemorySegment.ofAddress(token.id()),
                  timeoutMillis(timeout));
                if (rc != 0) {
                    ActorRequestCallbacks.remove(token.id());
                    throw new SubmitException(SubmitResult.fromValue(rc));
                }
            }
            return true;
        }

        private void ensureNotSubmitted() {
            if (submitted)
                throw new IllegalStateException("operation already submitted");
        }
    }

    private final class SendBoundSessionBuilder
      implements SendOp, SendSubmitOp {
        private final MessagePartsBuffer parts = new MessagePartsBuffer();
        private SendFlags flags = SendFlags.NONE;
        private boolean submitted;

        @Override
        public SendSubmitOp message(Message part) {
            ensureNotSubmitted();
            parts.add(Objects.requireNonNull(part, "part"));
            return this;
        }

        @Override
        public SendSubmitOp flags(SendFlags value) {
            ensureNotSubmitted();
            flags = Objects.requireNonNull(value, "flags");
            return this;
        }

        @Override
        public boolean submit() {
            ensureNotSubmitted();
            if (parts.isEmpty())
                throw new IllegalArgumentException("at least one message required");
            submitted = true;
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment refSegment = ActorInterop.actorRefToNative(arena, ref);
                for (int i = 0; i < parts.size(); i++) {
                    Message part = parts.get(i);
                    MemorySegment nativeMsg = arena.allocate(NativeLayouts.MSG_LAYOUT);
                    InternalAccess.messageCopyTo(part, nativeMsg);
                    int rc = Native.spotNodeActorSendBoundSessionMsg(nodeHandle(),
                      refSegment, nativeMsg, flags.value());
                    if (rc != 0) {
                        NativeMsg.msgClose(nativeMsg);
                        if (flags == SendFlags.DONT_WAIT
                            && SubmitResult.fromValue(rc) == SubmitResult.BACKPRESSURED) {
                            return false;
                        }
                        throw new SubmitException(SubmitResult.fromValue(rc));
                    }
                }
            }
            return true;
        }

        private void ensureNotSubmitted() {
            if (submitted)
                throw new IllegalStateException("operation already submitted");
        }
    }
}
