/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import java.nio.ByteBuffer;
import java.util.Objects;

final class NettyByteBuf implements ByteBuf {
    private final io.netty.buffer.ByteBuf delegate;

    NettyByteBuf(io.netty.buffer.ByteBuf delegate) {
        this.delegate = Objects.requireNonNull(delegate, "delegate");
    }

    @Override
    public ByteBuffer nioBuffer() {
        return delegate.nioBuffer();
    }

    @Override
    public int readableBytes() {
        return delegate.readableBytes();
    }

    @Override
    public int writableBytes() {
        return delegate.writableBytes();
    }

    @Override
    public int readerIndex() {
        return delegate.readerIndex();
    }

    @Override
    public int writerIndex() {
        return delegate.writerIndex();
    }

    @Override
    public void setReaderIndex(int index) {
        delegate.readerIndex(index);
    }

    @Override
    public void setWriterIndex(int index) {
        delegate.writerIndex(index);
    }

    @Override
    public void advanceReader(int bytes) {
        delegate.readerIndex(delegate.readerIndex() + bytes);
    }

    @Override
    public void advanceWriter(int bytes) {
        delegate.writerIndex(delegate.writerIndex() + bytes);
    }
}
