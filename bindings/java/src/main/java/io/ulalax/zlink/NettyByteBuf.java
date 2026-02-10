/* SPDX-License-Identifier: MPL-2.0 */

package io.ulalax.zlink;

import java.lang.reflect.Method;
import java.nio.ByteBuffer;

final class NettyByteBuf implements ByteBuf {
    private final Object delegate;
    private final Method readableBytes;
    private final Method writableBytes;
    private final Method readerIndex;
    private final Method writerIndex;
    private final Method readerIndexSet;
    private final Method writerIndexSet;
    private final Method nioBuffer;

    NettyByteBuf(Object delegate) {
        this.delegate = delegate;
        try {
            Class<?> cls = delegate.getClass();
            readableBytes = cls.getMethod("readableBytes");
            writableBytes = cls.getMethod("writableBytes");
            readerIndex = cls.getMethod("readerIndex");
            writerIndex = cls.getMethod("writerIndex");
            readerIndexSet = cls.getMethod("readerIndex", int.class);
            writerIndexSet = cls.getMethod("writerIndex", int.class);
            nioBuffer = cls.getMethod("nioBuffer");
        } catch (ReflectiveOperationException e) {
            throw new IllegalArgumentException("Invalid Netty ByteBuf", e);
        }
    }

    @Override
    public ByteBuffer nioBuffer() {
        try {
            return (ByteBuffer) nioBuffer.invoke(delegate);
        } catch (ReflectiveOperationException e) {
            throw new IllegalStateException("nioBuffer failed", e);
        }
    }

    @Override
    public int readableBytes() {
        try {
            return (int) readableBytes.invoke(delegate);
        } catch (ReflectiveOperationException e) {
            throw new IllegalStateException("readableBytes failed", e);
        }
    }

    @Override
    public int writableBytes() {
        try {
            return (int) writableBytes.invoke(delegate);
        } catch (ReflectiveOperationException e) {
            throw new IllegalStateException("writableBytes failed", e);
        }
    }

    @Override
    public int readerIndex() {
        try {
            return (int) readerIndex.invoke(delegate);
        } catch (ReflectiveOperationException e) {
            throw new IllegalStateException("readerIndex failed", e);
        }
    }

    @Override
    public int writerIndex() {
        try {
            return (int) writerIndex.invoke(delegate);
        } catch (ReflectiveOperationException e) {
            throw new IllegalStateException("writerIndex failed", e);
        }
    }

    @Override
    public void setReaderIndex(int index) {
        try {
            readerIndexSet.invoke(delegate, index);
        } catch (ReflectiveOperationException e) {
            throw new IllegalStateException("readerIndex set failed", e);
        }
    }

    @Override
    public void setWriterIndex(int index) {
        try {
            writerIndexSet.invoke(delegate, index);
        } catch (ReflectiveOperationException e) {
            throw new IllegalStateException("writerIndex set failed", e);
        }
    }

    @Override
    public void advanceReader(int bytes) {
        setReaderIndex(readerIndex() + bytes);
    }

    @Override
    public void advanceWriter(int bytes) {
        setWriterIndex(writerIndex() + bytes);
    }
}
