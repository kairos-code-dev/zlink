/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.Native;
import dev.kairoscode.zlink.internal.NativeLayouts;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.time.Duration;
import java.util.List;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.function.BiConsumer;

final class RouterSpotSupport {
    private final RouterSocket socket;

    RouterSpotSupport(RouterSocket socket) {
        this.socket = Objects.requireNonNull(socket, "socket");
    }

    void sendToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                    List<Message> parts, SendFlags flags) {
        Objects.requireNonNull(destNodeRid, "destNodeRid");
        Objects.requireNonNull(destSpotRid, "destSpotRid");
        Objects.requireNonNull(flags, "flags");
        List<Message> payload = RequestReplySupport.clonePayload(parts);
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.routerSendSpot(socket.handle(),
              nativeRoutingId(arena, destNodeRid),
              nativeRoutingId(arena, destSpotRid),
              RoutedRequestSupport.movePayloadToNative(arena, payload),
              payload.size(), flags.value());
            if (rc != 0) {
                throw new SubmitException(SubmitResult.fromValue(rc));
            }
        } finally {
            RequestReplySupport.closeAll(payload);
        }
    }

    CompletableFuture<List<Message>> requestToSpot(RoutingId destNodeRid,
                                                   RoutingId destSpotRid,
                                                   List<Message> parts,
                                                   Duration timeout) {
        long timeoutMs = RequestReplySupport.timeoutMillis(timeout);
        long requestId = RoutedRequestSupport.nextRequestId();
        List<Message> payload = RequestReplySupport.clonePayload(parts);
        CompletableFuture<Received> future = RoutedRequestSupport.registerPending(
          requestId, timeoutMs);
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.routerRequestSpot(socket.handle(),
              nativeRoutingId(arena, Objects.requireNonNull(destNodeRid,
                "destNodeRid")),
              nativeRoutingId(arena, Objects.requireNonNull(destSpotRid,
                "destSpotRid")),
              RoutedRequestSupport.movePayloadToNative(arena, payload),
              payload.size(), RoutedRequestSupport.replyCallback(),
              RoutedRequestSupport.userData(requestId), SendFlags.NONE.value(),
              RoutedRequestSupport.toTimeoutInt(timeoutMs));
            if (rc != 0) {
                RequestReplySupport.closeAll(payload);
                throw new SubmitException(SubmitResult.fromValue(rc));
            }
        } catch (RuntimeException ex) {
            RequestReplySupport.closeAll(payload);
            RoutedRequestSupport.removePending(requestId);
            future.cancel(false);
            throw ex;
        }
        return future.thenApply(reply -> {
            try (reply) {
                return RequestReplySupport.cloneReceived(reply).parts();
            }
        });
    }

    void requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                       List<Message> parts,
                       BiConsumer<RequestResult, List<Message>> callback,
                       SendFlags flags, Duration timeout) {
        Objects.requireNonNull(callback, "callback");
        long timeoutMs = RequestReplySupport.timeoutMillis(timeout);
        long requestId = RoutedRequestSupport.nextRequestId();
        List<Message> payload = RequestReplySupport.clonePayload(parts);
        CompletableFuture<Received> future = RoutedRequestSupport.registerPending(
          requestId, timeoutMs);
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.routerRequestSpot(socket.handle(),
              nativeRoutingId(arena, Objects.requireNonNull(destNodeRid,
                "destNodeRid")),
              nativeRoutingId(arena, Objects.requireNonNull(destSpotRid,
                "destSpotRid")),
              RoutedRequestSupport.movePayloadToNative(arena, payload),
              payload.size(), RoutedRequestSupport.replyCallback(),
              RoutedRequestSupport.userData(requestId),
              Objects.requireNonNull(flags, "flags").value(),
              RoutedRequestSupport.toTimeoutInt(timeoutMs));
            if (rc != 0) {
                RequestReplySupport.closeAll(payload);
                throw new SubmitException(SubmitResult.fromValue(rc));
            }
        } catch (RuntimeException ex) {
            RequestReplySupport.closeAll(payload);
            RoutedRequestSupport.removePending(requestId);
            future.cancel(false);
            throw ex;
        }
        future.whenComplete((reply, error) -> {
            List<Message> response = List.of();
            if (reply != null) {
                Received copy = RequestReplySupport.cloneReceived(reply);
                reply.close();
                response = copy.parts();
            }
            callback.accept(error == null ? RequestResult.OK
                : RequestReplySupport.requestResult(error), response);
        });
    }

    void replyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                     long requestSeq, List<Message> parts, SendFlags flags) {
        Objects.requireNonNull(destNodeRid, "destNodeRid");
        Objects.requireNonNull(destSpotRid, "destSpotRid");
        RequestReplySupport.requireReplyFlagsSupported(flags);
        List<Message> payload = RequestReplySupport.clonePayload(parts);
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.routerReplySpot(socket.handle(),
              nativeRoutingId(arena, destNodeRid),
              nativeRoutingId(arena, destSpotRid), requestSeq,
              RoutedRequestSupport.movePayloadToNative(arena, payload),
              payload.size());
            if (rc != 0) {
                throw new SubmitException(SubmitResult.fromValue(rc));
            }
        } finally {
            RequestReplySupport.closeAll(payload);
        }
    }

    private static MemorySegment nativeRoutingId(Arena arena,
                                                 RoutingId routingId) {
        byte[] value = routingId.toBytes();
        MemorySegment nativeRid = arena.allocate(NativeLayouts.ROUTING_ID_LAYOUT);
        nativeRid.set(ValueLayout.JAVA_BYTE, NativeLayouts.ROUTING_ID_SIZE_OFFSET,
          (byte) value.length);
        if (value.length > 0) {
            MemorySegment.copy(MemorySegment.ofArray(value), 0, nativeRid,
              NativeLayouts.ROUTING_ID_DATA_OFFSET, value.length);
        }
        return nativeRid;
    }
}
