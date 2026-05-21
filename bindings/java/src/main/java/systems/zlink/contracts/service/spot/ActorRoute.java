/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.RoutingId;
import java.util.Objects;

public record ActorRoute(ActorRef actor,
                         RoutingId currentSpotRid,
                         SpotKind currentSpotKind) {
    public ActorRoute {
        Objects.requireNonNull(actor, "actor");
        Objects.requireNonNull(currentSpotRid, "currentSpotRid");
        Objects.requireNonNull(currentSpotKind, "currentSpotKind");
    }
}
