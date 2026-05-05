/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.RequestResult;
import dev.kairoscode.zlink.internal.InternalAccess;
import dev.kairoscode.zlink.internal.NativeMsg;
import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentMap;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.atomic.AtomicLong;
import java.util.function.BiConsumer;

final class ActorRequestCallbacks {
    private static final Linker LINKER = Linker.nativeLinker();
    private static final FunctionDescriptor FD_REPLY_CALLBACK =
      FunctionDescriptor.ofVoid(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.JAVA_LONG, ValueLayout.ADDRESS);
    private static final Arena CALLBACK_ARENA = Arena.ofShared();
    static final MemorySegment REPLY_CALLBACK;
    private static final AtomicLong NEXT_ID = new AtomicLong(1L);
    private static final ConcurrentMap<Long, Pending> PENDING =
      new ConcurrentHashMap<>();

    static {
        try {
            REPLY_CALLBACK = LINKER.upcallStub(MethodHandles.lookup().findStatic(
              ActorRequestCallbacks.class, "handleReplyCallback",
              MethodType.methodType(void.class, int.class, MemorySegment.class,
                long.class, MemorySegment.class)), FD_REPLY_CALLBACK,
              CALLBACK_ARENA);
        } catch (ReflectiveOperationException ex) {
            throw new ExceptionInInitializerError(ex);
        }
    }

    private ActorRequestCallbacks() {
    }

    static PendingToken register(BiConsumer<RequestResult, List<Message>> callback) {
        long id = NEXT_ID.getAndIncrement();
        CompletableFuture<Void> future = new CompletableFuture<>();
        PENDING.put(id, new Pending(callback, future));
        return new PendingToken(id, future);
    }

    static void remove(long id) {
        Pending pending = PENDING.remove(id);
        if (pending != null) {
            pending.future().complete(null);
        }
    }

    @SuppressWarnings("unused")
    private static void handleReplyCallback(int result,
                                            MemorySegment parts,
                                            long partCount,
                                            MemorySegment userdata) {
        long id = userdata.address();
        Pending pending = PENDING.remove(id);
        if (pending == null) {
            NativeMsg.multipartClose(parts, partCount);
            return;
        }
        try {
            Message[] messages = result == RequestResult.OK.value()
              ? InternalAccess.messageFromOwnedMsgVectorShared(parts, partCount)
              : new Message[0];
            pending.callback().accept(RequestResult.fromValue(result),
              List.copyOf(Arrays.asList(messages)));
            pending.future().complete(null);
        } catch (RuntimeException ex) {
            pending.future().completeExceptionally(ex);
            throw ex;
        } finally {
            NativeMsg.multipartClose(parts, partCount);
        }
    }

    record PendingToken(long id, CompletableFuture<Void> future) {
    }

    private record Pending(BiConsumer<RequestResult, List<Message>> callback,
                           CompletableFuture<Void> future) {
    }
}
