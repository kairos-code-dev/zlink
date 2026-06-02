package systems.zlink.framework.runtime.backend;

import java.util.List;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;

public record ZLinkBackendActorJoinRequest(
    RoutingId actorNodeRid,
    String actorId,
    List<Message> parts) {
}
