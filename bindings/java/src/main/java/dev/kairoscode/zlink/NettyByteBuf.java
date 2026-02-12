/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.reflect.Method;
import java.nio.ByteBuffer;

final class NettyByteBuf implements ByteBuf {
    private final Object delegate;
    private final MethodHandle readableBytes;
    private final MethodHandle writableBytes;
    private final MethodHandle readerIndex;
    private final MethodHandle writerIndex;
    private final MethodHandle readerIndexSet;
    private final MethodHandle writerIndexSet;
    private final MethodHandle nioBuffer;

    NettyByteBuf(Object delegate) {
        this.delegate = delegate;
        try {
            Class<?> cls = delegate.getClass();
            MethodHandles.Lookup lookup = MethodHandles.lookup();
            readableBytes = bind(lookup, cls.getMethod("readableBytes"));
            writableBytes = bind(lookup, cls.getMethod("writableBytes"));
            readerIndex = bind(lookup, cls.getMethod("readerIndex"));
            writerIndex = bind(lookup, cls.getMethod("writerIndex"));
            readerIndexSet = bind(lookup, cls.getMethod("readerIndex", int.class));
            writerIndexSet = bind(lookup, cls.getMethod("writerIndex", int.class));
            nioBuffer = bind(lookup, cls.getMethod("nioBuffer"));
        } catch (ReflectiveOperationException e) {
            throw new IllegalArgumentException("Invalid Netty ByteBuf", e);
        }
    }

    @Override
    public ByteBuffer nioBuffer() {
        try {
            return (ByteBuffer) nioBuffer.invokeExact();
        } catch (Throwable e) {
            throw new IllegalStateException("nioBuffer failed", e);
        }
    }

    @Override
    public int readableBytes() {
        try {
            return (int) readableBytes.invokeExact();
        } catch (Throwable e) {
            throw new IllegalStateException("readableBytes failed", e);
        }
    }

    @Override
    public int writableBytes() {
        try {
            return (int) writableBytes.invokeExact();
        } catch (Throwable e) {
            throw new IllegalStateException("writableBytes failed", e);
        }
    }

    @Override
    public int readerIndex() {
        try {
            return (int) readerIndex.invokeExact();
        } catch (Throwable e) {
            throw new IllegalStateException("readerIndex failed", e);
        }
    }

    @Override
    public int writerIndex() {
        try {
            return (int) writerIndex.invokeExact();
        } catch (Throwable e) {
            throw new IllegalStateException("writerIndex failed", e);
        }
    }

    @Override
    public void setReaderIndex(int index) {
        try {
            readerIndexSet.invoke(index);
        } catch (Throwable e) {
            throw new IllegalStateException("readerIndex set failed", e);
        }
    }

    @Override
    public void setWriterIndex(int index) {
        try {
            writerIndexSet.invoke(index);
        } catch (Throwable e) {
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

    private MethodHandle bind(MethodHandles.Lookup lookup, Method method)
      throws IllegalAccessException {
        return lookup.unreflect(method).bindTo(delegate);
    }
}
