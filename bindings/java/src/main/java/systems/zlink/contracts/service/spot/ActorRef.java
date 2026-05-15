/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;

import systems.zlink.contracts.RoutingId;
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
