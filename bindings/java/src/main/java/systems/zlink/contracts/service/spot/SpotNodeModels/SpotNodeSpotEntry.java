/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.core.RoutingId;
import java.util.Objects;

/**
 * One spot hosted on a spot node and its actor counts.
 * @param spotRid the spot's routing id
 * @param spotKind the kind of spot
 * @param dispatchHandlerAttached whether a dispatch handler is attached
 * @param joinedActorCount the number of actors currently joined
 * @param pendingActorJoinCount the number of pending actor joins
 * @param routeSynced whether the spot's route is synced across peers
 * @param lastChangedMs when the spot last changed, in milliseconds
 */
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
