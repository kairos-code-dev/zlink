/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.core.RoutingId;
import java.util.Objects;

/**
 * The resolved route to an actor: which spot it currently lives on.
 * @param actor the actor reference
 * @param currentSpotRid the routing id of the spot the actor currently occupies
 * @param currentSpotKind the kind of the current spot
 */
public record ActorRoute(ActorRef actor,
                         RoutingId currentSpotRid,
                         SpotKind currentSpotKind) {
    public ActorRoute {
        Objects.requireNonNull(actor, "actor");
        Objects.requireNonNull(currentSpotRid, "currentSpotRid");
        Objects.requireNonNull(currentSpotKind, "currentSpotKind");
    }
}
