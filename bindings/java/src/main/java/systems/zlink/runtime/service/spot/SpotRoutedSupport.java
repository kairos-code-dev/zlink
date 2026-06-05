/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.spot;

import systems.zlink.contracts.service.spot.*;

import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.errors.ZlinkRecvException;
import systems.zlink.contracts.sockets.RecvFlags;
import systems.zlink.contracts.sockets.RecvResult;
import systems.zlink.contracts.errors.ZlinkRequestException;
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
import systems.zlink.runtime.nativeapi.NativeMessage;
import systems.zlink.runtime.nativeapi.NativeRoutingIds;
import systems.zlink.runtime.nativeapi.NativeSubmitErrors;
import systems.zlink.runtime.messaging.ReceivedPartCursor;
import systems.zlink.runtime.nativeapi.RequestProgressPump;
import systems.zlink.runtime.nativeapi.RequestReplySupport;
import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentMap;
import java.util.concurrent.atomic.AtomicLong;
import java.util.function.BiConsumer;

final class SpotRoutedSupport implements AutoCloseable {
        private static final int SEND_DONTWAIT = 1;
    private static final Linker LINKER = Linker.nativeLinker();
    private static final FunctionDescriptor FD_REPLY_CALLBACK =
      FunctionDescriptor.ofVoid(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.JAVA_LONG, ValueLayout.ADDRESS);
    private static final Arena CALLBACK_ARENA = Arena.ofShared();
    private static final MemorySegment REPLY_CALLBACK = LINKER.upcallStub(
      callbackHandle("handleReplyCallback", MethodType.methodType(void.class,
        int.class, MemorySegment.class, long.class, MemorySegment.class)),
      FD_REPLY_CALLBACK, CALLBACK_ARENA);
    private static final AtomicLong NEXT_REQUEST_ID = new AtomicLong(1L);
    private static final ConcurrentMap<Long, CompletableFuture<Received>> PENDING =
      new ConcurrentHashMap<>();
    private static final ConcurrentMap<Long, PendingCallback> PENDING_CALLBACKS =
      new ConcurrentHashMap<>();
    private final Spot spot;
    private final SpotDispatchSupport dispatchSupport;
    private final ThreadLocal<Received> activeLazyReceive = new ThreadLocal<>();

    SpotRoutedSupport(Spot spot) {
        this.spot = Objects.requireNonNull(spot, "spot");
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
        return requestViaNative(parts, timeout, (arena, payload, requestId,
                                                timeoutMs) -> {
            submitSpotRequestSpot(destNodeRid, destSpotRid, payload,
              REPLY_CALLBACK, MemorySegment.ofAddress(requestId), flags.value(),
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
            requestViaNativeCallback(parts, callback, timeout,
              (arena, payload, requestId, timeoutMs) -> {
                  submitSpotRequestSpot(destNodeRid, destSpotRid, payload,
                    REPLY_CALLBACK, MemorySegment.ofAddress(requestId),
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
        return requestViaNative(parts, timeout, (arena, payload, requestId,
                                                timeoutMs) -> {
            submitSpotRequestRouter(peerRid, payload, REPLY_CALLBACK,
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
            requestViaNativeCallback(parts, callback, timeout,
              (arena, payload, requestId, timeoutMs) -> {
                  submitSpotRequestRouter(peerRid, payload, REPLY_CALLBACK,
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
                     long requestSeq, List<Message> parts, SendFlags flags) {
        requireReplyFlagsSupported(flags);
        submitSpotReplySpot(Objects.requireNonNull(destNodeRid,
            "destNodeRid"), Objects.requireNonNull(destSpotRid,
            "destSpotRid"), requestSeq, parts);
    }

    public void replyToRouter(RoutingId peerRid, long requestSeq, List<Message> parts,
                       SendFlags flags) {
        requireReplyFlagsSupported(flags);
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
                    replyToSpot(source, sourceSpot, requestSeq, replyParts,
                        sendFlags);
                } else {
                    replyToRouter(source, requestSeq, replyParts, sendFlags);
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

    private CompletableFuture<List<Message>> requestViaNative(List<Message> parts,
                                                              Duration timeout,
                                                              NativeRequest request) {
        long timeoutMs = RequestReplySupport.timeoutMillis(timeout);
        long requestId = NEXT_REQUEST_ID.getAndIncrement();
        CompletableFuture<Received> future = registerPending(requestId, timeoutMs);
        RequestProgressPump.trackSpotRequest(future, handle(),
            "zlink-spot-routed-request-progress");
        try (Arena arena = Arena.ofConfined()) {
            int rc = request.invoke(arena, parts, requestId, timeoutMs);
            if (rc != 0) {
                future.cancel(false);
                throw new ZlinkSubmitException(SubmitResult.fromValue(rc));
            }
        } catch (RuntimeException ex) {
            PENDING.remove(requestId);
            future.cancel(false);
            throw ex;
        }
        return future.thenApply(InternalAccess::receivedTakeParts);
    }

    private void requestViaNativeCallback(List<Message> parts,
                                          BiConsumer<RequestResult, List<Message>> callback,
                                          Duration timeout,
                                          NativeRequest request) {
        Objects.requireNonNull(callback, "callback");
        long timeoutMs = RequestReplySupport.timeoutMillis(timeout);
        long requestId = NEXT_REQUEST_ID.getAndIncrement();
        PendingCallback pending = registerPendingCallback(requestId, callback);
        RequestProgressPump.trackSpotRequest(pending.progress, handle(),
            "zlink-spot-routed-request-progress");
        try (Arena arena = Arena.ofConfined()) {
            int rc = request.invoke(arena, parts, requestId, timeoutMs);
            if (rc != 0) {
                PENDING_CALLBACKS.remove(requestId, pending);
                pending.cancel();
                throw new ZlinkSubmitException(SubmitResult.fromValue(rc));
            }
        } catch (RuntimeException ex) {
            PENDING_CALLBACKS.remove(requestId, pending);
            pending.cancel();
            throw ex;
        }
    }

    private void submitSpotReplySpot(RoutingId destNodeRid,
                                     RoutingId destSpotRid,
                                     long requestSeq,
                                     List<Message> payload) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nodeRid = nativeRoutingId(arena, destNodeRid);
            MemorySegment spotRid = nativeRoutingId(arena, destSpotRid);
            if (payload.size() == 1) {
                while (true) {
                    int rc = spotReplySpotPartMoveOnce(nodeRid, spotRid,
                        requestSeq, payload.get(0), Native.PART_FINAL, arena);
                    if (rc == 0)
                        return;
                    int errno = Native.errno();
                    if (errno == NativeErrno.EINTR)
                        continue;
                    throw submitFailure("zlink_spot_reply_spot_part");
                }
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
                while (true) {
                    int rc = spotRequestSpotPartMoveOnce(nodeRid, spotRid,
                      payload.get(0), handler, userData, flags,
                      Native.PART_FINAL, timeoutMs, arena);
                    if (rc == 0)
                        return;
                    int errno = Native.errno();
                    if (errno == NativeErrno.EINTR)
                        continue;
                    throw submitFailure("zlink_spot_request_spot_part");
                }
            }
            submitParts(payload, "zlink_spot_request_spot_part",
                (index, partFlag) -> {
                    boolean last = partFlag == Native.PART_FINAL;
                    return spotRequestSpotPartOnce(nodeRid, spotRid,
                        payload.get(index), handler, userData, flags, partFlag,
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
                        payload.get(index), handler, userData, flags, partFlag,
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
            while (true) {
                int rc = submitter.submit(i, partFlag);
                if (rc == 0)
                    break;
                int errno = Native.errno();
                if (errno == NativeErrno.EINTR)
                    continue;
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
        throw InternalAccess.zlinkExceptionFromLastError(apiName);
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
                throw InternalAccess.zlinkExceptionFromLastError("zlink_spot_recv_part");
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
        dispatchSupport.close();
    }

    private static CompletableFuture<Received> registerPending(long requestId,
                                                               long timeoutMs) {
        CompletableFuture<Received> future = new CompletableFuture<>();
        PENDING.put(requestId, future);
        RequestReplySupport.armTimeout(PENDING, requestId, future, timeoutMs);
        return future;
    }

    private static PendingCallback registerPendingCallback(
      long requestId,
      BiConsumer<RequestResult, List<Message>> callback) {
        PendingCallback pending = new PendingCallback(callback);
        PENDING_CALLBACKS.put(requestId, pending);
        return pending;
    }

    private static void handleReplyCallback(int result, MemorySegment parts,
                                            long partCount,
                                            MemorySegment userdata) {
        long requestId = userdata.address();
        CompletableFuture<Received> future = PENDING.remove(requestId);
        PendingCallback pending = PENDING_CALLBACKS.remove(requestId);
        try {
            if (result != RequestResult.OK.value()) {
                if (future != null) {
                    future.completeExceptionally(new ZlinkRequestException(
                        RequestResult.fromValue(result), result));
                }
                if (pending != null) {
                    pending.complete(RequestResult.fromValue(result), List.of(),
                        null);
                }
                return;
            }
            Message[] frames = InternalAccess.messageFromOwnedMessageVectorShared(
              parts, partCount);
            if (pending != null) {
                pending.complete(RequestResult.OK, java.util.Arrays.asList(frames),
                    frames);
            } else {
                Received received = InternalAccess.received(null, null, frames,
                    0L, false, null);
                if (future == null || !future.complete(received)) {
                    received.close();
                }
            }
        } catch (Throwable error) {
            if (future != null) {
                future.completeExceptionally(error);
            }
            if (pending != null) {
                pending.completeExceptionally(error);
            }
        } finally {
            NativeMessage.multipartClose(parts, partCount);
        }
    }

    private static final class PendingCallback {
        private final BiConsumer<RequestResult, List<Message>> callback;
        private final CompletableFuture<Void> progress = new CompletableFuture<>();

        private PendingCallback(
          BiConsumer<RequestResult, List<Message>> callback) {
            this.callback = callback;
        }

        private void cancel() {
            progress.complete(null);
        }

        private void complete(RequestResult result, List<Message> reply,
                              Message[] closeOnCallbackFailure) {
            try {
                callback.accept(result, reply);
                progress.complete(null);
            } catch (Throwable error) {
                if (closeOnCallbackFailure != null) {
                    Message.closeAll(closeOnCallbackFailure);
                }
                progress.completeExceptionally(error);
            }
        }

        private void completeExceptionally(Throwable error) {
            progress.completeExceptionally(error);
        }
    }

    private static MemorySegment nativeRoutingId(Arena arena, RoutingId routingId) {
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

    private static void requireReplyFlagsSupported(SendFlags flags) {
        Objects.requireNonNull(flags, "flags");
        if (flags != SendFlags.NONE) {
            throw new ZlinkSubmitException(SubmitResult.NOT_SUPPORTED);
        }
    }

    private static MethodHandle callbackHandle(String name, MethodType type) {
        try {
            return MethodHandles.lookup().findStatic(SpotRoutedSupport.class,
              name, type);
        } catch (ReflectiveOperationException ex) {
            throw new ExceptionInInitializerError(ex);
        }
    }

    private MethodHandle callbackHandle(String name, MethodType type,
                                        SpotRoutedSupport receiver) {
        try {
            return MethodHandles.lookup().findVirtual(SpotRoutedSupport.class,
              name, type).bindTo(receiver);
        } catch (ReflectiveOperationException ex) {
            throw new ExceptionInInitializerError(ex);
        }
    }

    @FunctionalInterface
    private interface NativeRequest {
        int invoke(Arena arena, List<Message> payload, long requestId,
                   long timeoutMs);
    }

    @FunctionalInterface
    private interface PartSubmitter {
        int submit(int index, int partFlag);
    }

}
