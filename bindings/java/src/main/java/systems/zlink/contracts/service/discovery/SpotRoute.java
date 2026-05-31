/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.discovery;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.service.spot.SpotKind;
import java.util.Objects;

/**
 * The resolved route to a spot: the spot, its owning node, and its kind.
 * @param spotRid the spot's routing id
 * @param ownerNodeRid the owning node's routing id
 * @param spotKind the kind of the spot
 */
public record SpotRoute(RoutingId spotRid, RoutingId ownerNodeRid,
                        SpotKind spotKind) {
    public SpotRoute {
        Objects.requireNonNull(spotRid, "spotRid");
        Objects.requireNonNull(ownerNodeRid, "ownerNodeRid");
        Objects.requireNonNull(spotKind, "spotKind");
    }
}
