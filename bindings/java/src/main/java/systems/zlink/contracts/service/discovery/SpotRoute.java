/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.discovery;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.service.spot.SpotKind;
import java.util.Objects;

public record SpotRoute(RoutingId spotRid, RoutingId ownerNodeRid,
                        SpotKind spotKind) {
    public SpotRoute {
        Objects.requireNonNull(spotRid, "spotRid");
        Objects.requireNonNull(ownerNodeRid, "ownerNodeRid");
        Objects.requireNonNull(spotKind, "spotKind");
    }
}
