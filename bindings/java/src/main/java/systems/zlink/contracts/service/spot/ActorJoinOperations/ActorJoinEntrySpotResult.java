/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.sockets.RequestResult;
import systems.zlink.contracts.core.RoutingId;
/**
 * The outcome of an actor join to an entry spot.
 * @param result the request result
 * @param actor the actor reference
 * @param targetNodeRid the target node's routing id
 * @param joinEpoch the join epoch
 * @param flags operation flags
 */
public record ActorJoinEntrySpotResult(RequestResult result,
                                       ActorRef actor,
                                       RoutingId targetNodeRid,
                                       long joinEpoch,
                                       int flags) {
}
