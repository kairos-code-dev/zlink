/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

import dev.kairoscode.zlink.RoutingId;
import java.util.Objects;

public record SpotNodeActorEntry(ActorRef actor,
                                 boolean joined,
                                 RoutingId joinedSpotRid,
                                 boolean routeSynced,
                                 int pendingMessageCount,
                                 long lastChangedMs) {
    public SpotNodeActorEntry {
        Objects.requireNonNull(actor, "actor");
    }
}
