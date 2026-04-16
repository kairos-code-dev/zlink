/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.Objects;

/** Immutable binary-safe routing id value object. */
public final class RoutingId {
    public static final int MAX_LENGTH = 255;

    private final byte[] value;

    private RoutingId(byte[] value) {
        this.value = value;
    }

    static RoutingId fromTrusted(byte[] value) {
        Objects.requireNonNull(value, "value");
        validateLength(value.length);
        return new RoutingId(value);
    }

    /** Copies the full routing id byte array. */
    public static RoutingId fromBytes(byte[] value) {
        Objects.requireNonNull(value, "value");
        validateLength(value.length);
        return new RoutingId(Arrays.copyOf(value, value.length));
    }

    /** Copies the selected routing id byte range. */
    public static RoutingId fromBytes(byte[] value, int offset, int length) {
        Objects.requireNonNull(value, "value");
        if (offset < 0 || length < 0 || offset > value.length - length)
            throw new IndexOutOfBoundsException("value range out of bounds");
        validateLength(length);
        return new RoutingId(Arrays.copyOfRange(value, offset, offset + length));
    }

    public static RoutingId fromUInt32(int value) {
        byte[] bytes = new byte[Integer.BYTES];
        bytes[0] = (byte) (value >>> 24);
        bytes[1] = (byte) (value >>> 16);
        bytes[2] = (byte) (value >>> 8);
        bytes[3] = (byte) value;
        return new RoutingId(bytes);
    }

    public static RoutingId fromUtf8(String value) {
        Objects.requireNonNull(value, "value");
        byte[] bytes = value.getBytes(StandardCharsets.UTF_8);
        validateLength(bytes.length);
        return new RoutingId(bytes);
    }

    private static void validateLength(int length) {
        if (length > MAX_LENGTH) {
            throw new IllegalArgumentException(
                "routing id too long: " + length + " > " + MAX_LENGTH);
        }
    }

    /** Returns a defensive copy of the routing id bytes. */
    public byte[] toBytes() {
        return Arrays.copyOf(value, value.length);
    }

    byte[] trustedBytes() {
        return value;
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

    public String toHex() {
        StringBuilder out = new StringBuilder(value.length * 2);
        for (byte b : value) {
            out.append(Character.forDigit((b >>> 4) & 0xF, 16));
            out.append(Character.forDigit(b & 0xF, 16));
        }
        return out.toString();
    }

    public String utf8() {
        return new String(value, StandardCharsets.UTF_8);
    }

    public int toUInt32() {
        if (value.length != Integer.BYTES) {
            throw new IllegalStateException(
                "routing id is not a 4-byte STREAM id");
        }
        return ((value[0] & 0xFF) << 24)
            | ((value[1] & 0xFF) << 16)
            | ((value[2] & 0xFF) << 8)
            | (value[3] & 0xFF);
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
