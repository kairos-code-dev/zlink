/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.RequestResult;
import systems.zlink.contracts.RoutingId;

public record ActorJoinResult(RequestResult result,
                              ActorRef actor,
                              RoutingId joinedSpotRid,
                              long joinEpoch,
                              int flags) {
}
