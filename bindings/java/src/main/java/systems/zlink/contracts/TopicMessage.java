/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;


import java.util.List;
import java.util.Optional;

/** Topic-aware recv result used by raw SUB and unified Spot subscribe paths. */
public final class TopicMessage implements AutoCloseable {
    private RoutingId routingId;
    private String topic;
    private List<Message> parts;
    private boolean closed;

    public TopicMessage() {
        this(null, "", null);
    }

    TopicMessage(RoutingId routingId, String topicId, Message[] parts) {
        this.routingId = routingId;
        this.topic = topicId == null ? "" : topicId;
        this.parts = parts == null ? List.of() : List.of(parts);
    }

    public void adoptFrom(TopicMessage source) {
        if (source == this)
            return;
        close();
        this.routingId = source.routingId;
        this.topic = source.topic;
        this.parts = source.parts;
        this.closed = false;
        source.routingId = null;
        source.topic = "";
        source.parts = List.of();
        source.closed = true;
    }

    public Optional<RoutingId> routingId() {
        return Optional.ofNullable(routingId);
    }

    public String topic() {
        return topic;
    }

    /** Returns the payload parts as an immutable view. */
    public List<Message> parts() {
        return parts;
    }

    /** Returns whether the payload contains exactly one part. */
    public boolean isSinglePart() {
        return parts.size() == 1;
    }

    /** Returns the first payload part. */
    public Message firstPart() {
        if (parts.isEmpty())
            throw new RecvException(RecvResult.NO_DATA);
        return parts.get(0);
    }

    /** Returns the single payload part, or throws when the payload is multipart. */
    public Message singlePartOrThrow() {
        if (!isSinglePart()) {
            throw new RecvException(RecvResult.NOT_SUPPORTED);
        }
        return parts.get(0);
    }

    @Override
    public void close() {
        if (closed)
            return;
        closed = true;
        Message.closeAll(parts);
    }
}
