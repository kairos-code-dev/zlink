/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.spot;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.time.Duration;
import java.util.List;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.spot.ActorLeaveOperation;
import systems.zlink.contracts.service.spot.ReplyHandler;
import systems.zlink.contracts.sockets.SubmitResult;
import systems.zlink.runtime.nativeapi.ActorRequestCallbacks;
import systems.zlink.runtime.nativeapi.Native;

abstract class ActorLeaveOperationBase extends SingleSubmitOperation
    implements ActorLeaveOperation {
    private Duration timeout = Duration.ofMillis(5_000L);

    @Override
    public final ActorLeaveOperation timeout(Duration value) {
        ensureNotSubmitted();
        timeout = Objects.requireNonNull(value, "timeout");
        return this;
    }

    @Override
    public final CompletableFuture<List<Message>> submit() {
        return ActorRequestFutureAdapters.reply(this::submit);
    }

    @Override
    public final boolean submit(ReplyHandler callback) {
        Objects.requireNonNull(callback, "callback");
        markSubmitted();
        ActorRequestCallbacks.PendingToken token =
            ActorRequestCallbacks.register(callback::onReply);
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotNodeActorLeaveSpot(nodeHandle(),
                actorRef(arena),
                spotRid(arena),
                ActorRequestCallbacks.REPLY_CALLBACK,
                MemorySegment.ofAddress(token.id()),
                timeoutMillis(timeout));
            if (rc != 0) {
                ActorRequestCallbacks.remove(token.id());
                throw new ZlinkSubmitException(SubmitResult.fromValue(rc));
            }
        }
        return true;
    }

    protected abstract MemorySegment nodeHandle();

    protected abstract MemorySegment actorRef(Arena arena);

    protected abstract MemorySegment spotRid(Arena arena);

    protected abstract int timeoutMillis(Duration timeout);
}
