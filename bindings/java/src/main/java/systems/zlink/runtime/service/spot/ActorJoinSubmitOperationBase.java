/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.spot;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.time.Duration;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.spot.ActorJoinCallbackSubmitOperation;
import systems.zlink.contracts.service.spot.ActorJoinCompletion;
import systems.zlink.contracts.service.spot.ActorJoinHandler;
import systems.zlink.contracts.service.spot.ActorJoinOperation;
import systems.zlink.contracts.service.spot.ActorJoinSubmitOperation;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.sockets.SubmitResult;
import systems.zlink.runtime.nativeapi.ActorRequestCallbacks;
import systems.zlink.runtime.nativeapi.MessagePartsBuffer;
import systems.zlink.runtime.nativeapi.Native;
import systems.zlink.runtime.nativeapi.RequestProgressPump;

abstract class ActorJoinSubmitOperationBase extends SingleSubmitOperation
    implements ActorJoinOperation, ActorJoinSubmitOperation {
    private Duration timeout = Duration.ofMillis(5_000L);

    @Override
    public final ActorJoinSubmitOperation message(Message part) {
        addPart(part);
        return this;
    }

    @Override
    public final ActorJoinSubmitOperation timeout(Duration value) {
        ensureNotSubmitted();
        timeout = Objects.requireNonNull(value, "timeout");
        return this;
    }

    @Override
    public final ActorJoinCallbackSubmitOperation flags(SendFlags value) {
        setFlags(value);
        return new ActorJoinCallbackStage(this);
    }

    @Override
    public final CompletableFuture<ActorJoinCompletion> submit() {
        return ActorRequestFutureAdapters.actorJoin(this::submit);
    }

    @Override
    public final boolean submit(ActorJoinHandler callback) {
        Objects.requireNonNull(callback, "callback");
        requireParts();
        markSubmitted();
        ActorRequestCallbacks.JoinPendingToken token =
            ActorRequestCallbacks.registerJoin(callback);
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment partsArr = parts().copyToNativeArray(arena);
            int rc = Native.spotNodeActorJoinSpot(nodeHandle(),
                actorRef(arena),
                destNodeRid(arena),
                destSpotRid(arena),
                partsArr, parts().size(),
                ActorRequestCallbacks.ACTOR_JOIN_CALLBACK,
                MemorySegment.ofAddress(token.id()), flags().value(),
                timeoutMillis(timeout));
            if (rc != 0) {
                ActorRequestCallbacks.remove(token.id());
                MessagePartsBuffer.closeNativeArray(partsArr, parts().size());
                throw new ZlinkSubmitException(SubmitResult.fromValue(rc));
            }
            RequestProgressPump.trackSpotRequest(token.future(),
                progressHandle(), progressName());
        }
        return true;
    }

    protected abstract MemorySegment nodeHandle();

    protected abstract MemorySegment actorRef(Arena arena);

    protected abstract MemorySegment destNodeRid(Arena arena);

    protected abstract MemorySegment destSpotRid(Arena arena);

    protected abstract MemorySegment progressHandle();

    protected abstract String progressName();

    protected abstract int timeoutMillis(Duration timeout);

}
