package io.ulalax.zlink.internal;

import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;

public final class NativeMsg {
    private static final Linker LINKER = Linker.nativeLinker();

    private static MethodHandle downcall(String name, FunctionDescriptor fd) {
        return LINKER.downcallHandle(LibraryLoader.lookup().find(name).orElseThrow(), fd);
    }

    private static final MethodHandle MH_MSG_INIT = downcall("zlink_msg_init",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_MSG_INIT_SIZE = downcall("zlink_msg_init_size",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG));
    private static final MethodHandle MH_MSG_SEND = downcall("zlink_msg_send",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_MSG_RECV = downcall("zlink_msg_recv",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private static final MethodHandle MH_MSG_CLOSE = downcall("zlink_msg_close",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_MSG_MOVE = downcall("zlink_msg_move",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_MSG_COPY = downcall("zlink_msg_copy",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_MSG_DATA = downcall("zlink_msg_data",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_MSG_SIZE = downcall("zlink_msg_size",
            FunctionDescriptor.of(ValueLayout.JAVA_LONG, ValueLayout.ADDRESS));
    private static final MethodHandle MH_MSGV_CLOSE = downcall("zlink_msgv_close",
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.JAVA_LONG));

    private NativeMsg() {}

    public static int msgInit(MemorySegment msg) {
        try {
            return (int) MH_MSG_INIT.invokeExact(msg);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_msg_init failed", t);
        }
    }

    public static int msgInitSize(MemorySegment msg, int size) {
        try {
            return (int) MH_MSG_INIT_SIZE.invokeExact(msg, (long) size);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_msg_init_size failed", t);
        }
    }

    public static int msgSend(MemorySegment msg, MemorySegment socket, int flags) {
        try {
            return (int) MH_MSG_SEND.invokeExact(msg, socket, flags);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_msg_send failed", t);
        }
    }

    public static int msgRecv(MemorySegment msg, MemorySegment socket, int flags) {
        try {
            return (int) MH_MSG_RECV.invokeExact(msg, socket, flags);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_msg_recv failed", t);
        }
    }

    public static int msgClose(MemorySegment msg) {
        try {
            return (int) MH_MSG_CLOSE.invokeExact(msg);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_msg_close failed", t);
        }
    }

    public static int msgMove(MemorySegment dest, MemorySegment src) {
        try {
            return (int) MH_MSG_MOVE.invokeExact(dest, src);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_msg_move failed", t);
        }
    }

    public static int msgCopy(MemorySegment dest, MemorySegment src) {
        try {
            return (int) MH_MSG_COPY.invokeExact(dest, src);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_msg_copy failed", t);
        }
    }

    public static MemorySegment msgData(MemorySegment msg) {
        try {
            return (MemorySegment) MH_MSG_DATA.invokeExact(msg);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_msg_data failed", t);
        }
    }

    public static long msgSize(MemorySegment msg) {
        try {
            return (long) MH_MSG_SIZE.invokeExact(msg);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_msg_size failed", t);
        }
    }

    public static void msgvClose(MemorySegment parts, long count) {
        try {
            MH_MSGV_CLOSE.invokeExact(parts, count);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_msgv_close failed", t);
        }
    }

    public static byte[][] readMsgVector(MemorySegment partsAddr, long count) {
        if (partsAddr == null || partsAddr.address() == 0 || count <= 0)
            return new byte[0][];
        long msgSize = NativeLayouts.MSG_LAYOUT.byteSize();
        byte[][] out = new byte[(int) count][];
        try {
            MemorySegment parts = MemorySegment.ofAddress(partsAddr.address()).reinterpret(msgSize * count);
            for (int i = 0; i < count; i++) {
                MemorySegment msg = parts.asSlice((long) i * msgSize, msgSize);
                long size = msgSize(msg);
                MemorySegment data = msgData(msg).reinterpret(size);
                byte[] buf = new byte[(int) size];
                MemorySegment.copy(data, 0, MemorySegment.ofArray(buf), 0, size);
                out[i] = buf;
            }
        } finally {
            msgvClose(partsAddr, count);
        }
        return out;
    }
}
