/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.nativeapi;

import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.errors.ZlinkRequestException;
import systems.zlink.contracts.sockets.RequestResult;
import systems.zlink.contracts.core.RoutingId;
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

public final class RoutedRequestSupport {
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

    public static MemorySegment replyCallback() {
        return REPLY_CALLBACK;
    }

    public static long nextRequestId() {
        return NEXT_REQUEST_ID.getAndIncrement();
    }

    public static MemorySegment userData(long requestId) {
        return MemorySegment.ofAddress(requestId);
    }

    public static CompletableFuture<Received> registerPending(long requestId,
                                                              long timeoutMs) {
        CompletableFuture<Received> future = new CompletableFuture<>();
        PENDING.put(requestId, future);
        RequestReplySupport.armTimeout(PENDING, requestId, future, timeoutMs);
        return future;
    }

    public static void removePending(long requestId) {
        PENDING.remove(requestId);
    }

    public static MemorySegment movePayloadToNative(Arena arena,
                                                    List<Message> payload) {
        long messageSize = NativeLayouts.MESSAGE_LAYOUT.byteSize();
        MemorySegment nativeParts = arena.allocate(messageSize * payload.size(),
          NativeLayouts.MESSAGE_LAYOUT.byteAlignment());
        int built = 0;
        try {
            for (int i = 0; i < payload.size(); i++) {
                InternalAccess.messageTransferTo(payload.get(i),
                  nativeParts.asSlice((long) i * messageSize, messageSize));
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

    public static int toTimeoutInt(long timeoutMs) {
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
                        new ZlinkRequestException(RequestResult.fromValue(result),
                            result));
                }
                return;
            }
            if (future != null) {
                Message[] frames = InternalAccess.messageFromOwnedMessageVectorShared(
                    parts, partCount);
                RequestReplySupport.completeAsync(future,
                    () -> InternalAccess.received((RoutingId) null,
                        (RoutingId) null, frames, 0L, false, null));
            }
        } catch (Throwable error) {
            if (future != null) {
                RequestReplySupport.completeExceptionallyAsync(future, error);
            }
        } finally {
            NativeMessage.multipartClose(parts, partCount);
        }
    }
}
