/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.core.RoutingId;
/**
 * Payload-mandatory Actor join operation builder. Returned by
 * {@link SpotNode#joinActor(ActorRef, systems.zlink.contracts.core.RoutingId,
 * systems.zlink.contracts.core.RoutingId)} and {@link Actor#join(Spot)}.
 */
public interface ActorJoinOperation {
    ActorJoinSubmitOperation message(Message part);
}
