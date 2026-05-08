/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.service.spot;

import systems.zlink.RoutingId;
import java.util.Objects;
import java.util.Optional;

public record ActorRoute(ActorRef actor,
                         boolean joined,
                         Optional<RoutingId> joinedSpotRid) {
    public ActorRoute {
        Objects.requireNonNull(actor, "actor");
        joinedSpotRid = joinedSpotRid == null ? Optional.empty() : joinedSpotRid;
    }
}
