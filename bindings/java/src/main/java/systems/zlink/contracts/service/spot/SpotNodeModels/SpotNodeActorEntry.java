/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.core.RoutingId;
import java.util.Objects;

/**
 * One actor hosted on a spot node and its current placement.
 * @param actor the actor reference
 * @param currentSpotRid the routing id of the spot the actor currently occupies
 * @param currentSpotKind the kind of the current spot
 * @param routeSynced whether the actor's route is synced across peers
 * @param pendingMessageCount the number of messages queued for the actor
 * @param lastChangedMs when the actor last changed state, in milliseconds
 */
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
