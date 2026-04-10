package dev.kairoscode.zlink.internal;

import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.SymbolLookup;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;

public final class NativeMsg {
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

    private static final MethodHandle MH_MSG_INIT = downcall("zlink_msg_init",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_MSG_INIT_SIZE = downcall("zlink_msg_init_size",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG));
    private static final MethodHandle MH_MSG_INIT_DATA = downcall("zlink_msg_init_data",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
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
    private static final MethodHandle MH_MSG_REFCNT = downcall("zlink_msg_refcnt",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final MethodHandle MH_MSG_GETS = downcall("zlink_msg_gets",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_MSGV_CLOSE = downcallAny(
            new String[] {"zlink_multipart_close", "zlink_msgv_close"},
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.JAVA_LONG));
    private static final MethodHandle MH_FREE = cDowncall("free",
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS));
    private static final MethodHandle MH_DEALER_REQUEST = downcall("zlink_dealer_request",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.JAVA_LONG,
                    ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    private static final MethodHandle MH_ROUTER_REQUEST = downcall("zlink_router_request",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_LONG, ValueLayout.JAVA_INT,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final MethodHandle MH_ROUTER_REPLY = downcall("zlink_router_reply",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.JAVA_LONG,
                    ValueLayout.ADDRESS, ValueLayout.JAVA_LONG));
    private static final MethodHandle MH_ROUTER_HANDLER = downcall(
            "zlink_router_handler",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS));

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

    public static int msgInitData(MemorySegment msg, MemorySegment data, long size,
                                  MemorySegment freeFn, MemorySegment hint) {
        try {
            return (int) MH_MSG_INIT_DATA.invokeExact(msg, data, size, freeFn, hint);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_msg_init_data failed", t);
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

    public static int msgRefCnt(MemorySegment msg) {
        try {
            return (int) MH_MSG_REFCNT.invokeExact(msg);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_msg_refcnt failed", t);
        }
    }

    public static MemorySegment msgGets(MemorySegment msg, MemorySegment property) {
        try {
            return (MemorySegment) MH_MSG_GETS.invokeExact(msg, property);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_msg_gets failed", t);
        }
    }

    public static void msgvClose(MemorySegment parts, long count) {
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

    public static int dealerRequest(MemorySegment dealer, MemorySegment parts,
                                    long partCount, int timeoutMs,
                                    MemorySegment handler,
                                    MemorySegment userData) {
        try {
            return (int) MH_DEALER_REQUEST.invokeExact(dealer, parts, partCount,
                timeoutMs, handler, userData);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_dealer_request failed", t);
        }
    }

    public static int routerRequest(MemorySegment router, MemorySegment peerRid,
                                    MemorySegment parts, long partCount,
                                    int timeoutMs, MemorySegment handler,
                                    MemorySegment userData) {
        try {
            return (int) MH_ROUTER_REQUEST.invokeExact(router, peerRid, parts,
                partCount, timeoutMs, handler, userData);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_router_request failed", t);
        }
    }

    public static int routerReply(MemorySegment router, MemorySegment peerRid,
                                  long requestSeq, MemorySegment parts,
                                  long partCount) {
        try {
            return (int) MH_ROUTER_REPLY.invokeExact(router, peerRid, requestSeq,
                parts, partCount);
        } catch (Throwable t) {
            throw new RuntimeException("zlink_router_reply failed", t);
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

}
