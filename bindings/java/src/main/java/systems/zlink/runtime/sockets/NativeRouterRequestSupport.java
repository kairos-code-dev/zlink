/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.sockets;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.errors.SubmitException;
import systems.zlink.contracts.errors.ZlinkException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.sockets.RequestCallback;
import systems.zlink.contracts.sockets.RequestResult;
import systems.zlink.contracts.sockets.RouterSocket;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.sockets.SubmitResult;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.nativeapi.Native;
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

final class NativeRouterRequestSupport {
    private static final int ERRNO_EINTR = 4;

    private NativeRouterRequestSupport() {
    }

    public static CompletableFuture<List<Message>> requestAsync(
            RouterSocket socket,
            RoutingId routingId,
            List<Message> parts,
            SendFlags flags,
            Duration timeout) {
        return request(socket, routingId, parts, flags, timeout)
            .thenApply(RequestReplySupport::takeReceivedParts);
    }

    public static boolean requestCallback(RouterSocket socket,
                                          RoutingId routingId,
                                          List<Message> parts,
                                          RequestCallback callback,
                                          SendFlags flags,
                                          Duration timeout) {
        Objects.requireNonNull(callback, "callback");
        try {
            request(socket, routingId, parts, flags, timeout).whenComplete(
                (reply, error) -> {
                    List<Message> payload = List.of();
                    if (reply != null) {
                        payload = RequestReplySupport.takeReceivedParts(reply);
                    }
                    callback.onComplete(error == null ? RequestResult.OK
                        : RequestReplySupport.requestResult(error), payload);
                });
            return true;
        } catch (SubmitException ex) {
            if (flags == SendFlags.DONT_WAIT
                && ex.getResult() == SubmitResult.BACKPRESSURED) {
                return false;
            }
            throw ex;
        }
    }

    public static void reply(RouterSocket socket,
                             RoutingId routingId,
                             long requestSequence,
                             List<Message> parts,
                             SendFlags flags) {
        Objects.requireNonNull(socket, "socket");
        Objects.requireNonNull(routingId, "routingId");
        Objects.requireNonNull(parts, "parts");
        RequestReplySupport.requireReplyFlagsSupported(flags);
        for (int i = 0; i < parts.size(); i++) {
            int partFlag = i + 1 < parts.size()
                ? Native.PART_MORE : Native.PART_FINAL;
            while (true) {
                int rc = routerReplyPartOnce(socket, routingId,
                    requestSequence, parts.get(i), partFlag);
                if (rc == 0) {
                    break;
                }
                int errno = Native.errno();
                if (errno == ERRNO_EINTR) {
                    continue;
                }
                throw submitFailure("zlink_router_reply_part");
            }
        }
    }

    private static CompletableFuture<Received> request(
            RouterSocket socket,
            RoutingId routingId,
            List<Message> parts,
            SendFlags flags,
            Duration timeout) {
        Objects.requireNonNull(socket, "socket");
        Objects.requireNonNull(routingId, "routingId");
        Objects.requireNonNull(parts, "parts");
        long timeoutMs = RequestReplySupport.timeoutMillis(timeout);
        long requestId = RoutedRequestSupport.nextRequestId();
        CompletableFuture<Received> future =
            RoutedRequestSupport.registerPending(requestId, timeoutMs);
        try {
            submitRequest(socket, routingId, parts, timeoutMs, flags,
                RoutedRequestSupport.replyCallback(),
                RoutedRequestSupport.userData(requestId));
            RequestReplySupport.startSocketRequestProgress(future,
                InternalAccess.socketHandle(socket),
                "zlink-router-request-progress");
        } catch (RuntimeException ex) {
            RoutedRequestSupport.removePending(requestId);
            future.cancel(false);
            throw ex;
        }
        return future;
    }

    private static void submitRequest(RouterSocket socket,
                                      RoutingId routingId,
                                      List<Message> payload,
                                      long timeoutMs,
                                      SendFlags flags,
                                      MemorySegment handler,
                                      MemorySegment userData) {
        int nativeFlags = flags == null ? 0 : flags.value();
        int timeout = RoutedRequestSupport.toTimeoutInt(timeoutMs);
        for (int i = 0; i < payload.size(); i++) {
            int partFlag = i + 1 < payload.size()
                ? Native.PART_MORE : Native.PART_FINAL;
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment nativeRid = nativeRoutingId(arena, routingId);
                MemorySegment nativeMsg = arena.allocate(NativeLayouts.MSG_LAYOUT);
                InternalAccess.messageCopyTo(payload.get(i), nativeMsg);
                int rc = Native.routerRequestPart(
                    InternalAccess.socketHandle(socket), nativeRid, nativeMsg,
                    nativeFlags, partFlag, i + 1 < payload.size() ? 0 : timeout,
                    i + 1 < payload.size() ? MemorySegment.NULL : handler,
                    i + 1 < payload.size() ? MemorySegment.NULL : userData);
                if (rc != SubmitResult.OK.value()) {
                    throw new SubmitException(SubmitResult.fromValue(rc));
                }
            }
        }
    }

    private static int routerReplyPartOnce(RouterSocket socket,
                                           RoutingId routingId,
                                           long requestSequence,
                                           Message part,
                                           int partFlag) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeRid = nativeRoutingId(arena, routingId);
            MemorySegment nativeMsg = arena.allocate(NativeLayouts.MSG_LAYOUT);
            InternalAccess.messageTransferTo(part, nativeMsg);
            try {
                int rc = Native.routerReplyPart(
                    InternalAccess.socketHandle(socket), nativeRid,
                    requestSequence, nativeMsg, partFlag);
                if (rc != 0) {
                    InternalAccess.messageRestoreFromNative(part, nativeMsg,
                        false, null);
                }
                return rc;
            } catch (RuntimeException ex) {
                InternalAccess.messageRestoreFromNative(part, nativeMsg, false,
                    null);
                throw ex;
            }
        }
    }

    private static MemorySegment nativeRoutingId(Arena arena,
                                                 RoutingId routingId) {
        byte[] value = InternalAccess.routingIdTrustedBytes(routingId);
        MemorySegment nativeRid = arena.allocate(NativeLayouts.ROUTING_ID_LAYOUT);
        nativeRid.set(ValueLayout.JAVA_BYTE,
            NativeLayouts.ROUTING_ID_SIZE_OFFSET, (byte) value.length);
        if (value.length > 0) {
            MemorySegment.copy(MemorySegment.ofArray(value), 0, nativeRid,
                NativeLayouts.ROUTING_ID_DATA_OFFSET, value.length);
        }
        return nativeRid;
    }

    private static SubmitException submitFailure(String apiName) {
        int errno = Native.errno();
        SubmitException submit =
            NativeSubmitErrors.submitExceptionOrNull(errno);
        if (submit != null) {
            return submit;
        }
        throw ZlinkException.fromLastError(apiName);
    }
}
