/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.core.RoutingId;
import java.util.Objects;

/** Lifecycle and topology facade for the current unified spot node model. */
public interface SpotNode
  extends SpotNodeTopology, SpotNodeActorManagement, SpotNodeTuning,
          SpotNodeDiagnostics, AutoCloseable {
    /**
     * Result of atomically getting or creating a local logical spot.
     * @param spot the spot that was retrieved or created
     * @param created true if the spot was newly created; false if it already existed
     */
    public record SpotGetOrCreateResult(Spot spot, boolean created) {}

    /** Builds an unchecked remote Actor ref for request APIs. */
    static ActorRef remoteActorRef(RoutingId targetNodeRid,
                                   String actorId) {
        Objects.requireNonNull(targetNodeRid, "targetNodeRid");
        Objects.requireNonNull(actorId, "actorId");
        return new ActorRef(targetNodeRid, actorId, 0L);
    }

    @Override
    void close();
}
