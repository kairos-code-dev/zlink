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
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.contracts.sockets.RequestResult;
import systems.zlink.contracts.sockets.SubmitResult;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.nativeapi.RequestProgressPump;
import systems.zlink.runtime.nativeapi.RequestReplySupport;
import systems.zlink.runtime.nativeapi.RoutedRequestSupport;

final class SpotRoutedRequestPlane {
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
        long requestId = RoutedRequestSupport.nextRequestId();
        CompletableFuture<Received> future =
            RoutedRequestSupport.registerPending(requestId, timeoutMs);
        ownedRequests.add(requestId);
        future.whenComplete((ignored, error) -> ownedRequests.remove(requestId));
        RequestProgressPump.trackSpotRequest(future, handle(),
            "zlink-spot-routed-request-progress");
        try (Arena arena = Arena.ofConfined()) {
            int rc = request.invoke(arena, parts, requestId,
                RoutedRequestSupport.replyCallback(), timeoutMs);
            if (rc != 0) {
                future.cancel(false);
                throw new ZlinkSubmitException(SubmitResult.fromValue(rc));
            }
        } catch (RuntimeException ex) {
            RoutedRequestSupport.removePending(requestId);
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
        long requestId = RoutedRequestSupport.nextRequestId();
        CompletableFuture<Void> progress =
            RoutedRequestSupport.registerDirectPending(requestId, timeoutMs,
                callback);
        ownedCallbacks.add(requestId);
        progress.whenComplete((ignored, error) -> ownedCallbacks.remove(requestId));
        RequestProgressPump.trackSpotRequest(progress, handle(),
            "zlink-spot-routed-request-progress");
        try (Arena arena = Arena.ofConfined()) {
            int rc = request.invoke(arena, parts, requestId,
                RoutedRequestSupport.replyCallback(), timeoutMs);
            if (rc != 0) {
                RoutedRequestSupport.removeDirectPending(requestId);
                throw new ZlinkSubmitException(SubmitResult.fromValue(rc));
            }
        } catch (RuntimeException ex) {
            RoutedRequestSupport.removeDirectPending(requestId);
            throw ex;
        }
    }

    private MemorySegment handle() {
        return InternalAccess.spotHandle(spot);
    }

    void close() {
        for (Long requestId : List.copyOf(ownedRequests)) {
            ownedRequests.remove(requestId);
            RoutedRequestSupport.completePendingExceptionally(requestId,
                new ZlinkRequestException(RequestResult.TERMINATED));
        }
        for (Long requestId : List.copyOf(ownedCallbacks)) {
            ownedCallbacks.remove(requestId);
            RoutedRequestSupport.completeDirectPending(requestId,
                RequestResult.TERMINATED);
        }
    }

    @FunctionalInterface
    interface NativeRequest {
        int invoke(Arena arena, List<Message> payload, long requestId,
                   MemorySegment replyCallback, long timeoutMs);
    }
}
