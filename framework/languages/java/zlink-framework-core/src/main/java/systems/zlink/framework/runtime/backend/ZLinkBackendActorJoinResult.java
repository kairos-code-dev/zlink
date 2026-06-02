package systems.zlink.framework.runtime.backend;

import java.util.List;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;

public record ZLinkBackendActorJoinResult(
    ZLinkBackendRequestResult result,
    int joinResultCode,
    ZLinkBackendActorRef actor,
    RoutingId joinedSpotRid,
    long joinEpoch,
    int flags,
    List<Message> replyParts) {
}
