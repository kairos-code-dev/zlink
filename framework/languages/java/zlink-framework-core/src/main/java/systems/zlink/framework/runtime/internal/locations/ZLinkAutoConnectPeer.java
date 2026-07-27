package systems.zlink.framework.runtime.internal.locations;

import java.time.Instant;
import java.util.List;
import java.util.Map;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.locations.ZLinkLocationRole;

/** Internal descriptor projection consumed by the automatic-connect reconciler. */
public record ZLinkAutoConnectPeer(
    ZLinkAutoConnectType autoConnectType,
    String meshName,
    RoutingId nodeRid,
    ZLinkLocationRole role,
    String endpoint,
    long weight,
    boolean draining,
    long generation,
    Map<String, String> metadata,
    List<String> capabilities,
    String ownerId,
    long ownerLeaseGeneration,
    Instant updatedAt) {
}
