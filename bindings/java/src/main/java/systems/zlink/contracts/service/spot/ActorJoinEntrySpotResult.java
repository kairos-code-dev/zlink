/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.RequestResult;
import systems.zlink.contracts.RoutingId;

public record ActorJoinEntrySpotResult(RequestResult result,
                                       ActorRef actor,
                                       RoutingId targetNodeRid,
                                       long joinEpoch,
                                       int flags) {
}
