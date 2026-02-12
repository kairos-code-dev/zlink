/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import java.lang.foreign.MemorySegment;
import java.nio.ByteBuffer;
import java.util.Objects;

/**
 * Lightweight byte span view for low-copy send/recv paths.
 */
public interface ByteSpan {
    MemorySegment segment();

    int length();

    default ByteBuffer asByteBuffer() {
        if (length() == 0)
            return ByteBuffer.allocate(0);
        return segment().asSlice(0, length()).asByteBuffer();
    }

    static ByteSpan of(byte[] data) {
        return of(data, 0, data.length);
    }

    static ByteSpan of(byte[] data, int offset, int length) {
        Objects.requireNonNull(data, "data");
        validateRange(data.length, offset, length, "data");
        MemorySegment seg = length == 0 ? MemorySegment.NULL
            : MemorySegment.ofArray(data).asSlice(offset, length);
        return new SegmentBackedSpan(seg, length);
    }

    static ByteSpan of(ByteBuffer data) {
        Objects.requireNonNull(data, "data");
        int length = data.remaining();
        MemorySegment seg = length == 0 ? MemorySegment.NULL : MemorySegment.ofBuffer(data.slice());
        return new SegmentBackedSpan(seg, length);
    }

    static ByteSpan of(MemorySegment data) {
        Objects.requireNonNull(data, "data");
        long size = data.byteSize();
        if (size > Integer.MAX_VALUE)
            throw new IllegalArgumentException("segment too large: " + size);
        MemorySegment seg = size == 0 ? MemorySegment.NULL : data.asSlice(0, size);
        return new SegmentBackedSpan(seg, (int) size);
    }

    private static void validateRange(int total, int offset, int length, String name) {
        if (offset < 0 || length < 0 || offset > total - length)
            throw new IndexOutOfBoundsException(name + " range out of bounds");
    }

    record SegmentBackedSpan(MemorySegment segment, int length) implements ByteSpan {}
}
