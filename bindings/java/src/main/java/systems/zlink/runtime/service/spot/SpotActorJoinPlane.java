/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.spot;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.time.Duration;
import java.util.Objects;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.spot.ActorJoinEntrySpotCompletion;
import systems.zlink.contracts.service.spot.ActorJoinEntrySpotHandler;
import systems.zlink.contracts.service.spot.ActorJoinEntrySpotOperation;
import systems.zlink.contracts.service.spot.ActorJoinOperation;
import systems.zlink.contracts.service.spot.ActorLeaveOperation;
import systems.zlink.contracts.service.spot.ActorRef;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.sockets.SubmitResult;
import systems.zlink.runtime.nativeapi.ActorInterop;
import systems.zlink.runtime.nativeapi.ActorRequestCallbacks;
import systems.zlink.runtime.nativeapi.MessagePartsBuffer;
import systems.zlink.runtime.nativeapi.Native;
import systems.zlink.runtime.nativeapi.RequestProgressPump;

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
                                                   RoutingId destNodeRid,
                                                   Message request) {
        Objects.requireNonNull(actor, "actor");
        Objects.requireNonNull(destNodeRid, "destNodeRid");
        Objects.requireNonNull(request, "request");
        node.ensureOpen();
        return new ActorJoinEntrySpotBuilder(actor, destNodeRid, request);
    }

    ActorLeaveOperation leaveActor(ActorRef actor, RoutingId currentSpotRid) {
        Objects.requireNonNull(actor, "actor");
        Objects.requireNonNull(currentSpotRid, "currentSpotRid");
        node.ensureOpen();
        return new ActorLeaveBuilder(actor, currentSpotRid);
    }

    private final class ActorLeaveBuilder extends ActorLeaveOperationBase {
        private final ActorRef actor;
        private final RoutingId currentSpotRid;

        ActorLeaveBuilder(ActorRef actor, RoutingId currentSpotRid) {
            this.actor = actor;
            this.currentSpotRid = currentSpotRid;
        }

        @Override
        protected MemorySegment nodeHandle() {
            return node.handle();
        }

        @Override
        protected MemorySegment actorRef(Arena arena) {
            return ActorInterop.actorRefToNative(arena, actor);
        }

        @Override
        protected MemorySegment spotRid(Arena arena) {
            return ActorInterop.nativeRoutingId(arena, currentSpotRid);
        }

        @Override
        protected int timeoutMillis(Duration timeout) {
            return NativeSpotNode.timeoutMillis(timeout);
        }
    }

    private final class ActorJoinBuilder extends ActorJoinSubmitOperationBase {
        private final ActorRef actor;
        private final RoutingId destNodeRid;
        private final RoutingId destSpotRid;

        ActorJoinBuilder(ActorRef actor, RoutingId destNodeRid,
                         RoutingId destSpotRid) {
            this.actor = actor;
            this.destNodeRid = destNodeRid;
            this.destSpotRid = destSpotRid;
        }

        @Override
        protected MemorySegment nodeHandle() {
            return node.handle();
        }

        @Override
        protected MemorySegment actorRef(Arena arena) {
            return ActorInterop.actorRefToNative(arena, actor);
        }

        @Override
        protected MemorySegment destNodeRid(Arena arena) {
            return ActorInterop.nativeRoutingId(arena, destNodeRid);
        }

        @Override
        protected MemorySegment destSpotRid(Arena arena) {
            return ActorInterop.nativeRoutingId(arena, destSpotRid);
        }

        @Override
        protected MemorySegment progressHandle() {
            return node.handle();
        }

        @Override
        protected String progressName() {
            return "zlink-spot-node-actor-join-progress";
        }

        @Override
        protected int timeoutMillis(Duration timeout) {
            return NativeSpotNode.timeoutMillis(timeout);
        }
    }

    private final class ActorJoinEntrySpotBuilder extends SingleSubmitOperation
        implements ActorJoinEntrySpotOperation {
        private final ActorRef actor;
        private final RoutingId destNodeRid;
        private Duration timeout = Duration.ofMillis(5_000L);

        ActorJoinEntrySpotBuilder(ActorRef actor, RoutingId destNodeRid,
                                  Message request) {
            this.actor = actor;
            this.destNodeRid = destNodeRid;
            parts().add(request);
        }

        @Override
        public ActorJoinEntrySpotOperation message(Message part) {
            addPart(part);
            return this;
        }

        @Override
        public ActorJoinEntrySpotOperation timeout(Duration value) {
            ensureNotSubmitted();
            timeout = Objects.requireNonNull(value, "timeout");
            return this;
        }

        @Override
        public ActorJoinEntrySpotOperation flags(SendFlags value) {
            setFlags(value);
            return this;
        }

        @Override
        public CompletionStage<ActorJoinEntrySpotCompletion> submit() {
            return ActorRequestFutureAdapters.joinEntrySpot(this::submit);
        }

        @Override
        public boolean submit(ActorJoinEntrySpotHandler callback) {
            Objects.requireNonNull(callback, "callback");
            markSubmitted();
            ActorRequestCallbacks.JoinEntrySpotPendingToken token =
                ActorRequestCallbacks.registerJoinEntrySpot(callback);
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment partsArr = parts().copyToNativeArray(arena);
                int rc = Native.spotNodeActorJoinEntrySpot(node.handle(),
                    ActorInterop.actorRefToNative(arena, actor),
                    ActorInterop.nativeRoutingId(arena, destNodeRid),
                    partsArr, parts().size(),
                    ActorRequestCallbacks.ACTOR_JOIN_ENTRY_SPOT_CALLBACK,
                    MemorySegment.ofAddress(token.id()), flags().value(),
                    NativeSpotNode.timeoutMillis(timeout));
                if (rc != 0) {
                    ActorRequestCallbacks.remove(token.id());
                    MessagePartsBuffer.closeNativeArray(partsArr, parts().size());
                    throw new ZlinkSubmitException(SubmitResult.fromValue(rc));
                }
                RequestProgressPump.trackSpotRequest(token.future(),
                    node.handle(), "zlink-spot-node-actor-entry-join-progress");
            }
            return true;
        }
    }
}
