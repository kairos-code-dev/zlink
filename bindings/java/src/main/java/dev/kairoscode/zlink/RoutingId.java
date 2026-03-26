/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import java.nio.ByteBuffer;
import java.util.Arrays;
import java.util.Objects;

/** Immutable binary-safe routing id value object. */
public final class RoutingId {
    private final byte[] value;

    private RoutingId(byte[] value) {
        this.value = value;
    }

    /** Copies the full routing id byte array. */
    public static RoutingId copyOf(byte[] value) {
        Objects.requireNonNull(value, "value");
        return new RoutingId(Arrays.copyOf(value, value.length));
    }

    /** Copies the selected routing id byte range. */
    public static RoutingId copyOf(byte[] value, int offset, int length) {
        Objects.requireNonNull(value, "value");
        if (offset < 0 || length < 0 || offset > value.length - length)
            throw new IndexOutOfBoundsException("value range out of bounds");
        return new RoutingId(Arrays.copyOfRange(value, offset, offset + length));
    }

    /** Returns a defensive copy of the routing id bytes. */
    public byte[] toByteArray() {
        return Arrays.copyOf(value, value.length);
    }

    /** Returns a read-only buffer view over the routing id bytes. */
    public ByteBuffer asReadOnlyBuffer() {
        return ByteBuffer.wrap(value).asReadOnlyBuffer();
    }

    /** Returns the routing id byte length. */
    public int size() {
        return value.length;
    }

    /** Returns whether the routing id is empty. */
    public boolean empty() {
        return value.length == 0;
    }

    @Override
    public boolean equals(Object other) {
        return other instanceof RoutingId rid && Arrays.equals(value, rid.value);
    }

    @Override
    public int hashCode() {
        return Arrays.hashCode(value);
    }
}
