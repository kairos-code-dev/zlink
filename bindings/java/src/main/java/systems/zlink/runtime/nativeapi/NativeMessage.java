package systems.zlink.runtime.nativeapi;

import systems.zlink.contracts.errors.ZlinkConfigException;
import systems.zlink.contracts.errors.ConfigResult;
import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.SymbolLookup;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;

public final class NativeMessage {
    private static final Linker LINKER = Linker.nativeLinker();
    private static final SymbolLookup LOOKUP = LibraryLoader.lookup();
    private static final SymbolLookup C_LOOKUP = LINKER.defaultLookup();

    private static MemorySegment requireSymbol(String name) {
        return LOOKUP.find(name).orElseThrow(
          () -> new IllegalStateException(
            "Missing native symbol '" + name
              + "'. Loaded libzlink is incompatible with this Java binding."));
    }

    private static MethodHandle downcall(String name, FunctionDescriptor fd) {
        return LOOKUP.find(name)
          .map(symbol -> LINKER.downcallHandle(symbol, fd))
          .orElseGet(() -> missingDowncall(name, fd));
    }

    private static MethodHandle downcallCritical(String name,
                                                 FunctionDescriptor fd) {
        return LOOKUP.find(name)
          .map(symbol -> LINKER.downcallHandle(symbol, fd,
              Linker.Option.critical(false)))
          .orElseGet(() -> missingDowncall(name, fd));
    }

    private static MethodHandle downcallAny(String[] names, FunctionDescriptor fd) {
        for (String name : names) {
            if (LOOKUP.find(name).isPresent()) {
                return LINKER.downcallHandle(requireSymbol(name), fd);
            }
        }
        return missingDowncall(
          "one of: " + String.join(", ", names), fd);
    }

    private static MethodHandle cDowncall(String name, FunctionDescriptor fd) {
        return C_LOOKUP.find(name)
          .map(symbol -> LINKER.downcallHandle(symbol, fd))
          .orElseGet(() -> missingDowncall(name, fd));
    }

    private static MethodHandle missingDowncall(String name,
                                                FunctionDescriptor fd) {
        MethodType methodType = fd.toMethodType();
        IllegalStateException failure =
          new IllegalStateException(
            "Missing native symbol '" + name
              + "'. Loaded libzlink is incompatible with this Java binding.");
        MethodHandle throwing = MethodHandles.throwException(
          methodType.returnType(), IllegalStateException.class);
        throwing = MethodHandles.insertArguments(throwing, 0, failure);
        return MethodHandles.dropArguments(throwing, 0,
          methodType.parameterArray());
    }

    private static final MethodHandle MH_MSG_INIT = downcallCritical("zlink_msg_init",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_MSG_INIT_SIZE = downcallCritical("zlink_msg_init_size",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG));
    private static final MethodHandle MH_MSG_CLOSE = downcallCritical("zlink_msg_close",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_MSG_MOVE = downcallCritical("zlink_msg_move",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_MSG_COPY = downcallCritical("zlink_msg_copy",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_MSG_DATA = downcallCritical("zlink_msg_data",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_MSG_DATA_ADDR = downcallCritical(
            "zlink_java_msg_data_addr",
            FunctionDescriptor.of(ValueLayout.JAVA_LONG, ValueLayout.ADDRESS));
    private static final MethodHandle MH_MSG_SIZE = downcallCritical("zlink_msg_size",
            FunctionDescriptor.of(ValueLayout.JAVA_LONG, ValueLayout.ADDRESS));
    private static final MethodHandle MH_MSG_REFCNT = downcallCritical("zlink_msg_refcnt",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    private static final MethodHandle MH_MSG_GETS = downcall("zlink_msg_gets",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_MSGV_CLOSE = downcallAny(
            new String[] {"zlink_multipart_close", "zlink_msgv_close"},
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.JAVA_LONG));
    private static final MethodHandle MH_FREE = cDowncall("free",
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS));
    private static final MethodHandle MH_ROUTER_HANDLER = downcall(
            "zlink_router_handler",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    private NativeMessage() {}

    public static int messageInit(MemorySegment msg) {
        try {
            return (int) MH_MSG_INIT.invokeExact(msg);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_msg_init failed", t);
        }
    }

    public static int messageInitSize(MemorySegment msg, int size) {
        try {
            return (int) MH_MSG_INIT_SIZE.invokeExact(msg, (long) size);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_msg_init_size failed", t);
        }
    }

    public static int messageClose(MemorySegment msg) {
        try {
            return (int) MH_MSG_CLOSE.invokeExact(msg);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_msg_close failed", t);
        }
    }

    public static int messageMove(MemorySegment dest, MemorySegment src) {
        try {
            return (int) MH_MSG_MOVE.invokeExact(dest, src);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_msg_move failed", t);
        }
    }

    public static int messageCopy(MemorySegment dest, MemorySegment src) {
        try {
            return (int) MH_MSG_COPY.invokeExact(dest, src);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_msg_copy failed", t);
        }
    }

    public static MemorySegment messageData(MemorySegment msg) {
        try {
            return (MemorySegment) MH_MSG_DATA.invokeExact(msg);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_msg_data failed", t);
        }
    }

    public static long messageDataAddress(MemorySegment msg) {
        try {
            return (long) MH_MSG_DATA_ADDR.invokeExact(msg);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_java_msg_data_addr failed", t);
        }
    }

    public static long messageSize(MemorySegment msg) {
        try {
            return (long) MH_MSG_SIZE.invokeExact(msg);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_msg_size failed", t);
        }
    }

    public static int messageRefCount(MemorySegment msg) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment errorOut = arena.allocate(ValueLayout.JAVA_INT);
            int refCount = (int) MH_MSG_REFCNT.invokeExact(msg, errorOut);
            int configResult = errorOut.get(ValueLayout.JAVA_INT, 0);
            if (refCount < 0 || configResult != 0) {
                throw new ZlinkConfigException(ConfigResult.fromValue(configResult));
            }
            return refCount;
        } catch (ZlinkConfigException ex) {
            throw ex;
        } catch (Throwable t) {
            throw new RuntimeException("zlink_msg_refcnt failed", t);
        }
    }

    public static MemorySegment messageGetProperty(MemorySegment msg, MemorySegment property) {
        try {
            return (MemorySegment) MH_MSG_GETS.invokeExact(msg, property);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_msg_gets failed", t);
        }
    }

    public static void messageVectorClose(MemorySegment parts, long count) {
        try {
            MH_MSGV_CLOSE.invokeExact(parts, count);
            if (parts != null && parts.address() != 0) {
                MH_FREE.invokeExact(parts);
            }
        } catch (Throwable t) {
            throw new RuntimeException("zlink_multipart_close failed", t);
        }
    }

    public static void multipartClose(MemorySegment parts, long count) {
        try {
            MH_MSGV_CLOSE.invokeExact(parts, count);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_multipart_close failed", t);
        }
    }

    public static int routerHandler(MemorySegment router,
                                    MemorySegment handler,
                                    MemorySegment userData) {
        try {
            return (int) MH_ROUTER_HANDLER.invokeExact(router, handler,
                userData);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_router_handler failed", t);
        }
    }

    public static int routerRecv(MemorySegment router,
                                 MemorySegment sourceNodeRidOut,
                                 MemorySegment sourceSpotRidOut,
                                 MemorySegment requestSeqOut,
                                 MemorySegment partsOut,
                                 MemorySegment partCountOut,
                                 int flags) {
        try {
            return Native.routerRecv(router, sourceNodeRidOut,
                sourceSpotRidOut, requestSeqOut, partsOut, partCountOut,
                flags);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_router_recv_part failed", t);
        }
    }

}
