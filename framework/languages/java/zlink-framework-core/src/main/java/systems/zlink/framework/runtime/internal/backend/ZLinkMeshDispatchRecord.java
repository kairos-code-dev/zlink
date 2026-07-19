package systems.zlink.framework.runtime.internal.backend;

import java.util.List;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.spot.ReadyRecord;
import systems.zlink.contracts.service.spot.ReceiveRecord;

/**
 * One retained RouteMesh dispatch message.
 *
 * <p>The receiver owns the retained message parts and must close this record
 * after dispatch completes.
 */
public record ZLinkMeshDispatchRecord(
    ReadyRecord owner,
    ReceiveRecord receive,
    List<Message> parts) implements AutoCloseable {
    public ZLinkMeshDispatchRecord {
        parts = List.copyOf(parts);
    }

    @Override
    public void close() {
        parts.forEach(Message::close);
    }
}
