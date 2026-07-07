/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.spot;

import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.time.Duration;
import java.util.Arrays;
import java.util.List;
import java.util.Objects;
import java.util.Set;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentMap;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.atomic.AtomicLong;
import java.util.function.BiConsumer;
import systems.zlink.contracts.errors.ZlinkRequestException;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.sockets.RequestResult;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.sockets.SubmitResult;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.nativeapi.Native;
import systems.zlink.runtime.nativeapi.NativeErrno;
import systems.zlink.runtime.nativeapi.NativeHelpers;
import systems.zlink.runtime.nativeapi.NativeLayouts;
import systems.zlink.runtime.nativeapi.NativeMessage;
import systems.zlink.runtime.nativeapi.NativeSubmitErrors;
import systems.zlink.runtime.nativeapi.RequestProgressPump;
import systems.zlink.runtime.nativeapi.RequestReplySupport;

final class SpotRequestPlane {
        private static final Linker LINKER = Linker.nativeLinker();
    private static final FunctionDescriptor FD_REPLY_CALLBACK =
        FunctionDescriptor.ofVoid(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
            ValueLayout.JAVA_LONG, ValueLayout.ADDRESS);
    private static final Arena REQUEST_CALLBACK_ARENA = Arena.ofShared();
    private static final MemorySegment REPLY_CALLBACK;
    private static final AtomicLong NEXT_REQUEST_ID = new AtomicLong(1L);
    private static final ConcurrentMap<Long, CompletableFuture<Received>> PENDING =
        new ConcurrentHashMap<>();
    private static final ConcurrentMap<Long, DirectReplyState> DIRECT_PENDING =
        new ConcurrentHashMap<>();

    static {
        try {
            REPLY_CALLBACK = LINKER.upcallStub(MethodHandles.lookup().findStatic(
                SpotRequestPlane.class, "handleReplyCallback",
                MethodType.methodType(void.class, int.class,
                    MemorySegment.class, long.class, MemorySegment.class)),
                FD_REPLY_CALLBACK, REQUEST_CALLBACK_ARENA);
        } catch (ReflectiveOperationException ex) {
            throw new ExceptionInInitializerError(ex);
        }
    }

    private final NativeSpot spot;
    private final Set<Long> ownedRequests = ConcurrentHashMap.newKeySet();
    private final Set<Long> ownedCallbacks = ConcurrentHashMap.newKeySet();

    SpotRequestPlane(NativeSpot spot) {
        this.spot = spot;
    }

    CompletableFuture<List<Message>> requestToChannel(
        String channelName, List<Message> parts, Duration timeout,
        SendFlags flags) {
        long timeoutMs = RequestReplySupport.timeoutMillis(timeout);
        long requestId = NEXT_REQUEST_ID.getAndIncrement();
        CompletableFuture<Received> future = registerPending(requestId,
            timeoutMs);
        ownedRequests.add(requestId);
        future.whenComplete((ignored, error) -> ownedRequests.remove(requestId));
        RequestProgressPump.trackSpotRequest(future, spot.handle(),
            "zlink-spot-request-progress");
        try {
            submitRequestChannel(channelName, parts, REPLY_CALLBACK,
                MemorySegment.ofAddress(requestId),
                Objects.requireNonNull(flags, "flags").value(),
                RequestReplySupport.toTimeoutInt(timeoutMs));
        } catch (RuntimeException ex) {
            PENDING.remove(requestId);
            future.cancel(false);
            throw ex;
        }
        return future.thenApply(InternalAccess::receivedTakeParts);
    }

    boolean requestToChannelCallback(
        String channelName, List<Message> parts,
        BiConsumer<RequestResult, List<Message>> callback, Duration timeout,
        SendFlags flags) {
        Objects.requireNonNull(callback, "callback");
        long timeoutMs = RequestReplySupport.timeoutMillis(timeout);
        long requestId = NEXT_REQUEST_ID.getAndIncrement();
        DirectReplyState state = registerDirectPending(requestId, timeoutMs, callback);
        ownedCallbacks.add(requestId);
        state.progress.whenComplete((ignored, error) -> ownedCallbacks.remove(requestId));
        RequestProgressPump.trackSpotRequest(state.progress, spot.handle(),
            "zlink-spot-request-progress");
        try {
            submitRequestChannel(channelName, parts, REPLY_CALLBACK,
                MemorySegment.ofAddress(requestId),
                Objects.requireNonNull(flags, "flags").value(),
                RequestReplySupport.toTimeoutInt(timeoutMs));
            return true;
        } catch (ZlinkSubmitException ex) {
            removeDirectPending(requestId);
            if (flags == SendFlags.DONT_WAIT
                && ex.getResult() == SubmitResult.BACKPRESSURED) {
                return false;
            }
            throw ex;
        } catch (RuntimeException ex) {
            removeDirectPending(requestId);
            throw ex;
        }
    }

    private void submitRequestChannel(String channelName, List<Message> payload,
                                      MemorySegment handler,
                                      MemorySegment userData, int flags,
                                      int timeoutMs) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment service = NativeHelpers.toCString(arena,
                NativeSpot.requireChannelName(channelName));
            for (int i = 0; i < payload.size(); i++) {
                boolean last = i + 1 >= payload.size();
                int partFlag = last ? Native.PART_FINAL : Native.PART_MORE;
                while (true) {
                    int rc = requestChannelPartOnce(service, payload.get(i),
                        handler, userData, flags, partFlag,
                        timeoutMs, arena);
                    if (rc == 0)
                        break;
                    int errno = Native.errno();
                    if (errno == NativeErrno.EINTR)
                        continue;
                    throw submitFailure("zlink_spot_request_channel_part");
                }
            }
        }
    }

    private int requestChannelPartOnce(MemorySegment service, Message part,
                                       MemorySegment handler,
                                       MemorySegment userData, int flags,
                                       int partFlag, int timeoutMs,
                                       Arena arena) {
        MemorySegment nativeMsg = arena.allocate(NativeLayouts.MESSAGE_LAYOUT);
        InternalAccess.messageCopyTo(part, nativeMsg);
        return Native.spotRequestChannelPart(spot.handle(), service, nativeMsg,
            handler, userData, flags, partFlag, timeoutMs);
    }

    private RuntimeException submitFailure(String apiName) {
        int errno = Native.errno();
        ZlinkSubmitException submit =
            NativeSubmitErrors.submitExceptionOrNull(errno);
        if (submit != null) {
            return submit;
        }
        return InternalAccess.zlinkExceptionFromErrno(apiName, errno);
    }

    private static CompletableFuture<Received> registerPending(long requestId,
                                                               long timeoutMs) {
        CompletableFuture<Received> future = new CompletableFuture<>();
        PENDING.put(requestId, future);
        RequestReplySupport.armTimeout(PENDING, requestId, future, timeoutMs);
        return future;
    }

    private static DirectReplyState registerDirectPending(
        long requestId,
        long timeoutMs,
        BiConsumer<RequestResult, List<Message>> callback) {
        DirectReplyState state = new DirectReplyState(callback);
        DIRECT_PENDING.put(requestId, state);
        state.timeout = RequestReplySupport.scheduleRequestTimeout(() -> {
            DirectReplyState removed = DIRECT_PENDING.remove(requestId);
            if (removed != null) {
                removed.complete(RequestResult.TIMED_OUT, List.of());
            }
        }, timeoutMs);
        return state;
    }

    private static void removeDirectPending(long requestId) {
        DirectReplyState state = DIRECT_PENDING.remove(requestId);
        if (state != null) {
            state.cancelTimeout();
            state.progress.cancel(false);
        }
    }

    private static void handleReplyCallback(int result, MemorySegment parts,
                                            long partCount,
                                            MemorySegment userdata) {
        long requestId = userdata.address();
        DirectReplyState directState = DIRECT_PENDING.remove(requestId);
        if (directState != null) {
            completeDirect(directState, result, parts, partCount);
            return;
        }

        CompletableFuture<Received> future = PENDING.remove(requestId);
        try {
            if (result != RequestResult.OK.value()) {
                if (future != null) {
                    future.completeExceptionally(new ZlinkRequestException(
                        RequestResult.fromValue(result), result));
                }
                return;
            }
            Message[] frames = InternalAccess.messageFromOwnedMessageVectorShared(
                parts, partCount);
            Received received = InternalAccess.received(null, null, frames, 0L,
                false, null);
            if (future == null || !future.complete(received)) {
                received.close();
            }
        } catch (Throwable error) {
            if (future != null) {
                future.completeExceptionally(error);
            }
        } finally {
            NativeMessage.multipartClose(parts, partCount);
        }
    }

    private static void completeDirect(DirectReplyState state, int result,
                                       MemorySegment parts, long partCount) {
        state.cancelTimeout();
        try {
            if (result != RequestResult.OK.value()) {
                state.complete(RequestResult.fromValue(result), List.of());
                return;
            }
            try {
                Message[] frames = InternalAccess.messageFromOwnedMessageVectorShared(
                    parts, partCount);
                state.complete(RequestResult.OK,
                    frames.length == 0 ? List.of() : Arrays.asList(frames),
                    frames);
            } catch (Throwable error) {
                state.complete(RequestResult.PROTOCOL_ERROR, List.of());
            }
        } finally {
            NativeMessage.multipartClose(parts, partCount);
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
            DirectReplyState state = DIRECT_PENDING.remove(requestId);
            ownedCallbacks.remove(requestId);
            if (state != null) {
                state.cancelTimeout();
                state.complete(RequestResult.TERMINATED, List.of());
            }
        }
    }

    private static final class DirectReplyState {
        private final BiConsumer<RequestResult, List<Message>> callback;
        private final CompletableFuture<Void> progress = new CompletableFuture<>();
        private volatile ScheduledFuture<?> timeout;

        private DirectReplyState(BiConsumer<RequestResult, List<Message>> callback) {
            this.callback = callback;
        }

        private void cancelTimeout() {
            ScheduledFuture<?> scheduled = timeout;
            if (scheduled != null) {
                scheduled.cancel(false);
            }
        }

        private void complete(RequestResult result, List<Message> parts) {
            complete(result, parts, null);
        }

        private void complete(RequestResult result, List<Message> parts,
                              Message[] closeOnCallbackFailure) {
            // HOT PATH: spot callback requests deliver the reply directly.
            // Avoid the Received/future path used by awaitable requests.
            try {
                callback.accept(result, parts);
                progress.complete(null);
            } catch (Throwable error) {
                if (closeOnCallbackFailure != null) {
                    Message.closeAll(closeOnCallbackFailure);
                }
                progress.completeExceptionally(error);
            }
        }
    }
}
