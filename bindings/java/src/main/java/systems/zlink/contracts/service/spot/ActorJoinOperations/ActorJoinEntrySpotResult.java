/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.sockets.RequestResult;
import systems.zlink.contracts.core.RoutingId;
/**
 * The outcome of an actor join to an entry spot.
 * @param result the request result
 * @param joinResultCode the application-level admission result
 * @param actor the actor reference
 * @param targetNodeRid the target node's routing id
 * @param joinedSpotRid the entry spot routing id that admitted the actor
 * @param joinEpoch the join epoch
 * @param flags operation flags
 */
public record ActorJoinEntrySpotResult(RequestResult result,
                                       int joinResultCode,
                                       ActorRef actor,
                                       RoutingId targetNodeRid,
                                       RoutingId joinedSpotRid,
                                       long joinEpoch,
                                       int flags) {
}
