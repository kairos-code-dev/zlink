/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

import dev.kairoscode.zlink.RoutingId;
import java.util.Objects;

public record ActorRecvInfo(ActorRef actor,
                            RoutingId sourceNodeRid,
                            RoutingId sourceSessionRid,
                            int flags) {
    public ActorRecvInfo {
        Objects.requireNonNull(actor, "actor");
    }
}
