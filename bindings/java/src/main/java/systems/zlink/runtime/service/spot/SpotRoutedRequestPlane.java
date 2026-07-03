/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.spot;

import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.time.Duration;
import java.util.List;
import java.util.Objects;
import java.util.Set;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentMap;
import java.util.concurrent.atomic.AtomicLong;
import java.util.function.BiConsumer;
import systems.zlink.contracts.errors.ZlinkRequestException;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.contracts.sockets.RequestResult;
import systems.zlink.contracts.sockets.SubmitResult;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.nativeapi.NativeMessage;
import systems.zlink.runtime.nativeapi.RequestProgressPump;
import systems.zlink.runtime.nativeapi.RequestReplySupport;

final class SpotRoutedRequestPlane {
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
    private static final ConcurrentMap<Long, PendingCallback> PENDING_CALLBACKS =
      new ConcurrentHashMap<>();

    private final Spot spot;
    private final Set<Long> ownedRequests = ConcurrentHashMap.newKeySet();
    private final Set<Long> ownedCallbacks = ConcurrentHashMap.newKeySet();

    SpotRoutedRequestPlane(Spot spot) {
        this.spot = Objects.requireNonNull(spot, "spot");
    }

    CompletableFuture<List<Message>> request(List<Message> parts,
                                             Duration timeout,
                                             NativeRequest request) {
        long timeoutMs = RequestReplySupport.timeoutMillis(timeout);
        long requestId = NEXT_REQUEST_ID.getAndIncrement();
        CompletableFuture<Received> future = registerPending(requestId, timeoutMs);
        ownedRequests.add(requestId);
        future.whenComplete((ignored, error) -> ownedRequests.remove(requestId));
        RequestProgressPump.trackSpotRequest(future, handle(),
            "zlink-spot-routed-request-progress");
        try (Arena arena = Arena.ofConfined()) {
            int rc = request.invoke(arena, parts, requestId, REPLY_CALLBACK,
                timeoutMs);
            if (rc != 0) {
                future.cancel(false);
                throw new ZlinkSubmitException(SubmitResult.fromValue(rc));
            }
        } catch (RuntimeException ex) {
            PENDING.remove(requestId);
            future.cancel(false);
            throw ex;
        }
        return future.thenApply(InternalAccess::receivedTakeParts);
    }

    void requestCallback(List<Message> parts,
                         BiConsumer<RequestResult, List<Message>> callback,
                         Duration timeout,
                         NativeRequest request) {
        Objects.requireNonNull(callback, "callback");
        long timeoutMs = RequestReplySupport.timeoutMillis(timeout);
        long requestId = NEXT_REQUEST_ID.getAndIncrement();
        PendingCallback pending = registerPendingCallback(requestId, callback);
        ownedCallbacks.add(requestId);
        pending.progress.whenComplete((ignored, error) -> ownedCallbacks.remove(requestId));
        RequestProgressPump.trackSpotRequest(pending.progress, handle(),
            "zlink-spot-routed-request-progress");
        try (Arena arena = Arena.ofConfined()) {
            int rc = request.invoke(arena, parts, requestId, REPLY_CALLBACK,
                timeoutMs);
            if (rc != 0) {
                PENDING_CALLBACKS.remove(requestId, pending);
                pending.cancel();
                throw new ZlinkSubmitException(SubmitResult.fromValue(rc));
            }
        } catch (RuntimeException ex) {
            PENDING_CALLBACKS.remove(requestId, pending);
            pending.cancel();
            throw ex;
        }
    }

    private MemorySegment handle() {
        return InternalAccess.spotHandle(spot);
    }

    private static CompletableFuture<Received> registerPending(long requestId,
                                                               long timeoutMs) {
        CompletableFuture<Received> future = new CompletableFuture<>();
        PENDING.put(requestId, future);
        RequestReplySupport.armTimeout(PENDING, requestId, future, timeoutMs);
        return future;
    }

    private static PendingCallback registerPendingCallback(
      long requestId,
      BiConsumer<RequestResult, List<Message>> callback) {
        PendingCallback pending = new PendingCallback(callback);
        PENDING_CALLBACKS.put(requestId, pending);
        return pending;
    }

    private static void handleReplyCallback(int result, MemorySegment parts,
                                            long partCount,
                                            MemorySegment userdata) {
        long requestId = userdata.address();
        CompletableFuture<Received> future = PENDING.remove(requestId);
        PendingCallback pending = PENDING_CALLBACKS.remove(requestId);
        try {
            if (result != RequestResult.OK.value()) {
                if (future != null) {
                    future.completeExceptionally(new ZlinkRequestException(
                        RequestResult.fromValue(result), result));
                }
                if (pending != null) {
                    pending.complete(RequestResult.fromValue(result), List.of(),
                        null);
                }
                return;
            }
            Message[] frames = InternalAccess.messageFromOwnedMessageVectorShared(
              parts, partCount);
            if (pending != null) {
                pending.complete(RequestResult.OK, java.util.Arrays.asList(frames),
                    frames);
            } else {
                Received received = InternalAccess.received(null, null, frames,
                    0L, false, null);
                if (future == null || !future.complete(received)) {
                    received.close();
                }
            }
        } catch (Throwable error) {
            if (future != null) {
                future.completeExceptionally(error);
            }
            if (pending != null) {
                pending.completeExceptionally(error);
            }
        } finally {
            NativeMessage.multipartClose(parts, partCount);
        }
    }

    private static MethodHandle callbackHandle() {
        try {
            return MethodHandles.lookup().findStatic(SpotRoutedRequestPlane.class,
              "handleReplyCallback", MethodType.methodType(void.class,
                int.class, MemorySegment.class, long.class, MemorySegment.class));
        } catch (ReflectiveOperationException ex) {
            throw new ExceptionInInitializerError(ex);
        }
    }

    void close() {
        for (Long requestId : List.copyOf(ownedRequests)) {
            CompletableFuture<Received> future = PENDING.remove(requestId);
            ownedRequests.remove(requestId);
            if (future != null) {
                future.completeExceptionally(
                    new ZlinkRequestException(RequestResult.TERMINATED));
            }
        }
        for (Long requestId : List.copyOf(ownedCallbacks)) {
            PendingCallback pending = PENDING_CALLBACKS.remove(requestId);
            ownedCallbacks.remove(requestId);
            if (pending != null) {
                pending.complete(RequestResult.TERMINATED, List.of(), null);
            }
        }
    }

    private static final class PendingCallback {
        private final BiConsumer<RequestResult, List<Message>> callback;
        private final CompletableFuture<Void> progress = new CompletableFuture<>();

        private PendingCallback(
          BiConsumer<RequestResult, List<Message>> callback) {
            this.callback = callback;
        }

        private void cancel() {
            progress.complete(null);
        }

        private void complete(RequestResult result, List<Message> reply,
                              Message[] closeOnCallbackFailure) {
            try {
                callback.accept(result, reply);
                progress.complete(null);
            } catch (Throwable error) {
                if (closeOnCallbackFailure != null) {
                    Message.closeAll(closeOnCallbackFailure);
                }
                progress.completeExceptionally(error);
            }
        }

        private void completeExceptionally(Throwable error) {
            progress.completeExceptionally(error);
        }
    }

    @FunctionalInterface
    interface NativeRequest {
        int invoke(Arena arena, List<Message> payload, long requestId,
                   MemorySegment replyCallback, long timeoutMs);
    }
}
