/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.sockets.RequestResult;
import systems.zlink.contracts.core.RoutingId;
/**
 * The outcome of an actor join.
 * @param result the request result
 * @param joinResultCode the application-level join result code
 * @param actor the actor reference
 * @param joinedSpotRid the routing id of the spot the actor joined
 * @param joinEpoch the join epoch
 * @param flags operation flags
 */
public record ActorJoinResult(RequestResult result,
                              int joinResultCode,
                              ActorRef actor,
                              RoutingId joinedSpotRid,
                              long joinEpoch,
                              int flags) {
}
