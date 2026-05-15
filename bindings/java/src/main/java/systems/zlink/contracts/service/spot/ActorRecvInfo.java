/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;

import systems.zlink.contracts.RoutingId;
import java.util.Objects;

public record ActorRecvInfo(ActorRef actor,
                            RoutingId sourceNodeRid,
                            RoutingId sourceSessionRid,
                            int flags) {
    public ActorRecvInfo {
        Objects.requireNonNull(actor, "actor");
    }
}
