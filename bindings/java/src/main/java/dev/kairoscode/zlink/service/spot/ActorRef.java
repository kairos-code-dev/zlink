/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

import dev.kairoscode.zlink.RoutingId;
import java.util.Objects;

public record ActorRef(RoutingId nodeRid, String actorId, long generation) {
    public ActorRef {
        Objects.requireNonNull(nodeRid, "nodeRid");
        Objects.requireNonNull(actorId, "actorId");
    }

    public boolean unchecked() {
        return generation == 0L;
    }
}
