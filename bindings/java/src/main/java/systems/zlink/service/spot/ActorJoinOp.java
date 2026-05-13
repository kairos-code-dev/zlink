/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.service.spot;

import systems.zlink.Message;

/**
 * Payload-mandatory Actor join operation builder. Returned by
 * {@link SpotNode#joinActor(ActorRef, systems.zlink.RoutingId,
 * systems.zlink.RoutingId)} and {@link Actor#join(Spot)}.
 */
public interface ActorJoinOp {
    ActorJoinSubmitOp message(Message part);
}
