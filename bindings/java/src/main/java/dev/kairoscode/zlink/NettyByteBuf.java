/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.lang.reflect.Method;
import java.nio.ByteBuffer;

final class NettyByteBuf implements ByteBuf {
    private static final ClassValue<HandleSet> HANDLE_CACHE = new ClassValue<>() {
        @Override
        protected HandleSet computeValue(Class<?> type) {
            try {
                MethodHandles.Lookup lookup = MethodHandles.lookup();
                return new HandleSet(
                    asHandle(lookup, type.getMethod("readableBytes"),
                        MethodType.methodType(int.class, Object.class)),
                    asHandle(lookup, type.getMethod("writableBytes"),
                        MethodType.methodType(int.class, Object.class)),
                    asHandle(lookup, type.getMethod("readerIndex"),
                        MethodType.methodType(int.class, Object.class)),
                    asHandle(lookup, type.getMethod("writerIndex"),
                        MethodType.methodType(int.class, Object.class)),
                    asHandle(lookup, type.getMethod("readerIndex", int.class),
                        MethodType.methodType(void.class, Object.class, int.class)),
                    asHandle(lookup, type.getMethod("writerIndex", int.class),
                        MethodType.methodType(void.class, Object.class, int.class)),
                    asHandle(lookup, type.getMethod("nioBuffer"),
                        MethodType.methodType(ByteBuffer.class, Object.class))
                );
            } catch (ReflectiveOperationException e) {
                throw new IllegalArgumentException("Invalid Netty ByteBuf", e);
            }
        }
    };

    private final Object delegate;
    private final HandleSet handles;

    NettyByteBuf(Object delegate) {
        this.delegate = delegate;
        this.handles = HANDLE_CACHE.get(delegate.getClass());
    }

    @Override
    public ByteBuffer nioBuffer() {
        try {
            return (ByteBuffer) handles.nioBuffer.invokeExact(delegate);
        } catch (Throwable e) {
            throw new IllegalStateException("nioBuffer failed", e);
        }
    }

    @Override
    public int readableBytes() {
        try {
            return (int) handles.readableBytes.invokeExact(delegate);
        } catch (Throwable e) {
            throw new IllegalStateException("readableBytes failed", e);
        }
    }

    @Override
    public int writableBytes() {
        try {
            return (int) handles.writableBytes.invokeExact(delegate);
        } catch (Throwable e) {
            throw new IllegalStateException("writableBytes failed", e);
        }
    }

    @Override
    public int readerIndex() {
        try {
            return (int) handles.readerIndex.invokeExact(delegate);
        } catch (Throwable e) {
            throw new IllegalStateException("readerIndex failed", e);
        }
    }

    @Override
    public int writerIndex() {
        try {
            return (int) handles.writerIndex.invokeExact(delegate);
        } catch (Throwable e) {
            throw new IllegalStateException("writerIndex failed", e);
        }
    }

    @Override
    public void setReaderIndex(int index) {
        try {
            handles.readerIndexSet.invokeExact(delegate, index);
        } catch (Throwable e) {
            throw new IllegalStateException("readerIndex set failed", e);
        }
    }

    @Override
    public void setWriterIndex(int index) {
        try {
            handles.writerIndexSet.invokeExact(delegate, index);
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

    private static MethodHandle asHandle(MethodHandles.Lookup lookup,
                                         Method method,
                                         MethodType type)
      throws IllegalAccessException {
        return lookup.unreflect(method).asType(type);
    }

    private record HandleSet(MethodHandle readableBytes,
                             MethodHandle writableBytes,
                             MethodHandle readerIndex,
                             MethodHandle writerIndex,
                             MethodHandle readerIndexSet,
                             MethodHandle writerIndexSet,
                             MethodHandle nioBuffer) {
    }
}
