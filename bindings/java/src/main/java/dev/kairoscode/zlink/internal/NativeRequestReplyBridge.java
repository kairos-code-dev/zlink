package dev.kairoscode.zlink.internal;

import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.SymbolLookup;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;

public final class NativeRequestReplyBridge {
    private static final Linker LINKER = Linker.nativeLinker();
    private static final SymbolLookup LOOKUP = LibraryLoader.lookup();
    private static final MethodHandle MH_DEALER_REQUEST_SYNC = downcall(
        "zlink_java_dealer_request_sync",
        FunctionDescriptor.of(ValueLayout.JAVA_INT,
            ValueLayout.ADDRESS,
            ValueLayout.ADDRESS,
            ValueLayout.JAVA_LONG,
            ValueLayout.JAVA_INT,
            ValueLayout.JAVA_INT,
            ValueLayout.ADDRESS,
            ValueLayout.ADDRESS,
            ValueLayout.ADDRESS));
    private static final MethodHandle MH_ROUTER_REQUEST_SYNC = downcall(
        "zlink_java_router_request_sync",
        FunctionDescriptor.of(ValueLayout.JAVA_INT,
            ValueLayout.ADDRESS,
            ValueLayout.ADDRESS,
            ValueLayout.ADDRESS,
            ValueLayout.JAVA_LONG,
            ValueLayout.JAVA_INT,
            ValueLayout.JAVA_INT,
            ValueLayout.ADDRESS,
            ValueLayout.ADDRESS,
            ValueLayout.ADDRESS));

    private NativeRequestReplyBridge() {
    }

    public static ReplyResult dealerRequestSync(MemorySegment dealer,
                                                MemorySegment parts,
                                                long partCount,
                                                int flags,
                                                int timeoutMs) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment requestResultOut = arena.allocate(ValueLayout.JAVA_INT);
            MemorySegment replyPartsOut = arena.allocate(ValueLayout.ADDRESS);
            MemorySegment replyPartCountOut = arena.allocate(ValueLayout.JAVA_LONG);
            int submitResult = (int) MH_DEALER_REQUEST_SYNC.invokeExact(
                dealer, parts, partCount, flags, timeoutMs,
                requestResultOut, replyPartsOut, replyPartCountOut);
            return new ReplyResult(submitResult,
                requestResultOut.get(ValueLayout.JAVA_INT, 0),
                replyPartsOut.get(ValueLayout.ADDRESS, 0),
                replyPartCountOut.get(ValueLayout.JAVA_LONG, 0));
        } catch (Throwable t) {
            throw new RuntimeException("zlink_java_dealer_request_sync failed", t);
        }
    }

    public static ReplyResult routerRequestSync(MemorySegment router,
                                                MemorySegment peerRid,
                                                MemorySegment parts,
                                                long partCount,
                                                int flags,
                                                int timeoutMs) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment requestResultOut = arena.allocate(ValueLayout.JAVA_INT);
            MemorySegment replyPartsOut = arena.allocate(ValueLayout.ADDRESS);
            MemorySegment replyPartCountOut = arena.allocate(ValueLayout.JAVA_LONG);
            int submitResult = (int) MH_ROUTER_REQUEST_SYNC.invokeExact(
                router, peerRid, parts, partCount, flags, timeoutMs,
                requestResultOut, replyPartsOut, replyPartCountOut);
            return new ReplyResult(submitResult,
                requestResultOut.get(ValueLayout.JAVA_INT, 0),
                replyPartsOut.get(ValueLayout.ADDRESS, 0),
                replyPartCountOut.get(ValueLayout.JAVA_LONG, 0));
        } catch (Throwable t) {
            throw new RuntimeException("zlink_java_router_request_sync failed", t);
        }
    }

    private static MethodHandle downcall(String name, FunctionDescriptor fd) {
        return LOOKUP.find(name)
            .map(symbol -> LINKER.downcallHandle(symbol, fd))
            .orElseGet(() -> missingDowncall(name, fd));
    }

    private static MethodHandle missingDowncall(String name,
                                                FunctionDescriptor fd) {
        MethodType methodType = fd.toMethodType();
        IllegalStateException failure =
            new IllegalStateException(
                "Missing native symbol '" + name
                    + "'. Loaded zlink Java bridge is incompatible with this binding.");
        MethodHandle throwing = MethodHandles.throwException(
            methodType.returnType(), IllegalStateException.class);
        throwing = MethodHandles.insertArguments(throwing, 0, failure);
        return MethodHandles.dropArguments(throwing, 0,
            methodType.parameterArray());
    }

    public record ReplyResult(int submitResult,
                              int requestResult,
                              MemorySegment replyParts,
                              long replyPartCount) {
    }
}
