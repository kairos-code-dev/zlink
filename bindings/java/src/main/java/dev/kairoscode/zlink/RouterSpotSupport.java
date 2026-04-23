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

    boolean sendToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                    List<Message> parts, SendFlags flags) {
        Objects.requireNonNull(destNodeRid, "destNodeRid");
        Objects.requireNonNull(destSpotRid, "destSpotRid");
        Objects.requireNonNull(flags, "flags");
        try {
            submitRouterSendSpot(destNodeRid, destSpotRid, parts,
                flags.value());
            return true;
        } catch (SubmitException ex) {
            if (flags == SendFlags.DONT_WAIT
                && ex.getResult() == SubmitResult.BACKPRESSURED) {
                return false;
            }
            throw ex;
        }
    }

    CompletableFuture<List<Message>> requestToSpot(RoutingId destNodeRid,
                                                   RoutingId destSpotRid,
                                                   List<Message> parts,
                                                   Duration timeout) {
        return requestToSpot(destNodeRid, destSpotRid, parts, timeout,
          SendFlags.NONE);
    }

    CompletableFuture<List<Message>> requestToSpot(RoutingId destNodeRid,
                                                   RoutingId destSpotRid,
                                                   List<Message> parts,
                                                   Duration timeout,
                                                   SendFlags flags) {
        long timeoutMs = RequestReplySupport.timeoutMillis(timeout);
        long requestId = RoutedRequestSupport.nextRequestId();
        CompletableFuture<Received> future = RoutedRequestSupport.registerPending(
          requestId, timeoutMs);
        try {
            submitRouterRequestSpot(destNodeRid, destSpotRid, parts,
                RoutedRequestSupport.replyCallback(),
                RoutedRequestSupport.userData(requestId),
                Objects.requireNonNull(flags, "flags").value(),
                RoutedRequestSupport.toTimeoutInt(timeoutMs));
            RequestReplySupport.startSocketRequestProgress(future,
                socket.handle(), "zlink-router-spot-request-progress");
        } catch (RuntimeException ex) {
            RoutedRequestSupport.removePending(requestId);
            future.cancel(false);
            throw ex;
        }
        return future.thenApply(RequestReplySupport::takeReceivedParts);
    }

    boolean requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                       List<Message> parts,
                       BiConsumer<RequestResult, List<Message>> callback,
                       SendFlags flags, Duration timeout) {
        Objects.requireNonNull(callback, "callback");
        long timeoutMs = RequestReplySupport.timeoutMillis(timeout);
        long requestId = RoutedRequestSupport.nextRequestId();
        CompletableFuture<Received> future = RoutedRequestSupport.registerPending(
          requestId, timeoutMs);
        try {
            submitRouterRequestSpot(destNodeRid, destSpotRid, parts,
                RoutedRequestSupport.replyCallback(),
                RoutedRequestSupport.userData(requestId),
                Objects.requireNonNull(flags, "flags").value(),
                RoutedRequestSupport.toTimeoutInt(timeoutMs));
            RequestReplySupport.startSocketRequestProgress(future,
                socket.handle(), "zlink-router-spot-request-progress");
        } catch (RuntimeException ex) {
            RoutedRequestSupport.removePending(requestId);
            future.cancel(false);
            if (ex instanceof SubmitException submitException
                && flags == SendFlags.DONT_WAIT
                && submitException.getResult() == SubmitResult.BACKPRESSURED) {
                return false;
            }
            throw ex;
        }
        future.whenComplete((reply, error) -> {
            List<Message> response = List.of();
            if (reply != null) {
                response = RequestReplySupport.takeReceivedParts(reply);
            }
            callback.accept(error == null ? RequestResult.OK
                : RequestReplySupport.requestResult(error), response);
        });
        return true;
    }

    void replyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                     long requestSeq, List<Message> parts, SendFlags flags) {
        Objects.requireNonNull(destNodeRid, "destNodeRid");
        Objects.requireNonNull(destSpotRid, "destSpotRid");
        RequestReplySupport.requireReplyFlagsSupported(flags);
        submitRouterReplySpot(destNodeRid, destSpotRid, requestSeq, parts);
    }

    private void submitRouterSendSpot(RoutingId destNodeRid,
                                      RoutingId destSpotRid,
                                      List<Message> payload,
                                      int flags) {
        for (int i = 0; i < payload.size(); i++) {
            int partFlag = i + 1 < payload.size()
                ? Native.PART_MORE : Native.PART_FINAL;
            while (true) {
                int rc = routerSendSpotPartOnce(destNodeRid, destSpotRid,
                    payload.get(i), flags, partFlag);
                if (rc == 0)
                    break;
                int errno = Native.errno();
                if (errno == Socket.ERRNO_EINTR)
                    continue;
                throw submitFailure("zlink_router_send_spot_part");
            }
        }
    }

    private void submitRouterRequestSpot(RoutingId destNodeRid,
                                         RoutingId destSpotRid,
                                         List<Message> payload,
                                         MemorySegment handler,
                                         MemorySegment userData,
                                         int flags,
                                         int timeoutMs) {
        for (int i = 0; i < payload.size(); i++) {
            int partFlag = i + 1 < payload.size()
                ? Native.PART_MORE : Native.PART_FINAL;
            while (true) {
                int rc = routerRequestSpotPartOnce(destNodeRid, destSpotRid,
                    payload.get(i), i + 1 < payload.size()
                        ? MemorySegment.NULL : handler,
                    i + 1 < payload.size() ? MemorySegment.NULL : userData,
                    flags, partFlag, i + 1 < payload.size() ? 0 : timeoutMs);
                if (rc == 0)
                    break;
                int errno = Native.errno();
                if (errno == Socket.ERRNO_EINTR)
                    continue;
                throw submitFailure("zlink_router_request_spot_part");
            }
        }
    }

    private void submitRouterReplySpot(RoutingId destNodeRid,
                                       RoutingId destSpotRid,
                                       long requestSeq,
                                       List<Message> payload) {
        for (int i = 0; i < payload.size(); i++) {
            int partFlag = i + 1 < payload.size()
                ? Native.PART_MORE : Native.PART_FINAL;
            while (true) {
                int rc = routerReplySpotPartOnce(destNodeRid, destSpotRid,
                    requestSeq, payload.get(i), partFlag);
                if (rc == 0)
                    break;
                int errno = Native.errno();
                if (errno == Socket.ERRNO_EINTR)
                    continue;
                throw submitFailure("zlink_router_reply_spot_part");
            }
        }
    }

    private int routerSendSpotPartOnce(RoutingId destNodeRid,
                                       RoutingId destSpotRid,
                                       Message part,
                                       int flags,
                                       int partFlag) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nodeRid = nativeRoutingId(arena, destNodeRid);
            MemorySegment spotRid = nativeRoutingId(arena, destSpotRid);
            MemorySegment nativeMsg = arena.allocate(NativeLayouts.MSG_LAYOUT);
            part.copyTo(nativeMsg);
            return Native.routerSendSpotPart(socket.handle(), nodeRid,
                spotRid, nativeMsg, flags, partFlag);
        }
    }

    private int routerRequestSpotPartOnce(RoutingId destNodeRid,
                                          RoutingId destSpotRid,
                                          Message part,
                                          MemorySegment handler,
                                          MemorySegment userData,
                                          int flags,
                                          int partFlag,
                                          int timeoutMs) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nodeRid = nativeRoutingId(arena, destNodeRid);
            MemorySegment spotRid = nativeRoutingId(arena, destSpotRid);
            MemorySegment nativeMsg = arena.allocate(NativeLayouts.MSG_LAYOUT);
            part.copyTo(nativeMsg);
            return Native.routerRequestSpotPart(socket.handle(), nodeRid,
                spotRid, nativeMsg, handler, userData, flags, partFlag,
                timeoutMs);
        }
    }

    private int routerReplySpotPartOnce(RoutingId destNodeRid,
                                        RoutingId destSpotRid,
                                        long requestSeq,
                                        Message part,
                                        int partFlag) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nodeRid = nativeRoutingId(arena, destNodeRid);
            MemorySegment spotRid = nativeRoutingId(arena, destSpotRid);
            MemorySegment nativeMsg = arena.allocate(NativeLayouts.MSG_LAYOUT);
            part.copyTo(nativeMsg);
            return Native.routerReplySpotPart(socket.handle(), nodeRid,
                spotRid, requestSeq, nativeMsg, partFlag);
        }
    }

    private SubmitException submitFailure(String apiName) {
        int errno = Native.errno();
        if (errno == Socket.ERRNO_EAGAIN
            || errno == Socket.ERRNO_EWOULDBLOCK_WIN) {
            return new SubmitException(SubmitResult.BACKPRESSURED, errno);
        }
        if (errno == Socket.ERRNO_ENOTCONN
            || errno == Socket.ERRNO_ENOTCONN_WIN
            || errno == Socket.ERRNO_EHOSTUNREACH
            || errno == Socket.ERRNO_EHOSTUNREACH_WIN) {
            return new SubmitException(SubmitResult.NOT_CONNECTED, errno);
        }
        throw ZlinkException.fromLastError(apiName);
    }

    private static MemorySegment nativeRoutingId(Arena arena,
                                                 RoutingId routingId) {
        byte[] value = routingId.trustedBytes();
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
