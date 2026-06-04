package systems.zlink.framework.runtime.backend;

import java.util.List;
import java.util.Optional;
import java.util.function.Consumer;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;

public record ZLinkBackendReceived(
    Optional<RoutingId> routingId,
    Optional<RoutingId> spotRid,
    Optional<Long> requestSeq,
    List<Message> parts,
    Consumer<List<Message>> reply,
    Runnable closeAction) implements AutoCloseable {
    public ZLinkBackendReceived(
        Optional<RoutingId> routingId,
        Optional<RoutingId> spotRid,
        Optional<Long> requestSeq,
        List<Message> parts) {
        this(routingId, spotRid, requestSeq, parts, null, () -> { });
    }

    public ZLinkBackendReceived(
        Optional<RoutingId> routingId,
        Optional<RoutingId> spotRid,
        Optional<Long> requestSeq,
        List<Message> parts,
        Consumer<List<Message>> reply) {
        this(routingId, spotRid, requestSeq, parts, reply, () -> { });
    }

    public void reply(List<Message> replyParts) {
        if (reply == null) {
            throw new IllegalStateException("received message has no reply path");
        }
        reply.accept(replyParts);
    }

    @Override
    public void close() {
        parts.forEach(Message::close);
        closeAction.run();
    }
}
