package systems.zlink.framework.runtime.internal.backend;

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
    boolean hasMore,
    byte[] acceptedJournalRecord) implements AutoCloseable {
    public ZLinkBackendActorReceived {
        requestSeq = requestSeq == null ? Optional.empty() : requestSeq;
        acceptedJournalRecord = acceptedJournalRecord == null
            ? new byte[0]
            : acceptedJournalRecord.clone();
    }

    public ZLinkBackendActorReceived(
        ZLinkBackendActorRef actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        Optional<Long> requestSeq,
        long requestId,
        int flags,
        Message message,
        boolean hasMore) {
        this(
            actor,
            sourceNodeRid,
            sourceSessionRid,
            requestSeq,
            requestId,
            flags,
            message,
            hasMore,
            new byte[0]);
    }

    @Override
    public byte[] acceptedJournalRecord() {
        return acceptedJournalRecord.clone();
    }

    @Override
    public void close() {
        message.close();
    }
}
