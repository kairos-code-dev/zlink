/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.sockets;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.errors.ZlinkException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.sockets.RequestResult;
import systems.zlink.contracts.sockets.RouterSocket;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.sockets.SubmitResult;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.nativeapi.Native;
import systems.zlink.runtime.nativeapi.NativeErrno;
import systems.zlink.runtime.nativeapi.NativeLayouts;
import systems.zlink.runtime.nativeapi.NativeSubmitErrors;
import systems.zlink.runtime.nativeapi.RequestReplySupport;
import systems.zlink.runtime.nativeapi.RoutedRequestSupport;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.time.Duration;
import java.util.List;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.function.BiConsumer;

final class NativeRouterSpotSupport {

    private NativeRouterSpotSupport() {
    }

    public static boolean sendToSpot(RouterSocket socket,
                                     RoutingId destNodeRid,
                                     RoutingId destSpotRid,
                                     List<Message> parts,
                                     SendFlags flags) {
        Objects.requireNonNull(socket, "socket");
        Objects.requireNonNull(destNodeRid, "destNodeRid");
        Objects.requireNonNull(destSpotRid, "destSpotRid");
        Objects.requireNonNull(flags, "flags");
        try {
            submitRouterSendSpot(socket, destNodeRid, destSpotRid, parts,
                flags.value());
            return true;
        } catch (ZlinkSubmitException ex) {
            if (flags == SendFlags.DONT_WAIT
                && ex.getResult() == SubmitResult.BACKPRESSURED) {
                return false;
            }
            throw ex;
        }
    }

    public static CompletableFuture<List<Message>> requestToSpot(
            RouterSocket socket,
            RoutingId destNodeRid,
            RoutingId destSpotRid,
            List<Message> parts,
            Duration timeout,
            SendFlags flags) {
        long timeoutMs = RequestReplySupport.timeoutMillis(timeout);
        long requestId = RoutedRequestSupport.nextRequestId();
        CompletableFuture<Received> future = RoutedRequestSupport.registerPending(
          requestId, timeoutMs);
        try {
            submitRouterRequestSpot(socket, destNodeRid, destSpotRid, parts,
                RoutedRequestSupport.replyCallback(),
                RoutedRequestSupport.userData(requestId),
                Objects.requireNonNull(flags, "flags").value(),
                RoutedRequestSupport.toTimeoutInt(timeoutMs));
            RequestReplySupport.startSocketRequestProgress(future,
                InternalAccess.socketHandle(socket),
                "zlink-router-spot-request-progress");
        } catch (RuntimeException ex) {
            RoutedRequestSupport.removePending(requestId);
            future.cancel(false);
            throw ex;
        }
        return future.thenApply(RequestReplySupport::takeReceivedParts);
    }

    public static boolean requestToSpot(
            RouterSocket socket,
            RoutingId destNodeRid,
            RoutingId destSpotRid,
            List<Message> parts,
            BiConsumer<RequestResult, List<Message>> callback,
            SendFlags flags,
            Duration timeout) {
        Objects.requireNonNull(callback, "callback");
        long timeoutMs = RequestReplySupport.timeoutMillis(timeout);
        long requestId = RoutedRequestSupport.nextRequestId();
        CompletableFuture<Received> future = RoutedRequestSupport.registerPending(
          requestId, timeoutMs);
        try {
            submitRouterRequestSpot(socket, destNodeRid, destSpotRid, parts,
                RoutedRequestSupport.replyCallback(),
                RoutedRequestSupport.userData(requestId),
                Objects.requireNonNull(flags, "flags").value(),
                RoutedRequestSupport.toTimeoutInt(timeoutMs));
            RequestReplySupport.startSocketRequestProgress(future,
                InternalAccess.socketHandle(socket),
                "zlink-router-spot-request-progress");
        } catch (RuntimeException ex) {
            RoutedRequestSupport.removePending(requestId);
            future.cancel(false);
            if (ex instanceof ZlinkSubmitException submitException
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

    public static void replyToSpot(RouterSocket socket,
                                   RoutingId destNodeRid,
                                   RoutingId destSpotRid,
                                   long requestSeq,
                                   List<Message> parts,
                                   SendFlags flags) {
        Objects.requireNonNull(socket, "socket");
        Objects.requireNonNull(destNodeRid, "destNodeRid");
        Objects.requireNonNull(destSpotRid, "destSpotRid");
        RequestReplySupport.requireReplyFlagsSupported(flags);
        submitRouterReplySpot(socket, destNodeRid, destSpotRid, requestSeq, parts);
    }

    private static void submitRouterSendSpot(RouterSocket socket,
                                             RoutingId destNodeRid,
                                             RoutingId destSpotRid,
                                             List<Message> payload,
                                             int flags) {
        for (int i = 0; i < payload.size(); i++) {
            int partFlag = i + 1 < payload.size()
                ? Native.PART_MORE : Native.PART_FINAL;
            while (true) {
                int rc = routerSendSpotPartOnce(socket, destNodeRid, destSpotRid,
                    payload.get(i), flags, partFlag);
                if (rc == 0)
                    break;
                int errno = Native.errno();
                if (errno == NativeErrno.EINTR)
                    continue;
                throw submitFailure("zlink_router_send_spot_part");
            }
        }
    }

    private static void submitRouterRequestSpot(RouterSocket socket,
                                                RoutingId destNodeRid,
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
                int rc = routerRequestSpotPartOnce(socket, destNodeRid,
                    destSpotRid, payload.get(i), handler, userData,
                    flags, partFlag, timeoutMs);
                if (rc == 0)
                    break;
                int errno = Native.errno();
                if (errno == NativeErrno.EINTR)
                    continue;
                throw submitFailure("zlink_router_request_spot_part");
            }
        }
    }

    private static void submitRouterReplySpot(RouterSocket socket,
                                              RoutingId destNodeRid,
                                              RoutingId destSpotRid,
                                              long requestSeq,
                                              List<Message> payload) {
        for (int i = 0; i < payload.size(); i++) {
            int partFlag = i + 1 < payload.size()
                ? Native.PART_MORE : Native.PART_FINAL;
            while (true) {
                int rc = routerReplySpotPartOnce(socket, destNodeRid,
                    destSpotRid, requestSeq, payload.get(i), partFlag);
                if (rc == 0)
                    break;
                int errno = Native.errno();
                if (errno == NativeErrno.EINTR)
                    continue;
                throw submitFailure("zlink_router_reply_spot_part");
            }
        }
    }

    private static int routerSendSpotPartOnce(RouterSocket socket,
                                              RoutingId destNodeRid,
                                              RoutingId destSpotRid,
                                              Message part,
                                              int flags,
                                              int partFlag) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nodeRid = nativeRoutingId(arena, destNodeRid);
            MemorySegment spotRid = nativeRoutingId(arena, destSpotRid);
            MemorySegment nativeMsg = arena.allocate(NativeLayouts.MESSAGE_LAYOUT);
            InternalAccess.messageCopyTo(part, nativeMsg);
            return Native.routerSendSpotPart(InternalAccess.socketHandle(socket),
                nodeRid, spotRid, nativeMsg, flags, partFlag);
        }
    }

    private static int routerRequestSpotPartOnce(RouterSocket socket,
                                                 RoutingId destNodeRid,
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
            MemorySegment nativeMsg = arena.allocate(NativeLayouts.MESSAGE_LAYOUT);
            InternalAccess.messageCopyTo(part, nativeMsg);
            return Native.routerRequestSpotPart(InternalAccess.socketHandle(socket),
                nodeRid, spotRid, nativeMsg, handler, userData, flags,
                partFlag, timeoutMs);
        }
    }

    private static int routerReplySpotPartOnce(RouterSocket socket,
                                               RoutingId destNodeRid,
                                               RoutingId destSpotRid,
                                               long requestSeq,
                                               Message part,
                                               int partFlag) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nodeRid = nativeRoutingId(arena, destNodeRid);
            MemorySegment spotRid = nativeRoutingId(arena, destSpotRid);
            MemorySegment nativeMsg = arena.allocate(NativeLayouts.MESSAGE_LAYOUT);
            InternalAccess.messageCopyTo(part, nativeMsg);
            return Native.routerReplySpotPart(InternalAccess.socketHandle(socket),
                nodeRid, spotRid, requestSeq, nativeMsg, partFlag);
        }
    }

    private static ZlinkSubmitException submitFailure(String apiName) {
        int errno = Native.errno();
        ZlinkSubmitException submit = NativeSubmitErrors.submitExceptionOrNull(errno);
        if (submit != null)
            return submit;
        throw ZlinkException.fromLastError(apiName);
    }

    private static MemorySegment nativeRoutingId(Arena arena,
                                                 RoutingId routingId) {
        byte[] value = InternalAccess.routingIdTrustedBytes(routingId);
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
