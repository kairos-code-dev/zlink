/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.sockets.RequestResult;
import systems.zlink.contracts.core.RoutingId;
public record ActorJoinResult(RequestResult result,
                              int joinResultCode,
                              ActorRef actor,
                              RoutingId joinedSpotRid,
                              long joinEpoch,
                              int flags) {
}
