/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;

import systems.zlink.contracts.Message;

/**
 * Payload-mandatory Actor join operation builder. Returned by
 * {@link SpotNode#joinActor(ActorRef, systems.zlink.contracts.RoutingId,
 * systems.zlink.contracts.RoutingId)} and {@link Actor#join(Spot)}.
 */
public interface ActorJoinOp {
    ActorJoinSubmitOp message(Message part);
}
