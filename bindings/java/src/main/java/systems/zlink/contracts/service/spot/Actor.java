/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.sockets.RecvFlags;
import java.time.Duration;

/** Local Actor resource owned by a {@link SpotNode}. */
public interface Actor extends AutoCloseable {

    ActorRef ref();

    /**
     * Async user-Spot join operation builder. Completion delivers an
     * {@link ActorJoinResult} plus reply parts. {@code spot} must be a user
     * Spot.
     */
    ActorJoinOperation join(Spot spot);

    /** Async leave operation builder for the supplied Spot. */
    ActorLeaveOperation leave(Spot spot);

    ActorReceived recv(RecvFlags flags);

    ActorReceived recv();

    /** Actor-to-session relay operation builder. */
    SendOperation sendBoundSession();

    void closeBoundSession(Duration timeout);

    void closeBoundSession();

    void close(Duration timeout);

    @Override
    void close();
}
