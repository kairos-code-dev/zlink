/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.core.RoutingId;
import java.util.Objects;

/**
 * Metadata about a message received for an actor.
 * @param actor the actor that received the message
 * @param sourceNodeRid the routing id of the sending node
 * @param sourceSessionRid the routing id of the source session, if any
 * @param flags message flags
 */
public record ActorRecvInfo(ActorRef actor,
                            RoutingId sourceNodeRid,
                            RoutingId sourceSessionRid,
                            int flags) {
    public ActorRecvInfo {
        Objects.requireNonNull(actor, "actor");
    }
}
