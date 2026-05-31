/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.sockets.RecvFlags;
import java.time.Duration;

/** Local Actor resource owned by a {@link SpotNode}. */
public interface Actor extends AutoCloseable {

    /** Returns a reference to this actor. */
    ActorRef ref();

    /**
     * Builds an async join to a user spot. Completion delivers an
     * {@link ActorJoinResult} plus reply parts. {@code spot} must be a user spot.
     * @param spot the user spot to join
     * @return the join operation builder
     */
    ActorJoinOperation join(Spot spot);

    /**
     * Builds an async leave from a user spot.
     * @param spot the spot to leave
     * @return the leave operation builder
     */
    ActorLeaveOperation leave(Spot spot);

    /** Receives the next actor message without blocking. */
    ActorReceived recv(RecvFlags flags);

    /** Receives the next actor message, blocking until one arrives. */
    ActorReceived recv();

    /** Builds a send to the bound session. */
    SendOperation sendBoundSession();

    /** Closes the bound session with the given timeout. */
    void closeBoundSession(Duration timeout);

    /** Closes the bound session. */
    void closeBoundSession();

    /** Closes this actor with the given timeout. */
    void close(Duration timeout);

    @Override
    void close();
}
