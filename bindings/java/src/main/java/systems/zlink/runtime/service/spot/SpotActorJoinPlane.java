/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.spot;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.time.Duration;
import java.util.List;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.errors.ZlinkRequestException;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.spot.ActorJoinCallbackSubmitOperation;
import systems.zlink.contracts.service.spot.ActorJoinCompletion;
import systems.zlink.contracts.service.spot.ActorJoinEntrySpotCompletion;
import systems.zlink.contracts.service.spot.ActorJoinEntrySpotHandler;
import systems.zlink.contracts.service.spot.ActorJoinEntrySpotOperation;
import systems.zlink.contracts.service.spot.ActorJoinHandler;
import systems.zlink.contracts.service.spot.ActorJoinOperation;
import systems.zlink.contracts.service.spot.ActorJoinSubmitOperation;
import systems.zlink.contracts.service.spot.ActorLeaveOperation;
import systems.zlink.contracts.service.spot.ActorRef;
import systems.zlink.contracts.service.spot.ReplyHandler;
import systems.zlink.contracts.sockets.RequestResult;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.sockets.SubmitResult;
import systems.zlink.runtime.nativeapi.ActorInterop;
import systems.zlink.runtime.nativeapi.ActorRequestCallbacks;
import systems.zlink.runtime.nativeapi.MessagePartsBuffer;
import systems.zlink.runtime.nativeapi.Native;

final class SpotActorJoinPlane {
    private final NativeSpotNode node;

    SpotActorJoinPlane(NativeSpotNode node) {
        this.node = node;
    }

    ActorJoinOperation joinActor(ActorRef actor, RoutingId destNodeRid,
                                 RoutingId destSpotRid) {
        Objects.requireNonNull(actor, "actor");
        Objects.requireNonNull(destNodeRid, "destNodeRid");
        Objects.requireNonNull(destSpotRid, "destSpotRid");
        node.ensureOpen();
        return new ActorJoinBuilder(actor, destNodeRid, destSpotRid);
    }

    ActorJoinEntrySpotOperation joinActorEntrySpot(ActorRef actor,
                                                   RoutingId destNodeRid) {
        Objects.requireNonNull(actor, "actor");
        Objects.requireNonNull(destNodeRid, "destNodeRid");
        node.ensureOpen();
        return new ActorJoinEntrySpotBuilder(actor, destNodeRid);
    }

    ActorLeaveOperation leaveActor(ActorRef actor, RoutingId currentSpotRid) {
        Objects.requireNonNull(actor, "actor");
        Objects.requireNonNull(currentSpotRid, "currentSpotRid");
        node.ensureOpen();
        return new ActorLeaveBuilder(actor, currentSpotRid);
    }

    private final class ActorLeaveBuilder implements ActorLeaveOperation {
        private final ActorRef actor;
        private final RoutingId currentSpotRid;
        private Duration timeout = Duration.ofMillis(5_000L);
        private boolean submitted;

        ActorLeaveBuilder(ActorRef actor, RoutingId currentSpotRid) {
            this.actor = actor;
            this.currentSpotRid = currentSpotRid;
        }

        @Override
        public ActorLeaveOperation timeout(Duration value) {
            ensureNotSubmitted();
            timeout = Objects.requireNonNull(value, "timeout");
            return this;
        }

        @Override
        public CompletableFuture<List<Message>> submit() {
            CompletableFuture<List<Message>> future = new CompletableFuture<>();
            submit((result, parts) -> {
                if (result == RequestResult.OK) {
                    future.complete(parts);
                } else {
                    future.completeExceptionally(
                        new ZlinkRequestException(result));
                }
            });
            return future;
        }

        @Override
        public boolean submit(ReplyHandler callback) {
            Objects.requireNonNull(callback, "callback");
            markSubmitted();
            ActorRequestCallbacks.PendingToken token =
                ActorRequestCallbacks.register(callback::onReply);
            try (Arena arena = Arena.ofConfined()) {
                int rc = Native.spotNodeActorLeaveSpot(node.handle(),
                    ActorInterop.actorRefToNative(arena, actor),
                    ActorInterop.nativeRoutingId(arena, currentSpotRid),
                    ActorRequestCallbacks.REPLY_CALLBACK,
                    MemorySegment.ofAddress(token.id()),
                    NativeSpotNode.timeoutMillis(timeout));
                if (rc != 0) {
                    ActorRequestCallbacks.remove(token.id());
                    throw new ZlinkSubmitException(SubmitResult.fromValue(rc));
                }
            }
            return true;
        }

        private void markSubmitted() {
            ensureNotSubmitted();
            submitted = true;
        }

        private void ensureNotSubmitted() {
            if (submitted)
                throw new IllegalStateException("operation already submitted");
        }
    }

    private final class ActorJoinBuilder
        implements ActorJoinOperation, ActorJoinSubmitOperation {
        private final ActorRef actor;
        private final RoutingId destNodeRid;
        private final RoutingId destSpotRid;
        private final MessagePartsBuffer parts = new MessagePartsBuffer();
        private Duration timeout = Duration.ofMillis(5_000L);
        private SendFlags flags = SendFlags.NONE;
        private boolean submitted;

        ActorJoinBuilder(ActorRef actor, RoutingId destNodeRid,
                         RoutingId destSpotRid) {
            this.actor = actor;
            this.destNodeRid = destNodeRid;
            this.destSpotRid = destSpotRid;
        }

        @Override
        public ActorJoinSubmitOperation message(Message part) {
            ensureNotSubmitted();
            parts.add(Objects.requireNonNull(part, "part"));
            return this;
        }

        @Override
        public ActorJoinSubmitOperation timeout(Duration value) {
            ensureNotSubmitted();
            timeout = Objects.requireNonNull(value, "timeout");
            return this;
        }

        @Override
        public ActorJoinCallbackSubmitOperation flags(SendFlags value) {
            ensureNotSubmitted();
            flags = Objects.requireNonNull(value, "flags");
            return new ActorJoinCallbackStage(this);
        }

        @Override
        public CompletableFuture<ActorJoinCompletion> submit() {
            CompletableFuture<ActorJoinCompletion> future =
                new CompletableFuture<>();
            submit((result, replyParts) -> {
                if (result.result() == RequestResult.OK) {
                    future.complete(new ActorJoinCompletion(result, replyParts));
                } else {
                    future.completeExceptionally(
                        new ZlinkRequestException(result.result()));
                }
            });
            return future;
        }

        @Override
        public boolean submit(ActorJoinHandler callback) {
            Objects.requireNonNull(callback, "callback");
            markSubmitted();
            if (parts.isEmpty())
                throw new IllegalArgumentException("at least one message required");
            ActorRequestCallbacks.JoinPendingToken token =
                ActorRequestCallbacks.registerJoin(callback);
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment partsArr = parts.copyToNativeArray(arena);
                int rc = Native.spotNodeActorJoinSpot(node.handle(),
                    ActorInterop.actorRefToNative(arena, actor),
                    ActorInterop.nativeRoutingId(arena, destNodeRid),
                    ActorInterop.nativeRoutingId(arena, destSpotRid),
                    partsArr, parts.size(),
                    ActorRequestCallbacks.ACTOR_JOIN_CALLBACK,
                    MemorySegment.ofAddress(token.id()), flags.value(),
                    NativeSpotNode.timeoutMillis(timeout));
                if (rc != 0) {
                    ActorRequestCallbacks.remove(token.id());
                    MessagePartsBuffer.closeNativeArray(partsArr, parts.size());
                    throw new ZlinkSubmitException(SubmitResult.fromValue(rc));
                }
            }
            return true;
        }

        private void markSubmitted() {
            ensureNotSubmitted();
            submitted = true;
        }

        private void ensureNotSubmitted() {
            if (submitted)
                throw new IllegalStateException("operation already submitted");
        }
    }

    private final class ActorJoinCallbackStage
        implements ActorJoinCallbackSubmitOperation {
        private final ActorJoinBuilder builder;

        ActorJoinCallbackStage(ActorJoinBuilder builder) {
            this.builder = builder;
        }

        @Override
        public ActorJoinCallbackSubmitOperation message(Message part) {
            builder.message(part);
            return this;
        }

        @Override
        public ActorJoinCallbackSubmitOperation timeout(Duration timeout) {
            builder.timeout(timeout);
            return this;
        }

        @Override
        public ActorJoinCallbackSubmitOperation flags(SendFlags flags) {
            builder.flags(flags);
            return this;
        }

        @Override
        public boolean submit(ActorJoinHandler callback) {
            return builder.submit(callback);
        }
    }

    private final class ActorJoinEntrySpotBuilder
        implements ActorJoinEntrySpotOperation {
        private final ActorRef actor;
        private final RoutingId destNodeRid;
        private Duration timeout = Duration.ofMillis(5_000L);
        private boolean submitted;

        ActorJoinEntrySpotBuilder(ActorRef actor, RoutingId destNodeRid) {
            this.actor = actor;
            this.destNodeRid = destNodeRid;
        }

        @Override
        public ActorJoinEntrySpotOperation timeout(Duration value) {
            ensureNotSubmitted();
            timeout = Objects.requireNonNull(value, "timeout");
            return this;
        }

        @Override
        public CompletableFuture<ActorJoinEntrySpotCompletion> submit() {
            CompletableFuture<ActorJoinEntrySpotCompletion> future =
                new CompletableFuture<>();
            submit(result -> {
                if (result.result() == RequestResult.OK) {
                    future.complete(new ActorJoinEntrySpotCompletion(result));
                } else {
                    future.completeExceptionally(
                        new ZlinkRequestException(result.result()));
                }
            });
            return future;
        }

        @Override
        public boolean submit(ActorJoinEntrySpotHandler callback) {
            Objects.requireNonNull(callback, "callback");
            markSubmitted();
            ActorRequestCallbacks.JoinEntrySpotPendingToken token =
                ActorRequestCallbacks.registerJoinEntrySpot(callback);
            try (Arena arena = Arena.ofConfined()) {
                int rc = Native.spotNodeActorJoinEntrySpot(node.handle(),
                    ActorInterop.actorRefToNative(arena, actor),
                    ActorInterop.nativeRoutingId(arena, destNodeRid),
                    ActorRequestCallbacks.ACTOR_JOIN_ENTRY_SPOT_CALLBACK,
                    MemorySegment.ofAddress(token.id()),
                    NativeSpotNode.timeoutMillis(timeout));
                if (rc != 0) {
                    ActorRequestCallbacks.remove(token.id());
                    throw new ZlinkSubmitException(SubmitResult.fromValue(rc));
                }
            }
            return true;
        }

        private void markSubmitted() {
            ensureNotSubmitted();
            submitted = true;
        }

        private void ensureNotSubmitted() {
            if (submitted)
                throw new IllegalStateException("operation already submitted");
        }
    }
}
