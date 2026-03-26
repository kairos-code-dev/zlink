/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import java.util.List;

/** Topic-aware recv result used by raw SUB and unified Spot subscribe paths. */
public final class TopicMessage implements AutoCloseable {
    private final RoutingId routingId;
    private final String topicId;
    private final List<Message> parts;
    private boolean closed;

    TopicMessage(RoutingId routingId, String topicId, Message[] parts) {
        this.routingId = routingId;
        this.topicId = topicId == null ? "" : topicId;
        this.parts = parts == null ? List.of() : List.of(parts);
    }

    /** Returns whether the topic delivery includes a source routing id. */
    public boolean hasRoutingId() {
        return routingId != null;
    }

    /** Returns the source routing id when present, otherwise {@code null}. */
    public RoutingId routingId() {
        return routingId;
    }

    /** Returns the topic id associated with this delivery. */
    public String topicId() {
        return topicId;
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
            throw new IllegalStateException("topic message has no parts");
        return parts.get(0);
    }

    /** Returns the single payload part, or throws when the payload is multipart. */
    public Message singlePartOrThrow() {
        if (!isSinglePart()) {
            throw new IllegalStateException(
                "topic message is not single-part: " + parts.size());
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
