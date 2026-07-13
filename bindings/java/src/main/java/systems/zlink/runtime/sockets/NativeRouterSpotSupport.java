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
import systems.zlink.runtime.nativeapi.NativeRoutingIds;
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
        Objects.requireNonNull(flags, "flags");
        CompletableFuture<Received> future =
            RequestSubmitLoop.submitFuture(timeoutMs,
                InternalAccess.socketHandle(socket),
                "zlink-router-spot-request-progress",
                (handler, userData) -> submitRouterRequestSpot(socket,
                    destNodeRid, destSpotRid, parts, handler, userData,
                    flags.value(), RequestReplySupport.toTimeoutInt(timeoutMs)));
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
        Objects.requireNonNull(flags, "flags");
        return RequestSubmitLoop.submitCallback(timeoutMs,
            InternalAccess.socketHandle(socket),
            "zlink-router-spot-request-progress", flags, callback::accept,
            (handler, userData) -> submitRouterRequestSpot(socket, destNodeRid,
                destSpotRid, parts, handler, userData, flags.value(),
                RequestReplySupport.toTimeoutInt(timeoutMs)));
    }

    public static void replyToSpot(RouterSocket socket,
                                   RoutingId destNodeRid,
                                   RoutingId destSpotRid,
                                   long requestSeq,
                                   List<Message> parts) {
        Objects.requireNonNull(socket, "socket");
        Objects.requireNonNull(destNodeRid, "destNodeRid");
        Objects.requireNonNull(destSpotRid, "destSpotRid");
        submitRouterReplySpot(socket, destNodeRid, destSpotRid, requestSeq, parts);
    }

    private static void submitRouterSendSpot(RouterSocket socket,
                                             RoutingId destNodeRid,
                                             RoutingId destSpotRid,
                                             List<Message> payload,
                                             int flags) {
        RequestSubmitLoop.submitErrnoParts(payload,
            (part, partFlag) -> routerSendSpotPartOnce(socket, destNodeRid,
                destSpotRid, part, flags, partFlag),
            () -> submitFailure("zlink_router_send_spot_part"));
    }

    private static void submitRouterRequestSpot(RouterSocket socket,
                                                RoutingId destNodeRid,
                                                RoutingId destSpotRid,
                                                List<Message> payload,
                                                MemorySegment handler,
                                                MemorySegment userData,
                                                int flags,
                                                int timeoutMs) {
        RequestSubmitLoop.submitErrnoParts(payload,
            (part, partFlag) -> {
                boolean last = partFlag == Native.PART_FINAL;
                return routerRequestSpotPartOnce(socket, destNodeRid,
                    destSpotRid, part, last ? handler : MemorySegment.NULL,
                    last ? userData : MemorySegment.NULL, flags, partFlag,
                    timeoutMs);
            },
            () -> submitFailure("zlink_router_request_spot_part"));
    }

    private static void submitRouterReplySpot(RouterSocket socket,
                                              RoutingId destNodeRid,
                                              RoutingId destSpotRid,
                                              long requestSeq,
                                              List<Message> payload) {
        RequestSubmitLoop.submitErrnoParts(payload,
            (part, partFlag) -> routerReplySpotPartOnce(socket, destNodeRid,
                destSpotRid, requestSeq, part, partFlag),
            () -> submitFailure("zlink_router_reply_spot_part"));
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
        throw ZlinkException.fromLastError(
            systems.zlink.contracts.errors.ErrorCategory.SUBMIT);
    }

    private static MemorySegment nativeRoutingId(Arena arena,
                                                 RoutingId routingId) {
        return NativeRoutingIds.allocate(arena, routingId);
    }
}
