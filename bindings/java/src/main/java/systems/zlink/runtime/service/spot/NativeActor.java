/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.spot;

import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.errors.ZlinkRecvException;
import systems.zlink.contracts.service.spot.Actor;
import systems.zlink.contracts.service.spot.ActorJoinOperation;
import systems.zlink.contracts.service.spot.ActorJoinResult;
import systems.zlink.contracts.service.spot.ActorLeaveOperation;
import systems.zlink.contracts.service.spot.ActorReceived;
import systems.zlink.contracts.service.spot.ActorRef;
import systems.zlink.contracts.service.spot.SendOperation;
import systems.zlink.contracts.service.spot.SendSubmitOperation;
import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.contracts.service.spot.SpotNode;
import systems.zlink.contracts.sockets.RecvFlags;
import systems.zlink.contracts.sockets.RecvResult;
import systems.zlink.contracts.errors.ZlinkRequestException;
import systems.zlink.contracts.sockets.RequestResult;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.sockets.SubmitResult;
import systems.zlink.runtime.nativeapi.DurationConversions;
import systems.zlink.runtime.nativeapi.ActorInterop;
import systems.zlink.runtime.nativeapi.ActorRequestCallbacks;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.nativeapi.Native;
import systems.zlink.runtime.nativeapi.NativeLayouts;
import systems.zlink.runtime.nativeapi.NativeMessage;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.time.Duration;
import java.util.List;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;

final class NativeActor implements Actor {
    private SpotNode node;
    private ActorRef ref;

    NativeActor(SpotNode node, ActorRef ref) {
        this.node = Objects.requireNonNull(node, "node");
        this.ref = Objects.requireNonNull(ref, "ref");
    }

    @Override
    public ActorRef ref() {
        ensureOpen();
        return ref;
    }

    /**
     * Async user-Spot join operation builder. Completion delivers an
     * {@link ActorJoinResult} plus reply parts. {@code spot} must be a user
     * Spot.
     */
    @Override
    public ActorJoinOperation join(Spot spot) {
        Objects.requireNonNull(spot, "spot");
        ensureOpen();
        return new ActorJoinBuilder(spot);
    }

    /** Async leave operation builder for the supplied Spot. */
    @Override
    public ActorLeaveOperation leave(Spot spot) {
        Objects.requireNonNull(spot, "spot");
        ensureOpen();
        return new ActorLeaveBuilder(spot);
    }

    @Override
    public ActorReceived recv(RecvFlags flags) {
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
                    throw new ZlinkRecvException(RecvResult.fromValue(rc));
                }
                boolean hasMore = hasMoreOut.get(ValueLayout.JAVA_INT, 0) != 0;
                InternalAccess.messageFinishReceive(message, hasMore);
                success = true;
                return new ActorReceived(ActorInterop.actorRecvInfoFromNative(
                  infoOut), message, hasMore);
            } finally {
                if (!success) {
                    message.close();
                }
            }
        }
    }

    @Override
    public ActorReceived recv() {
        return recv(RecvFlags.NONE);
    }

    /** Actor-to-session relay operation builder. */
    @Override
    public SendOperation sendBoundSession() {
        ensureOpen();
        return new SendBoundSessionBuilder();
    }

    @Override
    public void closeBoundSession(Duration timeout) {
        ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotNodeActorCloseBoundSession(nodeHandle(),
              ActorInterop.actorRefToNative(arena, ref), DurationConversions.timeoutMillisOrZero(timeout));
            if (rc != 0) {
                throw new ZlinkRequestException(RequestResult.fromValue(rc));
            }
        }
    }

    @Override
    public void closeBoundSession() {
        closeBoundSession(Duration.ZERO);
    }

    @Override
    public void close() {
        close(Duration.ZERO);
    }

    @Override
    public void close(Duration timeout) {
        if (node == null) {
            return;
        }
        ActorRequestCallbacks.PendingToken pending =
          ActorRequestCallbacks.register((result, reply) -> {
              try {
                  if (result != RequestResult.OK) {
                      throw new ZlinkRequestException(result);
                  }
              } finally {
                  reply.forEach(Message::close);
              }
          });
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotNodeActorDestroy(nodeHandle(),
              ActorInterop.actorRefToNative(arena, ref),
              ActorRequestCallbacks.REPLY_CALLBACK,
              MemorySegment.ofAddress(pending.id()), DurationConversions.timeoutMillisOrZero(timeout));
            if (rc != 0) {
                ActorRequestCallbacks.remove(pending.id());
                throw new ZlinkRequestException(RequestResult.fromValue(rc));
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

    private final class ActorJoinBuilder extends ActorJoinSubmitOperationBase {
        private final Spot spot;

        ActorJoinBuilder(Spot spot) {
            this.spot = spot;
        }

        @Override
        protected MemorySegment nodeHandle() {
            return NativeActor.this.nodeHandle();
        }

        @Override
        protected MemorySegment actorRef(Arena arena) {
            return ActorInterop.actorRefToNative(arena, ref);
        }

        @Override
        protected MemorySegment destNodeRid(Arena arena) {
            return ActorInterop.nativeRoutingId(arena, node.getRoutingId());
        }

        @Override
        protected MemorySegment destSpotRid(Arena arena) {
            return ActorInterop.nativeRoutingId(arena, spot.getRoutingId());
        }

        @Override
        protected MemorySegment progressHandle() {
            return InternalAccess.spotHandle(spot);
        }

        @Override
        protected String progressName() {
            return "zlink-actor-join-progress";
        }

        @Override
        protected int timeoutMillis(Duration timeout) {
            return DurationConversions.timeoutMillisOrZero(timeout);
        }
    }

    private final class ActorLeaveBuilder extends ActorLeaveOperationBase {
        private final Spot spot;

        ActorLeaveBuilder(Spot spot) {
            this.spot = spot;
        }

        @Override
        protected MemorySegment nodeHandle() {
            return NativeActor.this.nodeHandle();
        }

        @Override
        protected MemorySegment actorRef(Arena arena) {
            return ActorInterop.actorRefToNative(arena, ref);
        }

        @Override
        protected MemorySegment spotRid(Arena arena) {
            return ActorInterop.nativeRoutingId(arena, spot.getRoutingId());
        }

        @Override
        protected int timeoutMillis(Duration timeout) {
            return DurationConversions.timeoutMillisOrZero(timeout);
        }
    }

    private final class SendBoundSessionBuilder extends SingleSubmitOperation
        implements SendOperation, SendSubmitOperation {

        @Override
        public SendSubmitOperation message(Message part) {
            addPart(part);
            return this;
        }

        @Override
        public SendSubmitOperation flags(SendFlags value) {
            setFlags(value);
            return this;
        }

        @Override
        public boolean submit() {
            ensureNotSubmitted();
            if (parts().isEmpty())
                throw new IllegalArgumentException("at least one message required");
            if (parts().size() != 1)
                throw new IllegalArgumentException(
                    "actor bound-session send requires exactly one message");
            markSubmitted();
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment refSegment = ActorInterop.actorRefToNative(arena, ref);
                Message part = parts().get(0);
                MemorySegment nativeMsg = arena.allocate(NativeLayouts.MESSAGE_LAYOUT);
                InternalAccess.messageCopyTo(part, nativeMsg);
                int rc = Native.spotNodeActorSendBoundSessionMessage(nodeHandle(),
                  refSegment, nativeMsg, flags().value());
                if (rc != 0) {
                    NativeMessage.messageClose(nativeMsg);
                    if (flags() == SendFlags.DONT_WAIT
                        && SubmitResult.fromValue(rc) == SubmitResult.BACKPRESSURED) {
                        return false;
                    }
                    throw new ZlinkSubmitException(SubmitResult.fromValue(rc));
                }
            }
            return true;
        }
    }
}
