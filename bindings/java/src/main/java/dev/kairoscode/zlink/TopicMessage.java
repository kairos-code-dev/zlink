/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import java.util.List;
import java.util.Optional;

/** Topic-aware recv result used by raw SUB and unified Spot subscribe paths. */
public final class TopicMessage implements AutoCloseable {
    private final RoutingId routingId;
    private final String topic;
    private final List<Message> parts;
    private boolean closed;

    public TopicMessage(RoutingId routingId, String topicId, Message[] parts) {
        this.routingId = routingId;
        this.topic = topicId == null ? "" : topicId;
        this.parts = parts == null ? List.of() : List.of(parts);
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
