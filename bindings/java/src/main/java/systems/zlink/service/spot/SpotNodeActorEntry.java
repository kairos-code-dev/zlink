/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.service.spot;

import systems.zlink.RoutingId;
import java.util.Objects;
import java.util.Optional;

public record SpotNodeActorEntry(ActorRef actor,
                                 boolean joined,
                                 Optional<RoutingId> joinedSpotRid,
                                 boolean routeSynced,
                                 int pendingMessageCount,
                                 long lastChangedMs) {
    public SpotNodeActorEntry {
        Objects.requireNonNull(actor, "actor");
        joinedSpotRid = joinedSpotRid == null ? Optional.empty() : joinedSpotRid;
    }
}
