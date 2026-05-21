/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.RoutingId;
import java.util.Objects;

public record SpotNodeActorEntry(ActorRef actor,
                                 RoutingId currentSpotRid,
                                 SpotKind currentSpotKind,
                                 boolean routeSynced,
                                 int pendingMessageCount,
                                 long lastChangedMs) {
    public SpotNodeActorEntry {
        Objects.requireNonNull(actor, "actor");
        Objects.requireNonNull(currentSpotRid, "currentSpotRid");
        Objects.requireNonNull(currentSpotKind, "currentSpotKind");
    }
}
