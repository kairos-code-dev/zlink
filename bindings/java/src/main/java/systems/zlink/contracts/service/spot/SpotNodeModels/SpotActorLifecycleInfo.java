/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.core.RoutingId;
import java.util.Optional;

/**
 * Details of an actor lifecycle change, before and after.
 * @param previousActor the actor before the transition
 * @param currentActor the actor after the transition
 * @param previousSpotRid the previous spot's routing id, if any
 * @param currentSpotRid the current spot's routing id, if any
 * @param joinEpoch the join epoch associated with the transition
 * @param flags transition flags
 */
public record SpotActorLifecycleInfo(ActorRef previousActor,
                                     ActorRef currentActor,
                                     Optional<RoutingId> previousSpotRid,
                                     Optional<RoutingId> currentSpotRid,
                                     long joinEpoch,
                                     int flags) {
}
