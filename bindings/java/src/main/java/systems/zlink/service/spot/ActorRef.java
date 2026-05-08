/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.service.spot;

import systems.zlink.RoutingId;
import java.util.Objects;

public record ActorRef(RoutingId nodeRid, String actorId, long generation) {
    public ActorRef {
        Objects.requireNonNull(nodeRid, "nodeRid");
        Objects.requireNonNull(actorId, "actorId");
    }

    boolean unchecked() {
        return generation == 0L;
    }
}
