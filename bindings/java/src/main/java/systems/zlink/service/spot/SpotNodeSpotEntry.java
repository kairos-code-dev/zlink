/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.service.spot;

import systems.zlink.RoutingId;
import java.util.Objects;

public record SpotNodeSpotEntry(RoutingId spotRid,
                                boolean dispatchHandlerAttached,
                                int joinedActorCount,
                                int pendingActorJoinCount,
                                boolean routeSynced,
                                long lastChangedMs) {
    public SpotNodeSpotEntry {
        Objects.requireNonNull(spotRid, "spotRid");
    }
}
