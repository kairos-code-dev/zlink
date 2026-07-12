package systems.zlink.framework.locations;

import java.time.Instant;
import java.util.List;
import java.util.Map;
import systems.zlink.contracts.core.RoutingId;

public record ZLinkPeerLocation(
    ZLinkLocationAutoConnectType autoConnectType,
    String meshName,
    RoutingId nodeRid,
    ZLinkLocationRole role,
    String endpoint,
    long weight,
    boolean draining,
    long value,
    Map<String, String> metadata,
    List<String> capabilities,
    String ownerId,
    long generation,
    Instant updatedAt) {
}
