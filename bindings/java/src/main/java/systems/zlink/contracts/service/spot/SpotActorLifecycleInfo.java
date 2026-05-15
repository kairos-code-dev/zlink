/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.RoutingId;
import java.util.Optional;

public record SpotActorLifecycleInfo(ActorRef previousActor,
                                     ActorRef currentActor,
                                     Optional<RoutingId> previousSpotRid,
                                     Optional<RoutingId> currentSpotRid,
                                     long joinEpoch,
                                     int flags) {
}
