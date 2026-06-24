/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.spot;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.time.Duration;
import java.util.List;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import systems.zlink.contracts.errors.ConfigResult;
import systems.zlink.contracts.errors.ZlinkConfigException;
import systems.zlink.contracts.errors.ZlinkRequestException;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.spot.Actor;
import systems.zlink.contracts.service.spot.ActorDestroyOperation;
import systems.zlink.contracts.service.spot.ActorJoinEntrySpotOperation;
import systems.zlink.contracts.service.spot.ActorJoinOperation;
import systems.zlink.contracts.service.spot.ActorLeaveOperation;
import systems.zlink.contracts.service.spot.ActorLookupHandler;
import systems.zlink.contracts.service.spot.ActorLookupOperation;
import systems.zlink.contracts.service.spot.ActorLookupResult;
import systems.zlink.contracts.service.spot.ActorRef;
import systems.zlink.contracts.service.spot.ReplyHandler;
import systems.zlink.contracts.service.spot.SendOperation;
import systems.zlink.contracts.service.spot.SendSubmitOperation;
import systems.zlink.contracts.sockets.RequestResult;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.sockets.SubmitResult;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.runtime.nativeapi.ActorInterop;
import systems.zlink.runtime.nativeapi.ActorRequestCallbacks;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.nativeapi.MessagePartsBuffer;
import systems.zlink.runtime.nativeapi.Native;
import systems.zlink.runtime.nativeapi.NativeHelpers;
import systems.zlink.runtime.nativeapi.NativeLayouts;
import systems.zlink.runtime.nativeapi.NativeMessage;

final class SpotNodeActorOperations {
    private final NativeSpotNode node;
    private final SpotActorJoinPlane joinPlane;

    SpotNodeActorOperations(NativeSpotNode node) {
        this.node = node;
        this.joinPlane = new SpotActorJoinPlane(node);
    }

    Actor createActor(String actorId) {
        Objects.requireNonNull(actorId, "actorId");
        node.ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment out = arena.allocate(NativeLayouts.ACTOR_REF_LAYOUT);
            int rc = Native.spotNodeActorNew(node.handle(),
                NativeHelpers.toCString(arena, actorId), out);
            if (rc != 0) {
                throw new ZlinkConfigException(ConfigResult.fromValue(rc));
            }
            return new NativeActor(node, ActorInterop.actorRefFromNative(out));
        }
    }

    Actor createActor(String actorId, Message request) {
        Objects.requireNonNull(request, "request");
        return createActor(actorId, List.of(request));
    }

    Actor createActor(String actorId, List<Message> requestParts) {
        Objects.requireNonNull(actorId, "actorId");
        Objects.requireNonNull(requestParts, "requestParts");
        if (requestParts.isEmpty()) {
            throw new IllegalArgumentException("requestParts required");
        }
        for (Message part : requestParts) {
            Objects.requireNonNull(part, "requestParts part");
        }
        node.ensureOpen();

        try (Arena arena = Arena.ofConfined()) {
            MemorySegment out = arena.allocate(NativeLayouts.ACTOR_REF_LAYOUT);
            MemorySegment nativeParts = arena.allocate(
                NativeLayouts.MESSAGE_LAYOUT,
                requestParts.size());
            int moved = moveRequestPartsToNative(requestParts, nativeParts);
            boolean submitted = false;
            try {
                int rc = Native.spotNodeActorNewWithRequest(
                    node.handle(),
                    NativeHelpers.toCString(arena, actorId),
                    nativeParts,
                    requestParts.size(),
                    out);
                submitted = rc == 0;
                if (rc != 0) {
                    restoreRequestPartsFromNative(requestParts, nativeParts, moved);
                    throw new ZlinkConfigException(ConfigResult.fromValue(rc));
                }
                return new NativeActor(node,
                    ActorInterop.actorRefFromNative(out));
            } finally {
                if (!submitted) {
                    closeNativeRequestParts(nativeParts, moved);
                }
            }
        }
    }

    ActorRef lookupActor(String actorId) {
        Objects.requireNonNull(actorId, "actorId");
        node.ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment out = arena.allocate(NativeLayouts.ACTOR_REF_LAYOUT);
            int rc = Native.spotNodeActorLookup(node.handle(),
                NativeHelpers.toCString(arena, actorId), out);
            if (rc != 0) {
                throw new ZlinkConfigException(ConfigResult.fromValue(rc));
            }
            return ActorInterop.actorRefFromNative(out);
        }
    }

    private static int moveRequestPartsToNative(List<Message> parts,
                                                MemorySegment nativeParts) {
        long stride = NativeLayouts.MESSAGE_LAYOUT.byteSize();
        int moved = 0;
        try {
            for (int i = 0; i < parts.size(); i++) {
                InternalAccess.messageMoveTo(parts.get(i),
                    nativeParts.asSlice((long) i * stride, stride));
                moved++;
            }
            return moved;
        } catch (RuntimeException | Error error) {
            restoreRequestPartsFromNative(parts, nativeParts, moved);
            closeNativeRequestParts(nativeParts, moved);
            throw error;
        }
    }

    private static void restoreRequestPartsFromNative(List<Message> parts,
                                                      MemorySegment nativeParts,
                                                      int moved) {
        long stride = NativeLayouts.MESSAGE_LAYOUT.byteSize();
        for (int i = moved - 1; i >= 0; i--) {
            InternalAccess.messageRestoreFromNative(
                parts.get(i),
                nativeParts.asSlice((long) i * stride, stride),
                false,
                null);
        }
    }

    private static void closeNativeRequestParts(MemorySegment nativeParts,
                                                int moved) {
        long stride = NativeLayouts.MESSAGE_LAYOUT.byteSize();
        for (int i = 0; i < moved; i++) {
            try {
                NativeMessage.messageClose(
                    nativeParts.asSlice((long) i * stride, stride));
            } catch (RuntimeException ignored) {
            }
        }
    }

    ActorLookupOperation remoteActorGetRef(RoutingId targetNodeRid,
                                           String actorId) {
        Objects.requireNonNull(targetNodeRid, "targetNodeRid");
        Objects.requireNonNull(actorId, "actorId");
        node.ensureOpen();
        return new ActorLookupBuilder(targetNodeRid, actorId);
    }

    ActorDestroyOperation destroyActor(ActorRef actor) {
        Objects.requireNonNull(actor, "actor");
        node.ensureOpen();
        return new ActorDestroyBuilder(actor);
    }

    ActorJoinOperation joinActor(ActorRef actor, RoutingId destNodeRid,
                                 RoutingId destSpotRid) {
        return joinPlane.joinActor(actor, destNodeRid, destSpotRid);
    }

    ActorJoinEntrySpotOperation joinActorEntrySpot(ActorRef actor,
                                                   RoutingId destNodeRid,
                                                   Message request) {
        return joinPlane.joinActorEntrySpot(actor, destNodeRid, request);
    }

    ActorLeaveOperation leaveActor(ActorRef actor, RoutingId currentSpotRid) {
        return joinPlane.leaveActor(actor, currentSpotRid);
    }

    SendOperation sendActorBoundSession(ActorRef actor) {
        Objects.requireNonNull(actor, "actor");
        node.ensureOpen();
        return new SendBoundSessionBuilder(actor);
    }

    SendOperation forwardActorBoundSession(
        ActorRef actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid) {
        Objects.requireNonNull(actor, "actor");
        Objects.requireNonNull(sourceNodeRid, "sourceNodeRid");
        Objects.requireNonNull(sourceSessionRid, "sourceSessionRid");
        node.ensureOpen();
        return new ForwardBoundSessionBuilder(actor, sourceNodeRid, sourceSessionRid);
    }

    void bindRemoteActorBoundSession(
        ActorRef actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid) {
        Objects.requireNonNull(actor, "actor");
        Objects.requireNonNull(sourceNodeRid, "sourceNodeRid");
        Objects.requireNonNull(sourceSessionRid, "sourceSessionRid");
        node.ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotNodeActorBindRemoteSession(
                node.handle(),
                ActorInterop.actorRefToNative(arena, actor),
                ActorInterop.nativeRoutingId(arena, sourceNodeRid),
                ActorInterop.nativeRoutingId(arena, sourceSessionRid));
            if (rc != 0) {
                throw new ZlinkConfigException(ConfigResult.fromValue(rc));
            }
        }
    }

    void closeActorBoundSession(ActorRef actor, Duration timeout) {
        Objects.requireNonNull(actor, "actor");
        node.ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotNodeActorCloseBoundSession(node.handle(),
                ActorInterop.actorRefToNative(arena, actor),
                NativeSpotNode.timeoutMillis(timeout));
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError(
                    "zlink_spot_node_actor_close_bound_session");
            }
        }
    }

    private final class ForwardBoundSessionBuilder
        implements SendOperation, SendSubmitOperation {
        private final ActorRef actor;
        private final RoutingId sourceNodeRid;
        private final RoutingId sourceSessionRid;
        private final MessagePartsBuffer parts = new MessagePartsBuffer();
        private SendFlags flags = SendFlags.NONE;
        private boolean submitted;

        ForwardBoundSessionBuilder(
            ActorRef actor,
            RoutingId sourceNodeRid,
            RoutingId sourceSessionRid) {
            this.actor = actor;
            this.sourceNodeRid = sourceNodeRid;
            this.sourceSessionRid = sourceSessionRid;
        }

        @Override
        public SendSubmitOperation message(Message part) {
            ensureNotSubmitted();
            parts.add(Objects.requireNonNull(part, "part"));
            return this;
        }

        @Override
        public SendSubmitOperation flags(SendFlags value) {
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
                MemorySegment refSegment =
                    ActorInterop.actorRefToNative(arena, actor);
                MemorySegment sourceNodeRidSegment =
                    ActorInterop.nativeRoutingId(arena, sourceNodeRid);
                MemorySegment sourceSessionRidSegment =
                    ActorInterop.nativeRoutingId(arena, sourceSessionRid);
                for (int i = 0; i < parts.size(); i++) {
                    Message part = parts.get(i);
                    MemorySegment nativeMsg = arena.allocate(
                        NativeLayouts.MESSAGE_LAYOUT);
                    InternalAccess.messageCopyTo(part, nativeMsg);
                    int rc = Native.spotNodeActorForwardBoundSessionPart(
                        node.handle(),
                        refSegment,
                        sourceNodeRidSegment,
                        sourceSessionRidSegment,
                        nativeMsg,
                        flags.value(),
                        i + 1 < parts.size() ? 1 : 0);
                    if (rc != 0) {
                        NativeMessage.messageClose(nativeMsg);
                        if (flags == SendFlags.DONT_WAIT
                            && SubmitResult.fromValue(rc)
                                == SubmitResult.BACKPRESSURED) {
                            return false;
                        }
                        throw new ZlinkSubmitException(SubmitResult.fromValue(rc));
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

    private final class ActorLookupBuilder implements ActorLookupOperation {
        private final RoutingId targetNodeRid;
        private final String actorId;
        private Duration timeout = Duration.ofMillis(5_000L);
        private boolean submitted;

        ActorLookupBuilder(RoutingId targetNodeRid, String actorId) {
            this.targetNodeRid = targetNodeRid;
            this.actorId = actorId;
        }

        @Override
        public ActorLookupOperation timeout(Duration value) {
            ensureNotSubmitted();
            timeout = Objects.requireNonNull(value, "timeout");
            return this;
        }

        @Override
        public CompletableFuture<ActorLookupResult> submit() {
            CompletableFuture<ActorLookupResult> future =
                new CompletableFuture<>();
            submit(result -> {
                if (result.result() == RequestResult.OK) {
                    future.complete(result);
                } else {
                    future.completeExceptionally(
                        new ZlinkRequestException(result.result()));
                }
            });
            return future;
        }

        @Override
        public boolean submit(ActorLookupHandler callback) {
            Objects.requireNonNull(callback, "callback");
            markSubmitted();
            ActorRequestCallbacks.LookupPendingToken token =
                ActorRequestCallbacks.registerLookup(callback);
            try (Arena arena = Arena.ofConfined()) {
                int rc = Native.remoteActorGetRef(node.handle(),
                    ActorInterop.nativeRoutingId(arena, targetNodeRid),
                    NativeHelpers.toCString(arena, actorId),
                    ActorRequestCallbacks.ACTOR_LOOKUP_CALLBACK,
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

    private final class ActorDestroyBuilder implements ActorDestroyOperation {
        private final ActorRef actor;
        private Duration timeout = Duration.ofMillis(5_000L);
        private boolean submitted;

        ActorDestroyBuilder(ActorRef actor) {
            this.actor = actor;
        }

        @Override
        public ActorDestroyOperation timeout(Duration value) {
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
                    future.completeExceptionally(new ZlinkRequestException(result));
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
                int rc = Native.spotNodeActorDestroy(node.handle(),
                    ActorInterop.actorRefToNative(arena, actor),
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

    private final class SendBoundSessionBuilder
        implements SendOperation, SendSubmitOperation {
        private final ActorRef actor;
        private final MessagePartsBuffer parts = new MessagePartsBuffer();
        private SendFlags flags = SendFlags.NONE;
        private boolean submitted;

        SendBoundSessionBuilder(ActorRef actor) {
            this.actor = actor;
        }

        @Override
        public SendSubmitOperation message(Message part) {
            ensureNotSubmitted();
            parts.add(Objects.requireNonNull(part, "part"));
            return this;
        }

        @Override
        public SendSubmitOperation flags(SendFlags value) {
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
                MemorySegment refSegment =
                    ActorInterop.actorRefToNative(arena, actor);
                for (int i = 0; i < parts.size(); i++) {
                    Message part = parts.get(i);
                    MemorySegment nativeMsg = arena.allocate(
                        NativeLayouts.MESSAGE_LAYOUT);
                    InternalAccess.messageCopyTo(part, nativeMsg);
                    int rc = Native.spotNodeActorSendBoundSessionMessage(
                        node.handle(), refSegment, nativeMsg, flags.value());
                    if (rc != 0) {
                        NativeMessage.messageClose(nativeMsg);
                        if (flags == SendFlags.DONT_WAIT
                            && SubmitResult.fromValue(rc)
                                == SubmitResult.BACKPRESSURED) {
                            return false;
                        }
                        throw new ZlinkSubmitException(SubmitResult.fromValue(rc));
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
