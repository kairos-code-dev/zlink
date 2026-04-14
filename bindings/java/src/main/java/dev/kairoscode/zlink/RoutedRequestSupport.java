/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.InternalAccess;
import dev.kairoscode.zlink.internal.NativeLayouts;
import dev.kairoscode.zlink.internal.NativeMsg;
import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentMap;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicLong;

final class RoutedRequestSupport {
    private static final Linker LINKER = Linker.nativeLinker();
    private static final FunctionDescriptor FD_REPLY_CALLBACK =
      FunctionDescriptor.ofVoid(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.JAVA_LONG, ValueLayout.ADDRESS);
    private static final Arena CALLBACK_ARENA = Arena.ofShared();
    private static final MemorySegment REPLY_CALLBACK = LINKER.upcallStub(
      callbackHandle(), FD_REPLY_CALLBACK, CALLBACK_ARENA);
    private static final AtomicLong NEXT_REQUEST_ID = new AtomicLong(1L);
    private static final ConcurrentMap<Long, CompletableFuture<Received>> PENDING =
      new ConcurrentHashMap<>();

    private RoutedRequestSupport() {
    }

    static MemorySegment replyCallback() {
        return REPLY_CALLBACK;
    }

    static long nextRequestId() {
        return NEXT_REQUEST_ID.getAndIncrement();
    }

    static MemorySegment userData(long requestId) {
        return MemorySegment.ofAddress(requestId);
    }

    static CompletableFuture<Received> registerPending(long requestId,
                                                       long timeoutMs) {
        CompletableFuture<Received> future = new CompletableFuture<>();
        PENDING.put(requestId, future);
        RequestReplySupport.armTimeout(PENDING, requestId, future, timeoutMs);
        return future;
    }

    static void removePending(long requestId) {
        PENDING.remove(requestId);
    }

    static MemorySegment movePayloadToNative(Arena arena, List<Message> payload) {
        long msgSize = NativeLayouts.MSG_LAYOUT.byteSize();
        MemorySegment nativeParts = arena.allocate(msgSize * payload.size(),
          NativeLayouts.MSG_LAYOUT.byteAlignment());
        int built = 0;
        try {
            for (int i = 0; i < payload.size(); i++) {
                payload.get(i).transferTo(nativeParts.asSlice((long) i * msgSize,
                  msgSize));
                built++;
            }
            return nativeParts;
        } catch (RuntimeException ex) {
            for (int i = built; i < payload.size(); i++) {
                try {
                    payload.get(i).close();
                } catch (RuntimeException ignored) {
                }
            }
            throw ex;
        }
    }

    static int toTimeoutInt(long timeoutMs) {
        if (timeoutMs <= 1L) {
            return 1;
        }
        return timeoutMs >= Integer.MAX_VALUE ? Integer.MAX_VALUE : (int) timeoutMs;
    }

    private static MethodHandle callbackHandle() {
        try {
            return MethodHandles.lookup().findStatic(RoutedRequestSupport.class,
              "handleReplyCallback", MethodType.methodType(void.class, int.class,
                MemorySegment.class, long.class, MemorySegment.class));
        } catch (ReflectiveOperationException ex) {
            throw new ExceptionInInitializerError(ex);
        }
    }

    private static void handleReplyCallback(int result,
                                            MemorySegment parts,
                                            long partCount,
                                            MemorySegment userData) {
        long requestId = userData.address();
        CompletableFuture<Received> future = PENDING.remove(requestId);
        try {
            if (result != RequestResult.OK.value()) {
                if (future != null) {
                    RequestReplySupport.completeExceptionallyAsync(future,
                        new RequestException(RequestResult.fromValue(result),
                            result));
                }
                return;
            }
            if (future != null) {
                Message[] frames = InternalAccess.messageFromOwnedMsgVectorShared(
                    parts, partCount);
                RequestReplySupport.completeAsync(future,
                    () -> new Received(null, null, frames, true, 0L, false,
                        null));
            }
        } catch (Throwable error) {
            if (future != null) {
                RequestReplySupport.completeExceptionallyAsync(future, error);
            }
        } finally {
            NativeMsg.multipartClose(parts, partCount);
        }
    }
}
