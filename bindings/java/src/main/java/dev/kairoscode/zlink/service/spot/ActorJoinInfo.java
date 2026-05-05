/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

import dev.kairoscode.zlink.RoutingId;
import java.lang.foreign.MemorySegment;
import java.util.Objects;

public record ActorJoinInfo(ActorRef actor,
                            RoutingId sourceNodeRid,
                            MemorySegment request,
                            int flags) {
    public ActorJoinInfo {
        Objects.requireNonNull(actor, "actor");
        Objects.requireNonNull(request, "request");
    }
}
