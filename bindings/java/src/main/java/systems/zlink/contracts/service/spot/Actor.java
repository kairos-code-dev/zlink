/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.sockets.RecvFlags;
import java.time.Duration;

/** Local Actor resource owned by a {@link SpotNode}. */
public interface Actor extends AutoCloseable {

    public abstract ActorRef ref();

    /**
     * Async user-Spot join operation builder. Completion delivers an
     * {@link ActorJoinResult} plus reply parts. {@code spot} must be a user
     * Spot.
     */
    public abstract ActorJoinOp join(Spot spot);

    /** Async leave operation builder for the supplied Spot. */
    public abstract ActorLeaveOp leave(Spot spot);

    public abstract ActorPart recvPart(RecvFlags flags);

    public abstract ActorPart recvPart();

    /** Actor-to-session relay operation builder. */
    public abstract SendOp sendBoundSession();

    public abstract void closeBoundSession(Duration timeout);

    public abstract void closeBoundSession();

    public abstract void close(Duration timeout);

    @Override
    public abstract void close();
}
