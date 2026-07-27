package systems.zlink.framework.runtime.internal.backend;

import java.util.List;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.runtime.internal.binding.spot.ReadyRecord;
import systems.zlink.framework.runtime.internal.binding.spot.ReceiveRecord;

/**
 * One retained RouteMesh dispatch message.
 *
 * <p>The receiver owns the retained message parts and must close this record
 * after dispatch completes.
 */
public record ZLinkMeshDispatchRecord(
    ReadyRecord owner,
    ReceiveRecord receive,
    List<Message> parts,
    java.util.function.Consumer<List<Message>> frameworkReply) implements AutoCloseable {
    public ZLinkMeshDispatchRecord {
        parts = List.copyOf(parts);
    }

    public ZLinkMeshDispatchRecord(
        ReadyRecord owner,
        ReceiveRecord receive,
        List<Message> parts) {
        this(owner, receive, parts, null);
    }

    public boolean canReply() {
        return frameworkReply != null;
    }

    public void reply(List<Message> replyParts) {
        if (frameworkReply == null) {
            throw new IllegalStateException("dispatch record has no reply route");
        }
        frameworkReply.accept(List.copyOf(replyParts));
    }

    @Override
    public void close() {
        parts.forEach(Message::close);
    }
}
