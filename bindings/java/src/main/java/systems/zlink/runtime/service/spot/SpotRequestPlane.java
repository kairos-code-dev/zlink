/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.spot;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.time.Duration;
import java.util.List;
import java.util.Objects;
import java.util.Set;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;
import java.util.function.BiConsumer;
import systems.zlink.contracts.errors.ZlinkRequestException;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.messaging.Message;
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
import systems.zlink.runtime.nativeapi.RoutedRequestSupport;

final class SpotRequestPlane {
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
        long requestId = RoutedRequestSupport.nextRequestId();
        CompletableFuture<systems.zlink.contracts.messaging.Received> future =
            RoutedRequestSupport.registerPending(requestId,
            timeoutMs);
        ownedRequests.add(requestId);
        future.whenComplete((ignored, error) -> ownedRequests.remove(requestId));
        RequestProgressPump.trackSpotRequest(future, spot.handle(),
            "zlink-spot-request-progress");
        try {
            submitRequestChannel(channelName, parts,
                RoutedRequestSupport.replyCallback(),
                RoutedRequestSupport.userData(requestId),
                Objects.requireNonNull(flags, "flags").value(),
                RequestReplySupport.toTimeoutInt(timeoutMs));
        } catch (RuntimeException ex) {
            RoutedRequestSupport.removePending(requestId);
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
        long requestId = RoutedRequestSupport.nextRequestId();
        CompletableFuture<Void> progress =
            RoutedRequestSupport.registerDirectPending(requestId, timeoutMs,
                callback);
        ownedCallbacks.add(requestId);
        progress.whenComplete((ignored, error) -> ownedCallbacks.remove(requestId));
        RequestProgressPump.trackSpotRequest(progress, spot.handle(),
            "zlink-spot-request-progress");
        try {
            submitRequestChannel(channelName, parts,
                RoutedRequestSupport.replyCallback(),
                RoutedRequestSupport.userData(requestId),
                Objects.requireNonNull(flags, "flags").value(),
                RequestReplySupport.toTimeoutInt(timeoutMs));
            return true;
        } catch (ZlinkSubmitException ex) {
            RoutedRequestSupport.removeDirectPending(requestId);
            if (flags == SendFlags.DONT_WAIT
                && ex.getResult() == SubmitResult.BACKPRESSURED) {
                return false;
            }
            throw ex;
        } catch (RuntimeException ex) {
            RoutedRequestSupport.removeDirectPending(requestId);
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
                Message part = payload.get(i);
                int rc = NativeErrno.retryWhileInterrupted(
                    () -> requestChannelPartOnce(service, part,
                        last ? handler : MemorySegment.NULL,
                        last ? userData : MemorySegment.NULL,
                        flags, partFlag,
                        timeoutMs, arena),
                    result -> result != 0);
                if (rc != 0)
                    throw submitFailure("zlink_spot_request_channel_part");
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
        return InternalAccess.zlinkExceptionFromErrno(
            systems.zlink.contracts.errors.ErrorCategory.SUBMIT, errno);
    }

    void close() {
        for (Long requestId : List.copyOf(ownedRequests)) {
            CompletableFuture<systems.zlink.contracts.messaging.Received> future =
                RoutedRequestSupport.removePending(requestId);
            ownedRequests.remove(requestId);
            if (future != null) {
                future.completeExceptionally(
                    new ZlinkRequestException(RequestResult.TERMINATED));
            }
        }
        for (Long requestId : List.copyOf(ownedCallbacks)) {
            ownedCallbacks.remove(requestId);
            RoutedRequestSupport.completeDirectPending(requestId,
                RequestResult.TERMINATED);
        }
    }
}
