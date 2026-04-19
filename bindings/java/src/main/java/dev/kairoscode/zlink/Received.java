/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Iterator;
import java.util.List;
import java.util.NoSuchElementException;
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
public final class Received implements AutoCloseable, Iterable<Message> {
    public interface PartCursor extends AutoCloseable {
        Message nextPartOrNull();

        @Override
        void close();
    }

    private final long requestSequence;
    private final boolean hasRequestSequence;
    private final BiConsumer<List<Message>, SendFlags> replySender;
    private final byte[] routingIdBytes;
    private final byte[] spotRidBytes;
    private final Runnable onTerminalState;
    private final ArrayList<Message> realizedParts;
    private PartCursor cursor;
    private RoutingId routingId;
    private RoutingId spotRid;
    private List<Message> partsView;
    private boolean closed;

    Received(RoutingId routingId, Message[] parts) {
        this(routingId, null, parts, false, 0L, false, null);
    }

    Received(byte[] routingIdBytes, Message[] parts) {
        this(routingIdBytes, null, parts, false, 0L, false, null);
    }

    Received(RoutingId routingId, Message[] parts, boolean trustedParts) {
        this(routingId, null, parts, trustedParts, 0L, false, null);
    }

    Received(byte[] routingIdBytes, Message[] parts, boolean trustedParts) {
        this(routingIdBytes, null, parts, trustedParts, 0L, false, null);
    }

    Received(RoutingId routingId, RoutingId spotRid, Message[] parts,
             boolean trustedParts, long requestSequence,
             boolean hasRequestSequence,
             BiConsumer<List<Message>, SendFlags> replySender) {
        this(routingId, spotRid, parts, trustedParts, requestSequence,
            hasRequestSequence, replySender, null);
    }

    Received(RoutingId routingId, RoutingId spotRid, Message[] parts,
             boolean trustedParts, long requestSequence,
             boolean hasRequestSequence,
             BiConsumer<List<Message>, SendFlags> replySender,
             Runnable onTerminalState) {
        this.routingId = routingId;
        this.spotRid = spotRid;
        this.routingIdBytes = null;
        this.spotRidBytes = null;
        this.requestSequence = requestSequence;
        this.hasRequestSequence = hasRequestSequence;
        this.replySender = replySender;
        this.onTerminalState = onTerminalState;
        Message[] ownedParts = Objects.requireNonNull(parts, "parts");
        Message[] safeParts = trustedParts ? ownedParts
            : Arrays.copyOf(ownedParts, ownedParts.length);
        this.realizedParts = new ArrayList<>(safeParts.length);
        Collections.addAll(this.realizedParts, safeParts);
        this.cursor = null;
    }

    Received(byte[] routingIdBytes, byte[] spotRidBytes, Message[] parts,
             boolean trustedParts, long requestSequence,
             boolean hasRequestSequence,
             BiConsumer<List<Message>, SendFlags> replySender) {
        this(routingIdBytes, spotRidBytes, parts, trustedParts, requestSequence,
            hasRequestSequence, replySender, null);
    }

    Received(byte[] routingIdBytes, byte[] spotRidBytes, Message[] parts,
             boolean trustedParts, long requestSequence,
             boolean hasRequestSequence,
             BiConsumer<List<Message>, SendFlags> replySender,
             Runnable onTerminalState) {
        this.routingId = null;
        this.spotRid = null;
        this.routingIdBytes = routingIdBytes;
        this.spotRidBytes = spotRidBytes;
        this.requestSequence = requestSequence;
        this.hasRequestSequence = hasRequestSequence;
        this.replySender = replySender;
        this.onTerminalState = onTerminalState;
        Message[] ownedParts = Objects.requireNonNull(parts, "parts");
        Message[] safeParts = trustedParts ? ownedParts
            : Arrays.copyOf(ownedParts, ownedParts.length);
        this.realizedParts = new ArrayList<>(safeParts.length);
        Collections.addAll(this.realizedParts, safeParts);
        this.cursor = null;
    }

    Received(RoutingId routingId, RoutingId spotRid, Message singlePart,
             long requestSequence, boolean hasRequestSequence,
             BiConsumer<List<Message>, SendFlags> replySender) {
        this(routingId, spotRid, singlePart, requestSequence, hasRequestSequence,
            replySender, null);
    }

    Received(RoutingId routingId, RoutingId spotRid, Message singlePart,
             long requestSequence, boolean hasRequestSequence,
             BiConsumer<List<Message>, SendFlags> replySender,
             Runnable onTerminalState) {
        this.routingId = routingId;
        this.spotRid = spotRid;
        this.routingIdBytes = null;
        this.spotRidBytes = null;
        this.requestSequence = requestSequence;
        this.hasRequestSequence = hasRequestSequence;
        this.replySender = replySender;
        this.onTerminalState = onTerminalState;
        this.realizedParts = new ArrayList<>(1);
        this.realizedParts.add(Objects.requireNonNull(singlePart, "singlePart"));
        this.cursor = null;
    }

    Received(byte[] routingIdBytes, byte[] spotRidBytes, Message singlePart,
             long requestSequence, boolean hasRequestSequence,
             BiConsumer<List<Message>, SendFlags> replySender) {
        this(routingIdBytes, spotRidBytes, singlePart, requestSequence,
            hasRequestSequence, replySender, null);
    }

    Received(byte[] routingIdBytes, byte[] spotRidBytes, Message singlePart,
             long requestSequence, boolean hasRequestSequence,
             BiConsumer<List<Message>, SendFlags> replySender,
             Runnable onTerminalState) {
        this.routingId = null;
        this.spotRid = null;
        this.routingIdBytes = routingIdBytes;
        this.spotRidBytes = spotRidBytes;
        this.requestSequence = requestSequence;
        this.hasRequestSequence = hasRequestSequence;
        this.replySender = replySender;
        this.onTerminalState = onTerminalState;
        this.realizedParts = new ArrayList<>(1);
        this.realizedParts.add(Objects.requireNonNull(singlePart, "singlePart"));
        this.cursor = null;
    }

    Received(byte[] routingIdBytes, byte[] spotRidBytes, Message firstPart,
             PartCursor cursor, long requestSequence,
             boolean hasRequestSequence,
             BiConsumer<List<Message>, SendFlags> replySender,
             Runnable onTerminalState) {
        this.routingId = null;
        this.spotRid = null;
        this.routingIdBytes = routingIdBytes;
        this.spotRidBytes = spotRidBytes;
        this.requestSequence = requestSequence;
        this.hasRequestSequence = hasRequestSequence;
        this.replySender = replySender;
        this.onTerminalState = onTerminalState;
        this.realizedParts = new ArrayList<>(4);
        this.realizedParts.add(Objects.requireNonNull(firstPart, "firstPart"));
        this.cursor = cursor;
    }

    /** Returns the routing id when the transport delivered one. */
    public Optional<RoutingId> routingId() {
        return Optional.ofNullable(routingIdOrNull());
    }

    public RoutingId routingIdOrNull() {
        if (routingId == null && routingIdBytes != null) {
            routingId = RoutingId.fromTrusted(routingIdBytes);
        }
        return routingId;
    }

    public RoutingId routingIdOrThrow() {
        RoutingId resolved = routingIdOrNull();
        if (resolved == null)
            throw new RecvException(RecvResult.NO_DATA);
        return resolved;
    }

    public Optional<RoutingId> spotRid() {
        return Optional.ofNullable(spotRidOrNull());
    }

    public RoutingId spotRidOrNull() {
        if (spotRid == null && spotRidBytes != null) {
            spotRid = RoutingId.fromTrusted(spotRidBytes);
        }
        return spotRid;
    }

    /** Returns the owned parts as an immutable view without copying. */
    public List<Message> parts() {
        synchronized (this) {
            ensureOpen();
            materializeAllLocked();
            if (partsView == null) {
                partsView = Collections.unmodifiableList(realizedParts);
            }
            return partsView;
        }
    }

    public Optional<Long> requestSeq() {
        return hasRequestSequence ? Optional.of(requestSequence)
            : Optional.empty();
    }

    /** Returns whether exactly one payload part was received. */
    public boolean isSinglePart() {
        synchronized (this) {
            ensureOpen();
            ensureRealizedThroughLocked(1);
            return realizedParts.size() == 1 && cursor == null;
        }
    }

    /** Returns the first payload part. */
    public Message firstPart() {
        synchronized (this) {
            ensureOpen();
            ensureRealizedThroughLocked(0);
            if (realizedParts.isEmpty())
                throw new RecvException(RecvResult.NO_DATA);
            return realizedParts.get(0);
        }
    }

    /** Returns the only payload part or throws when the result is multipart. */
    public Message singlePartOrThrow() {
        synchronized (this) {
            ensureOpen();
            ensureRealizedThroughLocked(1);
            if (realizedParts.size() != 1 || cursor != null)
                throw new RecvException(RecvResult.NOT_SUPPORTED);
            return realizedParts.get(0);
        }
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
        PartCursor pendingCursor;
        List<Message> toClose;
        synchronized (this) {
            if (closed)
                return;
            closed = true;
            pendingCursor = cursor;
            cursor = null;
            toClose = new ArrayList<>(realizedParts);
            realizedParts.clear();
            partsView = Collections.emptyList();
        }
        Message.closeAll(toClose);
        closeCursorQuietly(pendingCursor);
        markTerminal();
    }

    @Override
    public Iterator<Message> iterator() {
        return new Iterator<>() {
            private int index;

            @Override
            public boolean hasNext() {
                synchronized (Received.this) {
                    if (closed)
                        return false;
                    ensureRealizedThroughLocked(index);
                    return index < realizedParts.size();
                }
            }

            @Override
            public Message next() {
                synchronized (Received.this) {
                    ensureOpen();
                    ensureRealizedThroughLocked(index);
                    if (index >= realizedParts.size())
                        throw new NoSuchElementException();
                    Message next = realizedParts.get(index);
                    index++;
                    return next;
                }
            }
        };
    }

    void forceMaterialize() {
        synchronized (this) {
            if (closed)
                return;
            materializeAllLocked();
        }
    }

    private void ensureOpen() {
        if (closed)
            throw new IllegalStateException("received is closed");
    }

    private void ensureRealizedThroughLocked(int index) {
        while (!closed && realizedParts.size() <= index && cursor != null) {
            Message next = cursor.nextPartOrNull();
            if (next == null) {
                cursor = null;
                markTerminal();
                break;
            }
            realizedParts.add(next);
        }
    }

    private void materializeAllLocked() {
        while (!closed && cursor != null) {
            Message next = cursor.nextPartOrNull();
            if (next == null) {
                cursor = null;
                markTerminal();
                break;
            }
            realizedParts.add(next);
        }
    }

    private void markTerminal() {
        if (onTerminalState != null) {
            onTerminalState.run();
        }
    }

    private static void closeCursorQuietly(PartCursor cursor) {
        if (cursor == null)
            return;
        try {
            cursor.close();
        } catch (RuntimeException ignored) {
        }
    }
}
