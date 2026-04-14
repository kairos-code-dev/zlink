/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.function.BiConsumer;

/**
 * Aggregates one recv result, including an optional routing id and the owned
 * message parts.
 *
 * <p>The returned {@link #parts()} view is immutable and does not copy the
 * underlying part array. Closing the aggregate closes every owned part.
 */
public final class Received implements AutoCloseable {
    private final RoutingId routingId;
    private final RoutingId spotRid;
    private final Message[] parts;
    private final List<Message> partsView;
    private final long requestSequence;
    private final boolean hasRequestSequence;
    private final BiConsumer<List<Message>, SendFlags> replySender;

    Received(RoutingId routingId, Message[] parts) {
        this(routingId, null, parts, false, 0L, false, null);
    }

    Received(RoutingId routingId, Message[] parts, boolean trustedParts) {
        this(routingId, null, parts, trustedParts, 0L, false, null);
    }

    Received(RoutingId routingId, RoutingId spotRid, Message[] parts,
             boolean trustedParts, long requestSequence,
             boolean hasRequestSequence,
             BiConsumer<List<Message>, SendFlags> replySender) {
        this.routingId = routingId;
        this.spotRid = spotRid;
        Message[] ownedParts = Objects.requireNonNull(parts, "parts");
        this.parts = trustedParts ? ownedParts
            : Arrays.copyOf(ownedParts, ownedParts.length);
        this.partsView = Collections.unmodifiableList(Arrays.asList(this.parts));
        this.requestSequence = requestSequence;
        this.hasRequestSequence = hasRequestSequence;
        this.replySender = replySender;
    }

    /** Returns the routing id when the transport delivered one. */
    public Optional<RoutingId> routingId() {
        return Optional.ofNullable(routingId);
    }

    public Optional<RoutingId> spotRid() {
        return Optional.ofNullable(spotRid);
    }

    /** Returns the owned parts as an immutable view without copying. */
    public List<Message> parts() {
        return partsView;
    }

    public Optional<Long> requestSeq() {
        return hasRequestSequence ? Optional.of(requestSequence)
            : Optional.empty();
    }

    /** Returns whether exactly one payload part was received. */
    public boolean isSinglePart() {
        return parts.length == 1;
    }

    /** Returns the first payload part. */
    public Message firstPart() {
        if (parts.length == 0)
            throw new RecvException(RecvResult.NO_DATA);
        return parts[0];
    }

    /** Returns the only payload part or throws when the result is multipart. */
    public Message singlePartOrThrow() {
        if (!isSinglePart())
            throw new RecvException(RecvResult.NOT_SUPPORTED);
        return parts[0];
    }

    public void reply(Message part) {
        reply(List.of(Objects.requireNonNull(part, "part")), SendFlags.NONE);
    }

    public void reply(Message part, SendFlags flags) {
        reply(List.of(Objects.requireNonNull(part, "part")), flags);
    }

    public void reply(List<Message> parts) {
        reply(parts, SendFlags.NONE);
    }

    public void reply(List<Message> parts, SendFlags flags) {
        if (!hasRequestSequence || replySender == null) {
            throw new SubmitException(SubmitResult.INVALID_STATE);
        }
        try {
            replySender.accept(Objects.requireNonNull(parts, "parts"),
              Objects.requireNonNull(flags, "flags"));
        } catch (IllegalStateException ex) {
            throw new SubmitException(SubmitResult.TERMINATED);
        }
    }

    @Override
    public void close() {
        Message.closeAll(parts);
    }
}
