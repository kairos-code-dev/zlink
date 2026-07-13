/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.spot;

import systems.zlink.contracts.service.spot.*;

import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.errors.ZlinkRecvException;
import systems.zlink.contracts.sockets.RecvFlags;
import systems.zlink.contracts.sockets.RecvResult;
import systems.zlink.contracts.sockets.RequestResult;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.service.spot.SpotDispatchEventHandler;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.sockets.SubmitResult;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.nativeapi.Native;
import systems.zlink.runtime.nativeapi.NativeErrno;
import systems.zlink.runtime.nativeapi.NativeLayouts;
import systems.zlink.runtime.nativeapi.NativeRoutingIds;
import systems.zlink.runtime.nativeapi.NativeSubmitErrors;
import systems.zlink.runtime.messaging.ReceivedPartCursor;
import systems.zlink.runtime.nativeapi.RequestReplySupport;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.time.Duration;
import java.util.List;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.function.BiConsumer;

final class SpotRoutedSupport implements AutoCloseable {
    private static final int SEND_DONTWAIT = 1;
    private final Spot spot;
    private final SpotRoutedRequestPlane requestPlane;
    private final SpotDispatchSupport dispatchSupport;
    private final ThreadLocal<Received> activeLazyReceive = new ThreadLocal<>();

    SpotRoutedSupport(Spot spot) {
        this.spot = Objects.requireNonNull(spot, "spot");
        this.requestPlane = new SpotRoutedRequestPlane(spot);
        this.dispatchSupport = new SpotDispatchSupport(spot);
    }

    public boolean sendToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                       List<Message> parts, SendFlags flags) {
        Objects.requireNonNull(flags, "flags");
        submitSpotSendSpot(Objects.requireNonNull(destNodeRid,
            "destNodeRid"), Objects.requireNonNull(destSpotRid,
            "destSpotRid"), parts, flags == SendFlags.DONT_WAIT);
        return true;
    }

    public CompletableFuture<List<Message>> requestToSpot(RoutingId destNodeRid,
                                                   RoutingId destSpotRid,
                                                   List<Message> parts,
                                                   Duration timeout,
                                                   SendFlags flags) {
        Objects.requireNonNull(flags, "flags");
        return requestPlane.request(parts, timeout, (arena, payload, requestId,
                                                    replyCallback, timeoutMs) -> {
            submitSpotRequestSpot(destNodeRid, destSpotRid, payload,
              replyCallback, MemorySegment.ofAddress(requestId), flags.value(),
              RequestReplySupport.toTimeoutInt(timeoutMs));
            return 0;
        });
    }

    public boolean requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                          List<Message> parts,
                          BiConsumer<RequestResult, List<Message>> callback,
                          SendFlags flags,
                          Duration timeout) {
        try {
            requestPlane.requestCallback(parts, callback, timeout,
              (arena, payload, requestId, replyCallback, timeoutMs) -> {
                  submitSpotRequestSpot(destNodeRid, destSpotRid, payload,
                    replyCallback, MemorySegment.ofAddress(requestId),
                    flags.value(), RequestReplySupport.toTimeoutInt(timeoutMs));
                  return 0;
              });
            return true;
        } catch (ZlinkSubmitException ex) {
            if (flags == SendFlags.DONT_WAIT
                && ex.getResult() == SubmitResult.BACKPRESSURED) {
                return false;
            }
            throw ex;
        }
    }

    public CompletableFuture<List<Message>> requestToRouter(RoutingId peerRid,
                                                     List<Message> parts,
                                                     Duration timeout,
                                                     SendFlags flags) {
        Objects.requireNonNull(flags, "flags");
        return requestPlane.request(parts, timeout, (arena, payload, requestId,
                                                    replyCallback, timeoutMs) -> {
            submitSpotRequestRouter(peerRid, payload, replyCallback,
              MemorySegment.ofAddress(requestId), flags.value(),
              RequestReplySupport.toTimeoutInt(timeoutMs));
            return 0;
        });
    }

    public boolean requestToRouter(RoutingId peerRid, List<Message> parts,
                            BiConsumer<RequestResult, List<Message>> callback,
                            SendFlags flags,
                            Duration timeout) {
        try {
            requestPlane.requestCallback(parts, callback, timeout,
              (arena, payload, requestId, replyCallback, timeoutMs) -> {
                  submitSpotRequestRouter(peerRid, payload, replyCallback,
                    MemorySegment.ofAddress(requestId), flags.value(),
                    RequestReplySupport.toTimeoutInt(timeoutMs));
                  return 0;
              });
            return true;
        } catch (ZlinkSubmitException ex) {
            if (flags == SendFlags.DONT_WAIT
                && ex.getResult() == SubmitResult.BACKPRESSURED) {
                return false;
            }
            throw ex;
        }
    }

    public void replyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                     long requestSeq, List<Message> parts) {
        submitSpotReplySpot(Objects.requireNonNull(destNodeRid,
            "destNodeRid"), Objects.requireNonNull(destSpotRid,
            "destSpotRid"), requestSeq, parts);
    }

    public void replyToRouter(RoutingId peerRid, long requestSeq, List<Message> parts,
                       SendFlags ignored) {
        replyToRouter(peerRid, requestSeq, parts);
    }

    public void replyToRouter(RoutingId peerRid, long requestSeq,
                              List<Message> parts) {
        submitSpotReplyRouter(Objects.requireNonNull(peerRid, "peerRid"),
            requestSeq, parts);
    }

    public Received recvRouted(RecvFlags flags) {
        Objects.requireNonNull(flags, "flags");
        ensureOpen();
        Received active = activeLazyReceive.get();
        if (active != null) {
            InternalAccess.receivedForceMaterialize(active);
            activeLazyReceive.remove();
        }
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment sourceRidOut = arena.allocate(ValueLayout.ADDRESS);
            MemorySegment spotRidOut = arena.allocate(ValueLayout.ADDRESS);
            MemorySegment requestSeqOut = arena.allocate(ValueLayout.JAVA_LONG);
            MemorySegment hasMoreOut = arena.allocate(ValueLayout.JAVA_INT);
            Message firstPart = new Message();
            boolean success = false;
            try {
                int rc = Native.spotRecvPart(handle(), sourceRidOut, spotRidOut,
                    requestSeqOut, InternalAccess.messageNativeHandle(firstPart),
                    hasMoreOut, flags.value());
                if (rc != 0) {
                    if (flags == RecvFlags.DONT_WAIT
                        && (rc == RecvResult.NO_DATA.value()
                            || rc == RecvResult.BUSY.value())) {
                        return null;
                    }
                    throw new ZlinkRecvException(RecvResult.fromValue(rc));
                }
                success = true;
                boolean hasMore = hasMoreOut.get(ValueLayout.JAVA_INT, 0) != 0;
                InternalAccess.messageFinishReceive(firstPart, hasMore);
                RoutingId source = NativeRoutingIds.readOut(sourceRidOut);
                RoutingId sourceSpot = NativeRoutingIds.readOut(spotRidOut);
                long requestSeq = requestSeqOut.get(ValueLayout.JAVA_LONG, 0);
                Received received = createRoutedReceived(source, sourceSpot,
                    firstPart, hasMore, requestSeq, flags.value());
                if (hasMore) {
                    activeLazyReceive.set(received);
                }
                return received;
            } finally {
                if (!success) {
                    try {
                        firstPart.close();
                    } catch (RuntimeException ignored) {
                    }
                }
            }
        }
    }

    private Received createRoutedReceived(RoutingId source,
                                          RoutingId sourceSpot,
                                          Message firstPart,
                                          boolean hasMore,
                                          long requestSeq,
                                          int recvFlags) {
        ReceivedPartCursor cursor = hasMore
            ? new SpotReceiveCursor(recvFlags) : null;
        Received[] ref = new Received[1];
        Runnable onTerminal = () -> {
            Received pending = activeLazyReceive.get();
            if (pending == ref[0]) {
                activeLazyReceive.remove();
            }
        };
        BiConsumer<List<Message>, SendFlags> replySender =
            requestSeq == 0L ? null : (replyParts, sendFlags) -> {
                if (sourceSpot != null) {
                    replyToSpot(source, sourceSpot, requestSeq, replyParts);
                } else {
                    replyToRouter(source, requestSeq, replyParts);
                }
            };
        Received received = InternalAccess.receivedLazy(source, sourceSpot,
            firstPart, cursor, requestSeq, requestSeq != 0L, replySender,
            onTerminal);
        if (source != null && sourceSpot != null) {
            InternalAccess.receivedSetSendSender(received,
                (sendParts, sendFlags) -> sendToSpot(source, sourceSpot,
                    sendParts, sendFlags));
        }
        ref[0] = received;
        return received;
    }

    public void setDispatchHandler(SpotDispatchEventHandler handler) {
        dispatchSupport.setDispatchHandler(handler);
    }

    private void submitSpotReplySpot(RoutingId destNodeRid,
                                     RoutingId destSpotRid,
                                     long requestSeq,
                                     List<Message> payload) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nodeRid = nativeRoutingId(arena, destNodeRid);
            MemorySegment spotRid = nativeRoutingId(arena, destSpotRid);
            if (payload.size() == 1) {
                int rc = NativeErrno.retryWhileInterrupted(
                    () -> spotReplySpotPartMoveOnce(nodeRid, spotRid,
                        requestSeq, payload.get(0), Native.PART_FINAL, arena),
                    result -> result != 0);
                if (rc == 0)
                    return;
                throw submitFailure("zlink_spot_reply_spot_part");
            }
            submitParts(payload, "zlink_spot_reply_spot_part",
                (index, partFlag) -> spotReplySpotPartOnce(nodeRid, spotRid,
                    requestSeq, payload.get(index), partFlag, arena));
        }
    }

    private void submitSpotSendSpot(RoutingId destNodeRid,
                                    RoutingId destSpotRid,
                                    List<Message> payload,
                                    boolean nonBlocking) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nodeRid = nativeRoutingId(arena, destNodeRid);
            MemorySegment spotRid = nativeRoutingId(arena, destSpotRid);
            int flags = nonBlocking ? SEND_DONTWAIT : 0;
            submitParts(payload, "zlink_spot_send_spot_part",
                (index, partFlag) -> spotSendSpotPartOnce(nodeRid, spotRid,
                    payload.get(index), flags, partFlag, arena));
        }
    }

    private void submitSpotRequestSpot(RoutingId destNodeRid,
                                       RoutingId destSpotRid,
                                       List<Message> payload,
                                       MemorySegment handler,
                                       MemorySegment userData,
                                       int flags,
                                       int timeoutMs) {
        Objects.requireNonNull(destNodeRid, "destNodeRid");
        Objects.requireNonNull(destSpotRid, "destSpotRid");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nodeRid = nativeRoutingId(arena, destNodeRid);
            MemorySegment spotRid = nativeRoutingId(arena, destSpotRid);
            if (payload.size() == 1) {
                int rc = NativeErrno.retryWhileInterrupted(
                    () -> spotRequestSpotPartMoveOnce(nodeRid, spotRid,
                      payload.get(0), handler, userData, flags,
                      Native.PART_FINAL, timeoutMs, arena),
                    result -> result != 0);
                if (rc == 0)
                    return;
                throw submitFailure("zlink_spot_request_spot_part");
            }
            submitParts(payload, "zlink_spot_request_spot_part",
                (index, partFlag) -> {
                    boolean last = partFlag == Native.PART_FINAL;
                    return spotRequestSpotPartOnce(nodeRid, spotRid,
                        payload.get(index), last ? handler : MemorySegment.NULL,
                        last ? userData : MemorySegment.NULL, flags, partFlag,
                        timeoutMs, arena);
                });
        }
    }

    private void submitSpotRequestRouter(RoutingId peerRid,
                                         List<Message> payload,
                                         MemorySegment handler,
                                         MemorySegment userData,
                                         int flags,
                                         int timeoutMs) {
        Objects.requireNonNull(peerRid, "peerRid");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeRid = nativeRoutingId(arena, peerRid);
            submitParts(payload, "zlink_spot_request_router_part",
                (index, partFlag) -> {
                    boolean last = partFlag == Native.PART_FINAL;
                    return spotRequestRouterPartOnce(nativeRid,
                        payload.get(index), last ? handler : MemorySegment.NULL,
                        last ? userData : MemorySegment.NULL, flags, partFlag,
                        timeoutMs, arena);
                });
        }
    }

    private void submitSpotReplyRouter(RoutingId peerRid, long requestSeq,
                                       List<Message> payload) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeRid = nativeRoutingId(arena, peerRid);
            submitParts(payload, "zlink_spot_reply_router_part",
                (index, partFlag) -> spotReplyRouterPartOnce(nativeRid,
                    requestSeq, payload.get(index), partFlag, arena));
        }
    }

    private void submitParts(List<Message> payload, String apiName,
                             PartSubmitter submitter) {
        for (int i = 0; i < payload.size(); i++) {
            int partFlag = i + 1 < payload.size()
                ? Native.PART_MORE : Native.PART_FINAL;
            int partIndex = i;
            int rc = NativeErrno.retryWhileInterrupted(
                () -> submitter.submit(partIndex, partFlag),
                result -> result != 0);
            if (rc != 0) {
                throw submitFailure(apiName);
            }
        }
    }

    private int spotReplySpotPartOnce(MemorySegment nodeRid,
                                      MemorySegment spotRid,
                                      long requestSeq,
                                      Message part,
                                      int partFlag,
                                      Arena arena) {
        MemorySegment nativeMsg = arena.allocate(NativeLayouts.MESSAGE_LAYOUT);
        InternalAccess.messageCopyTo(part, nativeMsg);
        return Native.spotReplySpotPart(handle(), nodeRid, spotRid,
            requestSeq, nativeMsg, partFlag);
    }

    private int spotReplySpotPartMoveOnce(MemorySegment nodeRid,
                                      MemorySegment spotRid,
                                      long requestSeq,
                                      Message part,
                                      int partFlag,
                                      Arena arena) {
        MemorySegment nativeMsg = arena.allocate(NativeLayouts.MESSAGE_LAYOUT);
        Object anchor = InternalAccess.messageTransferTo(part, nativeMsg);
        try {
            int rc = Native.spotReplySpotPart(handle(), nodeRid, spotRid,
                requestSeq, nativeMsg, partFlag);
            if (rc != 0) {
                InternalAccess.messageRestoreFromNative(part, nativeMsg, false,
                    anchor);
            }
            return rc;
        } catch (RuntimeException ex) {
            InternalAccess.messageRestoreFromNative(part, nativeMsg, false,
                anchor);
            throw ex;
        }
    }

    private int spotSendSpotPartOnce(MemorySegment nodeRid,
                                     MemorySegment spotRid,
                                     Message part,
                                     int flags,
                                     int partFlag,
                                     Arena arena) {
        MemorySegment nativeMsg = arena.allocate(NativeLayouts.MESSAGE_LAYOUT);
        InternalAccess.messageCopyTo(part, nativeMsg);
        return Native.spotSendSpotPart(handle(), nodeRid, spotRid,
            nativeMsg, flags, partFlag);
    }

    private int spotRequestSpotPartOnce(MemorySegment nodeRid,
                                        MemorySegment spotRid,
                                        Message part,
                                        MemorySegment handler,
                                        MemorySegment userData,
                                        int flags,
                                        int partFlag,
                                        int timeoutMs,
                                        Arena arena) {
        MemorySegment nativeMsg = arena.allocate(NativeLayouts.MESSAGE_LAYOUT);
        InternalAccess.messageCopyTo(part, nativeMsg);
        return Native.spotRequestSpotPart(handle(), nodeRid, spotRid,
          nativeMsg, handler, userData, flags, partFlag, timeoutMs);
    }

    private int spotRequestSpotPartMoveOnce(MemorySegment nodeRid,
                                        MemorySegment spotRid,
                                        Message part,
                                        MemorySegment handler,
                                        MemorySegment userData,
                                        int flags,
                                        int partFlag,
                                        int timeoutMs,
                                        Arena arena) {
        MemorySegment nativeMsg = arena.allocate(NativeLayouts.MESSAGE_LAYOUT);
        Object anchor = InternalAccess.messageTransferTo(part, nativeMsg);
        try {
            int rc = Native.spotRequestSpotPart(handle(), nodeRid, spotRid,
              nativeMsg, handler, userData, flags, partFlag, timeoutMs);
            if (rc != 0) {
                InternalAccess.messageRestoreFromNative(part, nativeMsg, false,
                    anchor);
            }
            return rc;
        } catch (RuntimeException ex) {
            InternalAccess.messageRestoreFromNative(part, nativeMsg, false,
                anchor);
            throw ex;
        }
    }

    private int spotRequestRouterPartOnce(MemorySegment nativeRid,
                                          Message part,
                                          MemorySegment handler,
                                          MemorySegment userData,
                                          int flags,
                                          int partFlag,
                                          int timeoutMs,
                                          Arena arena) {
        MemorySegment nativeMsg = arena.allocate(NativeLayouts.MESSAGE_LAYOUT);
        InternalAccess.messageCopyTo(part, nativeMsg);
        return Native.spotRequestRouterPart(handle(), nativeRid,
          nativeMsg, handler, userData, flags, partFlag, timeoutMs);
    }

    private int spotReplyRouterPartOnce(MemorySegment nativeRid,
                                        long requestSeq,
                                        Message part,
                                        int partFlag,
                                        Arena arena) {
        MemorySegment nativeMsg = arena.allocate(NativeLayouts.MESSAGE_LAYOUT);
        InternalAccess.messageCopyTo(part, nativeMsg);
        return Native.spotReplyRouterPart(handle(), nativeRid,
            requestSeq, nativeMsg, partFlag);
    }

    private ZlinkSubmitException submitFailure(String apiName) {
        int errno = Native.errno();
        ZlinkSubmitException submit = NativeSubmitErrors.submitExceptionOrNull(errno);
        if (submit != null)
            return submit;
        throw InternalAccess.zlinkExceptionFromLastError(
            systems.zlink.contracts.errors.ErrorCategory.SUBMIT);
    }

    private final class SpotReceiveCursor implements ReceivedPartCursor {
        private final int flags;
        private final Arena arena = Arena.ofConfined();
        private final MemorySegment sourceRidOut = arena.allocate(
            ValueLayout.ADDRESS);
        private final MemorySegment spotRidOut = arena.allocate(
            ValueLayout.ADDRESS);
        private final MemorySegment requestSeqOut = arena.allocate(
            ValueLayout.JAVA_LONG);
        private final MemorySegment hasMoreOut = arena.allocate(
            ValueLayout.JAVA_INT);
        private boolean hasMore = true;
        private boolean closed;

        private SpotReceiveCursor(int flags) {
            this.flags = flags;
        }

        @Override
        public Message nextPartOrNull() {
            if (closed || !hasMore)
                return null;
            while (true) {
                Message next = new Message();
                boolean success = false;
                try {
                    int rc = Native.spotRecvPart(handle(), sourceRidOut,
                        spotRidOut, requestSeqOut,
                        InternalAccess.messageNativeHandle(next),
                        hasMoreOut, flags);
                    if (rc == 0) {
                        success = true;
                        hasMore = hasMoreOut.get(ValueLayout.JAVA_INT, 0) != 0;
                        InternalAccess.messageFinishReceive(next, hasMore);
                        if (!hasMore) {
                            closeArena();
                        }
                        return next;
                    }
                } finally {
                    if (!success) {
                        try {
                            next.close();
                        } catch (RuntimeException ignored) {
                        }
                    }
                }

                int errno = Native.errno();
                if (errno == NativeErrno.EINTR)
                    continue;
                closeArena();
                throw InternalAccess.zlinkExceptionFromLastError(systems.zlink.contracts.errors.ErrorCategory.RECV);
            }
        }

        @Override
        public void close() {
            if (closed)
                return;
            while (hasMore) {
                Message next = nextPartOrNull();
                if (next == null)
                    break;
                try {
                    next.close();
                } catch (RuntimeException ignored) {
                }
            }
            closed = true;
            closeArena();
        }

        private void closeArena() {
            hasMore = false;
            if (arena.scope().isAlive()) {
                arena.close();
            }
        }
    }

    private MemorySegment handle() {
        return InternalAccess.spotHandle(spot);
    }

    private void ensureOpen() {
        MemorySegment handle = handle();
        if (handle == null || handle.address() == 0) {
            throw new IllegalStateException("spot is closed");
        }
        dispatchSupport.ensureNoCallbackFailure();
    }

    @Override
    public void close() {
        requestPlane.close();
        dispatchSupport.close();
    }

    private static MemorySegment nativeRoutingId(Arena arena, RoutingId routingId) {
        return NativeRoutingIds.allocate(arena, routingId);
    }

    @FunctionalInterface
    private interface PartSubmitter {
        int submit(int index, int partFlag);
    }

}
