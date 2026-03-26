/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Objects;

/**
 * Aggregates one recv result, including an optional routing id and the owned
 * message parts.
 *
 * <p>The returned {@link #parts()} view is immutable and does not copy the
 * underlying part array. Closing the aggregate closes every owned part.
 */
public final class Received implements AutoCloseable {
    private final RoutingId routingId;
    private final Message[] parts;
    private final List<Message> partsView;

    public Received(RoutingId routingId, Message[] parts) {
        this.routingId = routingId;
        this.parts = Arrays.copyOf(Objects.requireNonNull(parts, "parts"),
            parts.length);
        this.partsView = Collections.unmodifiableList(Arrays.asList(this.parts));
    }

    /** Returns the routing id when the transport delivered one. */
    public RoutingId routingId() {
        return routingId;
    }

    /** Returns whether the recv result includes a routing id. */
    public boolean hasRoutingId() {
        return routingId != null && !routingId.empty();
    }

    /** Returns the owned parts as an immutable view without copying. */
    public List<Message> parts() {
        return partsView;
    }

    /** Returns whether exactly one payload part was received. */
    public boolean isSinglePart() {
        return parts.length == 1;
    }

    /** Returns the first payload part. */
    public Message firstPart() {
        if (parts.length == 0)
            throw new IllegalStateException("received message has no parts");
        return parts[0];
    }

    /** Returns the only payload part or throws when the result is multipart. */
    public Message singlePartOrThrow() {
        if (!isSinglePart())
            throw new IllegalStateException("received message is multipart");
        return parts[0];
    }

    @Override
    public void close() {
        Message.closeAll(parts);
    }
}
