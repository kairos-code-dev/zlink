/* SPDX-License-Identifier: MPL-2.0 */

package io.ulalax.zlink;

import java.nio.ByteBuffer;

final class NioByteBuf implements ByteBuf {
    private final ByteBuffer buffer;
    private int readerIndex;
    private int writerIndex;

    NioByteBuf(ByteBuffer buffer) {
        this.buffer = buffer;
        this.readerIndex = buffer.position();
        this.writerIndex = buffer.limit();
    }

    @Override
    public ByteBuffer nioBuffer() {
        ByteBuffer dup = buffer.duplicate();
        dup.position(readerIndex);
        dup.limit(writerIndex);
        return dup;
    }

    @Override
    public int readableBytes() {
        return writerIndex - readerIndex;
    }

    @Override
    public int writableBytes() {
        return buffer.capacity() - writerIndex;
    }

    @Override
    public int readerIndex() {
        return readerIndex;
    }

    @Override
    public int writerIndex() {
        return writerIndex;
    }

    @Override
    public void setReaderIndex(int index) {
        if (index < 0 || index > writerIndex)
            throw new IndexOutOfBoundsException("readerIndex");
        readerIndex = index;
    }

    @Override
    public void setWriterIndex(int index) {
        if (index < readerIndex || index > buffer.capacity())
            throw new IndexOutOfBoundsException("writerIndex");
        writerIndex = index;
    }

    @Override
    public void advanceReader(int bytes) {
        setReaderIndex(readerIndex + bytes);
    }

    @Override
    public void advanceWriter(int bytes) {
        setWriterIndex(writerIndex + bytes);
    }
}
