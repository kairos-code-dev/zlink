package systems.zlink.framework.runtime.backend;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import java.util.Optional;

public record ZLinkBackendActorReceived(
    ZLinkBackendActorRef actor,
    RoutingId sourceNodeRid,
    RoutingId sourceSessionRid,
    Optional<Long> requestSeq,
    long requestId,
    int flags,
    Message message,
    boolean hasMore) implements AutoCloseable {
    public ZLinkBackendActorReceived {
        requestSeq = requestSeq == null ? Optional.empty() : requestSeq;
    }

    @Override
    public void close() {
        message.close();
    }
}
