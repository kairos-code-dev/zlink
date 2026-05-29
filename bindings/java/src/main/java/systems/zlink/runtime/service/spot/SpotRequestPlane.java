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
import java.util.List;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentMap;
import java.util.concurrent.atomic.AtomicLong;
import systems.zlink.contracts.errors.ZlinkRequestException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.sockets.RequestResult;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.nativeapi.Native;
import systems.zlink.runtime.nativeapi.NativeHelpers;
import systems.zlink.runtime.nativeapi.NativeLayouts;
import systems.zlink.runtime.nativeapi.NativeMessage;
import systems.zlink.runtime.nativeapi.RequestProgressPump;
import systems.zlink.runtime.nativeapi.RequestReplySupport;

final class SpotRequestPlane {
    private static final int ERRNO_EINTR = 4;
    private static final Linker LINKER = Linker.nativeLinker();
    private static final FunctionDescriptor FD_REPLY_CALLBACK =
        FunctionDescriptor.ofVoid(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
            ValueLayout.JAVA_LONG, ValueLayout.ADDRESS);
    private static final Arena REQUEST_CALLBACK_ARENA = Arena.ofShared();
    private static final MemorySegment REPLY_CALLBACK;
    private static final AtomicLong NEXT_REQUEST_ID = new AtomicLong(1L);
    private static final ConcurrentMap<Long, CompletableFuture<Received>> PENDING =
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
                        last ? handler : MemorySegment.NULL,
                        last ? userData : MemorySegment.NULL, flags, partFlag,
                        last ? timeoutMs : 0, arena);
                    if (rc == 0)
                        break;
                    int errno = Native.errno();
                    if (errno == ERRNO_EINTR)
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
        return InternalAccess.zlinkExceptionFromLastError(apiName);
    }

    private static CompletableFuture<Received> registerPending(long requestId,
                                                               long timeoutMs) {
        CompletableFuture<Received> future = new CompletableFuture<>();
        PENDING.put(requestId, future);
        RequestReplySupport.armTimeout(PENDING, requestId, future, timeoutMs);
        return future;
    }

    private static void handleReplyCallback(int result, MemorySegment parts,
                                            long partCount,
                                            MemorySegment userdata) {
        long requestId = userdata.address();
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
}
