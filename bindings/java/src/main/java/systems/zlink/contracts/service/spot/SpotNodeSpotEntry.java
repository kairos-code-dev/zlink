/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.RoutingId;
import java.util.Objects;

public record SpotNodeSpotEntry(RoutingId spotRid,
                                SpotKind spotKind,
                                boolean dispatchHandlerAttached,
                                int joinedActorCount,
                                int pendingActorJoinCount,
                                boolean routeSynced,
                                long lastChangedMs) {
    public SpotNodeSpotEntry {
        Objects.requireNonNull(spotRid, "spotRid");
        Objects.requireNonNull(spotKind, "spotKind");
    }
}
