/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.service.spot;

import systems.zlink.RoutingId;
import java.util.Optional;

public record SpotActorLifecycleInfo(ActorRef previousActor,
                                     ActorRef currentActor,
                                     Optional<RoutingId> previousSpotRid,
                                     Optional<RoutingId> currentSpotRid,
                                     long joinEpoch,
                                     int flags) {
}
