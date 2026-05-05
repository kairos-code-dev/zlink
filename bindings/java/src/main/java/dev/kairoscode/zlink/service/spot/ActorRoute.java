/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

import dev.kairoscode.zlink.RoutingId;
import java.util.Objects;

public record ActorRoute(ActorRef actor, boolean joined, RoutingId joinedSpotRid) {
    public ActorRoute {
        Objects.requireNonNull(actor, "actor");
    }
}
