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
    private final long requestSequence;
    private final boolean hasRequestSequence;

    public Received(RoutingId routingId, Message[] parts) {
        this(routingId, parts, false, 0L, false);
    }

    public Received(RoutingId routingId, Message[] parts, long requestSequence,
                    boolean hasRequestSequence) {
        this(routingId, parts, false, requestSequence, hasRequestSequence);
    }

    Received(RoutingId routingId, Message[] parts, boolean trustedParts) {
        this(routingId, parts, trustedParts, 0L, false);
    }

    Received(RoutingId routingId, Message[] parts, boolean trustedParts,
             long requestSequence, boolean hasRequestSequence) {
        this.routingId = routingId;
        Message[] ownedParts = Objects.requireNonNull(parts, "parts");
        this.parts = trustedParts ? ownedParts
            : Arrays.copyOf(ownedParts, ownedParts.length);
        this.partsView = Collections.unmodifiableList(Arrays.asList(this.parts));
        this.requestSequence = requestSequence;
        this.hasRequestSequence = hasRequestSequence;
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

    public long requestSequence() {
        return requestSequence;
    }

    public boolean hasRequestSequence() {
        return hasRequestSequence;
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
