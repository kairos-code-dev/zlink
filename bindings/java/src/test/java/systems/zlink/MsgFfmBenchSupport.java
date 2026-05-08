package systems.zlink;

import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemoryLayout;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.SymbolLookup;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;

final class MsgFfmBenchSupport {
    static final MemoryLayout MSG_LAYOUT = MemoryLayout
        .sequenceLayout(64, ValueLayout.JAVA_BYTE)
        .withByteAlignment(ValueLayout.ADDRESS.byteAlignment());

    private static final Linker LINKER = Linker.nativeLinker();
    private static final SymbolLookup LOOKUP;
    private static final MethodHandle MH_MSG_INIT;
    private static final MethodHandle MH_MSG_INIT_SIZE;
    private static final MethodHandle MH_MSG_CLOSE;
    private static final MethodHandle MH_MSG_MOVE;
    private static final MethodHandle MH_MSG_COPY;
    private static final MethodHandle MH_MSG_DATA;
    private static final MethodHandle MH_MSG_DATA_ADDR;
    private static final MethodHandle MH_MSG_SIZE;

    static {
        Zlink.version();
        LOOKUP = SymbolLookup.loaderLookup();
        MH_MSG_INIT = downcall("zlink_msg_init",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
        MH_MSG_INIT_SIZE = downcall("zlink_msg_init_size",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                ValueLayout.JAVA_LONG));
        MH_MSG_CLOSE = downcall("zlink_msg_close",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
        MH_MSG_MOVE = downcall("zlink_msg_move",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                ValueLayout.ADDRESS));
        MH_MSG_COPY = downcall("zlink_msg_copy",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                ValueLayout.ADDRESS));
        MH_MSG_DATA = downcall("zlink_msg_data",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS));
        MH_MSG_DATA_ADDR = downcall("zlink_java_msg_data_addr",
            FunctionDescriptor.of(ValueLayout.JAVA_LONG, ValueLayout.ADDRESS));
        MH_MSG_SIZE = downcall("zlink_msg_size",
            FunctionDescriptor.of(ValueLayout.JAVA_LONG, ValueLayout.ADDRESS));
    }

    private MsgFfmBenchSupport() {
    }

    static int msgInit(MemorySegment msg) {
        try {
            return (int) MH_MSG_INIT.invokeExact(msg);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_msg_init failed", t);
        }
    }

    static int msgInitSize(MemorySegment msg, int size) {
        try {
            return (int) MH_MSG_INIT_SIZE.invokeExact(msg, (long) size);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_msg_init_size failed", t);
        }
    }

    static int msgClose(MemorySegment msg) {
        try {
            return (int) MH_MSG_CLOSE.invokeExact(msg);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_msg_close failed", t);
        }
    }

    static int msgMove(MemorySegment dest, MemorySegment src) {
        try {
            return (int) MH_MSG_MOVE.invokeExact(dest, src);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_msg_move failed", t);
        }
    }

    static int msgCopy(MemorySegment dest, MemorySegment src) {
        try {
            return (int) MH_MSG_COPY.invokeExact(dest, src);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_msg_copy failed", t);
        }
    }

    static MemorySegment msgData(MemorySegment msg) {
        try {
            return (MemorySegment) MH_MSG_DATA.invokeExact(msg);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_msg_data failed", t);
        }
    }

    static long msgDataAddr(MemorySegment msg) {
        try {
            return (long) MH_MSG_DATA_ADDR.invokeExact(msg);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_java_msg_data_addr failed", t);
        }
    }

    static long msgSize(MemorySegment msg) {
        try {
            return (long) MH_MSG_SIZE.invokeExact(msg);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_msg_size failed", t);
        }
    }

    private static MethodHandle downcall(String name, FunctionDescriptor fd) {
        MemorySegment symbol = LOOKUP.find(name).orElseThrow(
            () -> new IllegalStateException("Missing native symbol: " + name));
        return LINKER.downcallHandle(symbol, fd);
    }
}
