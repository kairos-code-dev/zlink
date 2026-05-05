/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

import dev.kairoscode.zlink.RoutingId;
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
