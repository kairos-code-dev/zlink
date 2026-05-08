/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.service.spot;

import systems.zlink.HandlerException;
import systems.zlink.HandlerResult;
import systems.zlink.Message;
import systems.zlink.Received;
import systems.zlink.RecvException;
import systems.zlink.RecvFlags;
import systems.zlink.RecvResult;
import systems.zlink.RequestException;
import systems.zlink.RequestResult;
import systems.zlink.RoutingId;
import systems.zlink.SendFlags;
import systems.zlink.SpotDispatchInfo;
import systems.zlink.SpotDispatchEvent;
import systems.zlink.SpotDispatchEventHandler;
import systems.zlink.SpotDispatchSubjectKind;
import systems.zlink.SpotRoutedHandler;
import systems.zlink.SubmitException;
import systems.zlink.SubmitResult;
import systems.zlink.ZlinkException;
import systems.zlink.internal.ActorInterop;
import systems.zlink.internal.EnumCodecs;
import systems.zlink.internal.InternalAccess;
import systems.zlink.internal.Native;
import systems.zlink.internal.NativeLayouts;
import systems.zlink.internal.NativeMsg;
import systems.zlink.internal.ReceivedPartCursor;
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
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicLong;
import java.util.function.BiConsumer;

final class SpotRoutedSupport implements AutoCloseable {
    private static final int ERRNO_EINTR = 4;
    private static final int SEND_DONTWAIT = 1;
    private static final Linker LINKER = Linker.nativeLinker();
    private static final FunctionDescriptor FD_REPLY_CALLBACK =
      FunctionDescriptor.ofVoid(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.JAVA_LONG, ValueLayout.ADDRESS);
    private static final FunctionDescriptor FD_SPOT_HANDLER =
      FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.ADDRESS,
        ValueLayout.JAVA_LONG, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG,
        ValueLayout.ADDRESS);
    private static final FunctionDescriptor FD_DISPATCH_HANDLER =
      FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS);
    private static final Arena CALLBACK_ARENA = Arena.ofShared();
    private static final MemorySegment REPLY_CALLBACK = LINKER.upcallStub(
      callbackHandle("handleReplyCallback", MethodType.methodType(void.class,
        int.class, MemorySegment.class, long.class, MemorySegment.class)),
      FD_REPLY_CALLBACK, CALLBACK_ARENA);
    private static final AtomicLong NEXT_REQUEST_ID = new AtomicLong(1L);
    private static final ConcurrentMap<Long, CompletableFuture<Received>> PENDING =
      new ConcurrentHashMap<>();
    private static final ScheduledExecutorService REQUEST_TIMEOUTS =
      Executors.newSingleThreadScheduledExecutor(new TimeoutThreadFactory());

    private final Spot spot;
    private SpotRoutedHandler routedHandler;
    private SpotDispatchEventHandler dispatchEventHandler;
    private ExecutorService callbackExecutor;
    private Arena routedCallbackArena;
    private Arena dispatchCallbackArena;
    private volatile RuntimeException callbackFailure;
    private final ThreadLocal<Received> activeLazyReceive = new ThreadLocal<>();

    SpotRoutedSupport(Spot spot) {
        this.spot = Objects.requireNonNull(spot, "spot");
    }

    boolean sendToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                       List<Message> parts, SendFlags flags) {
        Objects.requireNonNull(flags, "flags");
        submitSpotSendSpot(Objects.requireNonNull(destNodeRid,
            "destNodeRid"), Objects.requireNonNull(destSpotRid,
            "destSpotRid"), parts, flags == SendFlags.DONT_WAIT);
        return true;
    }

    CompletableFuture<List<Message>> requestToSpot(RoutingId destNodeRid,
                                                   RoutingId destSpotRid,
                                                   List<Message> parts,
                                                   Duration timeout,
                                                   SendFlags flags) {
        Objects.requireNonNull(flags, "flags");
        return requestViaNative(parts, timeout, (arena, payload, requestId,
                                                timeoutMs) -> {
            submitSpotRequestSpot(destNodeRid, destSpotRid, payload,
              REPLY_CALLBACK, MemorySegment.ofAddress(requestId), flags.value(),
              toTimeoutInt(timeoutMs));
            return 0;
        });
    }

    boolean requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                          List<Message> parts,
                          BiConsumer<RequestResult, List<Message>> callback,
                          SendFlags flags,
                          Duration timeout) {
        try {
            requestToSpot(destNodeRid, destSpotRid, parts, timeout, flags)
              .whenComplete((reply, error) -> callback.accept(error == null
                ? RequestResult.OK : requestResult(error),
                reply == null ? List.of() : reply));
            return true;
        } catch (SubmitException ex) {
            if (flags == SendFlags.DONT_WAIT
                && ex.getResult() == SubmitResult.BACKPRESSURED) {
                return false;
            }
            throw ex;
        }
    }

    CompletableFuture<List<Message>> requestToRouter(RoutingId peerRid,
                                                     List<Message> parts,
                                                     Duration timeout,
                                                     SendFlags flags) {
        Objects.requireNonNull(flags, "flags");
        return requestViaNative(parts, timeout, (arena, payload, requestId,
                                                timeoutMs) -> {
            submitSpotRequestRouter(peerRid, payload, REPLY_CALLBACK,
              MemorySegment.ofAddress(requestId), flags.value(),
              toTimeoutInt(timeoutMs));
            return 0;
        });
    }

    boolean requestToRouter(RoutingId peerRid, List<Message> parts,
                            BiConsumer<RequestResult, List<Message>> callback,
                            SendFlags flags,
                            Duration timeout) {
        try {
            requestToRouter(peerRid, parts, timeout, flags)
              .whenComplete((reply, error) -> callback.accept(error == null
                ? RequestResult.OK : requestResult(error),
                reply == null ? List.of() : reply));
            return true;
        } catch (SubmitException ex) {
            if (flags == SendFlags.DONT_WAIT
                && ex.getResult() == SubmitResult.BACKPRESSURED) {
                return false;
            }
            throw ex;
        }
    }

    void replyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                     long requestSeq, List<Message> parts, SendFlags flags) {
        requireReplyFlagsSupported(flags);
        submitSpotReplySpot(Objects.requireNonNull(destNodeRid,
            "destNodeRid"), Objects.requireNonNull(destSpotRid,
            "destSpotRid"), requestSeq, parts);
    }

    void replyToRouter(RoutingId peerRid, long requestSeq, List<Message> parts,
                       SendFlags flags) {
        requireReplyFlagsSupported(flags);
        submitSpotReplyRouter(Objects.requireNonNull(peerRid, "peerRid"),
            requestSeq, parts);
    }

    Received recvRouted(RecvFlags flags) {
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
                    throw new RecvException(RecvResult.fromValue(rc));
                }
                success = true;
                boolean hasMore = hasMoreOut.get(ValueLayout.JAVA_INT, 0) != 0;
                InternalAccess.messageFinishReceive(firstPart, hasMore);
                RoutingId source = readRoutingIdOut(sourceRidOut);
                RoutingId sourceSpot = readRoutingIdOut(spotRidOut);
                long requestSeq = requestSeqOut.get(ValueLayout.JAVA_LONG, 0);
                ReceivedPartCursor cursor = hasMore
                    ? new SpotReceiveCursor(flags.value()) : null;
                Received[] ref = new Received[1];
                Runnable onTerminal = () -> {
                    Received pending = activeLazyReceive.get();
                    if (pending == ref[0]) {
                        activeLazyReceive.remove();
                    }
                };
                Received received = InternalAccess.receivedLazy(source,
                    sourceSpot, firstPart, cursor, requestSeq,
                    requestSeq != 0L, requestSeq == 0L ? null
                    : (replyParts, sendFlags) -> {
                        if (sourceSpot != null) {
                            replyToSpot(source, sourceSpot, requestSeq,
                                replyParts, sendFlags);
                        } else {
                            replyToRouter(source, requestSeq, replyParts,
                                sendFlags);
                        }
                    }, onTerminal);
                ref[0] = received;
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

    void onRoutedReceive(SpotRoutedHandler handler) {
        Objects.requireNonNull(handler, "handler");
        ensureOpen();
        ensureNoCallbackFailure();
        ExecutorService executor = ensureCallbackExecutor("zlink-spot-routed-callback");
        Arena arena = Arena.ofShared();
        MemorySegment stub = LINKER.upcallStub(callbackHandle(
          "handleRoutedCallback", MethodType.methodType(void.class,
            MemorySegment.class, MemorySegment.class, long.class,
            MemorySegment.class, long.class, MemorySegment.class)),
          FD_SPOT_HANDLER, arena);
        boolean success = false;
        try {
            int rc = Native.spotHandler(handle(), stub, MemorySegment.NULL);
            if (rc != 0) {
                throw new HandlerException(HandlerResult.fromValue(rc));
            }
            success = true;
            closeArena(routedCallbackArena);
            routedCallbackArena = arena;
            routedHandler = handler;
        } finally {
            if (!success) {
                closeArena(arena);
                if (callbackExecutor == executor && routedHandler == null
                    && dispatchEventHandler == null) {
                    shutdownExecutor(executor);
                    callbackExecutor = null;
                }
            }
        }
    }

    void onDispatchEvent(SpotDispatchEventHandler handler) {
        Objects.requireNonNull(handler, "handler");
        ensureOpen();
        ensureNoCallbackFailure();
        ExecutorService executor = ensureCallbackExecutor("zlink-spot-dispatch-callback");
        Arena arena = Arena.ofShared();
        MemorySegment stub = LINKER.upcallStub(callbackHandle(
          "handleDispatchEventCallback",
          MethodType.methodType(void.class, MemorySegment.class,
            MemorySegment.class, MemorySegment.class), this),
          FD_DISPATCH_HANDLER, arena);
        boolean success = false;
        try {
            int rc = Native.spotDispatchEventHandler(handle(), stub,
              MemorySegment.NULL);
            if (rc != 0) {
                throw new HandlerException(HandlerResult.fromValue(rc));
            }
            success = true;
            closeArena(dispatchCallbackArena);
            dispatchCallbackArena = arena;
            dispatchEventHandler = handler;
        } finally {
            if (!success) {
                closeArena(arena);
                if (callbackExecutor == executor && routedHandler == null
                    && dispatchEventHandler == null) {
                    shutdownExecutor(executor);
                    callbackExecutor = null;
                }
            }
        }
    }

    @Override
    public void close() {
        routedHandler = null;
        dispatchEventHandler = null;
        callbackFailure = null;
        shutdownExecutor(callbackExecutor);
        callbackExecutor = null;
        closeArena(routedCallbackArena);
        closeArena(dispatchCallbackArena);
        routedCallbackArena = null;
        dispatchCallbackArena = null;
    }

    private void handleRoutedCallback(MemorySegment sourceRid,
                                      MemorySegment spotRid,
                                      long requestSeq,
                                      MemorySegment parts,
                                      long partCount,
                                      MemorySegment userdata) {
        SpotRoutedHandler handler = routedHandler;
        ExecutorService executor = callbackExecutor;
        if (handler == null || executor == null) {
            NativeMsg.multipartClose(parts, partCount);
            return;
        }
        RoutedSnapshot snapshot = null;
        try {
            Message[] frames = InternalAccess.messageFromOwnedMsgVectorShared(
              parts, partCount);
            snapshot = new RoutedSnapshot(readRoutingId(sourceRid),
              readRoutingId(spotRid), requestSeq, frames);
            NativeMsg.multipartClose(parts, partCount);
            RoutedSnapshot callbackSnapshot = snapshot;
            executor.execute(() -> dispatchRouted(handler, callbackSnapshot));
        } catch (RejectedExecutionException ex) {
            recordCallbackFailure(ex);
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
        }
    }

    private void handleDispatchEventCallback(MemorySegment spotHandle,
                                             MemorySegment info,
                                             MemorySegment userdata) {
        SpotDispatchEventHandler handler = dispatchEventHandler;
        ExecutorService executor = callbackExecutor;
        if (handler == null || executor == null) {
            return;
        }
        try {
            SpotDispatchInfo dispatchInfo = decodeDispatchInfo(info);
            executor.execute(() -> dispatchEvent(handler, dispatchInfo));
        } catch (RejectedExecutionException ex) {
            recordCallbackFailure(ex);
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
        }
    }

    private void dispatchRouted(SpotRoutedHandler handler,
                                RoutedSnapshot snapshot) {
        try {
            Received received = InternalAccess.received(snapshot.sourceRid(),
              snapshot.spotRid(), snapshot.parts(), snapshot.requestSeq(),
              snapshot.requestSeq() != 0L,
              snapshot.requestSeq() == 0L ? null : (replyParts, sendFlags) -> {
                  if (snapshot.spotRid() != null) {
                      replyToSpot(snapshot.sourceRid(), snapshot.spotRid(),
                        snapshot.requestSeq(), replyParts, sendFlags);
                  } else {
                      replyToRouter(snapshot.sourceRid(), snapshot.requestSeq(),
                        replyParts, sendFlags);
                  }
              });
            InternalAccess.enterCallback();
            try (received) {
                handler.onMessage(snapshot.sourceRid(), snapshot.spotRid(),
                  snapshot.requestSeq(), received);
            } finally {
                InternalAccess.leaveCallback();
            }
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
        }
    }

    private void dispatchEvent(SpotDispatchEventHandler handler,
                               SpotDispatchInfo info) {
        InternalAccess.enterCallback();
        try {
            handler.onEvent(info);
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
        } finally {
            InternalAccess.leaveCallback();
        }
    }

    private SpotDispatchInfo decodeDispatchInfo(MemorySegment info) {
        if (info == null || info.address() == 0) {
            throw new IllegalArgumentException("dispatch info must not be null");
        }
        info = info.reinterpret(NativeLayouts.SPOT_DISPATCH_INFO_LAYOUT.byteSize());
        SpotDispatchEvent event = EnumCodecs.spotDispatchEventFromValue(info.get(
          ValueLayout.JAVA_INT, NativeLayouts.SPOT_DISPATCH_INFO_EVENT_OFFSET));
        SpotDispatchSubjectKind subjectKind = EnumCodecs.spotDispatchSubjectKindFromValue(
          info.get(ValueLayout.JAVA_INT,
            NativeLayouts.SPOT_DISPATCH_INFO_SUBJECT_KIND_OFFSET));
        MemorySegment subject = info.get(ValueLayout.ADDRESS,
          NativeLayouts.SPOT_DISPATCH_INFO_SUBJECT_OFFSET);
        if (event == SpotDispatchEvent.ACTOR_READABLE
            && subjectKind == SpotDispatchSubjectKind.ACTOR
            && subject != null && subject.address() != 0) {
            return InternalAccess.spotDispatchInfo(event, subjectKind, subject,
              drainActorParts(subject));
        }
        if (event == SpotDispatchEvent.TIMER_READABLE
            && subjectKind == SpotDispatchSubjectKind.TIMER
            && subject != null && subject.address() != 0) {
            return InternalAccess.spotDispatchInfo(event, subjectKind, subject,
              InternalAccess.timerFromBorrowedHandle(subject), null, List.of());
        }
        return InternalAccess.spotDispatchInfo(event, subjectKind, subject);
    }

    private List<ActorPart> drainActorParts(MemorySegment actor) {
        MemorySegment node = spot.ownerNodeHandleInternal();
        if (node == null || node.address() == 0) {
            return List.of();
        }
        ArrayList<ActorPart> parts = new ArrayList<>();
        try (Arena arena = Arena.ofConfined()) {
            while (true) {
                MemorySegment infoOut = arena.allocate(
                  NativeLayouts.ACTOR_RECV_INFO_LAYOUT);
                MemorySegment hasMoreOut = arena.allocate(ValueLayout.JAVA_INT);
                Message message = new Message();
                boolean success = false;
                try {
                    int rc = Native.spotNodeActorRecvPart(node, actor, infoOut,
                      InternalAccess.messageNativeHandle(message), hasMoreOut,
                      RecvFlags.DONT_WAIT.value());
                    if (rc != 0) {
                        message.close();
                        if (rc == RecvResult.NO_DATA.value()) {
                            break;
                        }
                        throw new RecvException(RecvResult.fromValue(rc));
                    }
                    boolean hasMore =
                      hasMoreOut.get(ValueLayout.JAVA_INT, 0) != 0;
                    InternalAccess.messageFinishReceive(message, hasMore);
                    success = true;
                    parts.add(new ActorPart(
                      ActorInterop.actorRecvInfoFromNative(infoOut), message,
                      hasMore));
                } finally {
                    if (!success) {
                        try {
                            message.close();
                        } catch (RuntimeException ignored) {
                        }
                    }
                }
            }
        }
        return List.copyOf(parts);
    }

    private CompletableFuture<List<Message>> requestViaNative(List<Message> parts,
                                                              Duration timeout,
                                                              NativeRequest request) {
        long timeoutMs = timeoutMillis(timeout);
        long requestId = NEXT_REQUEST_ID.getAndIncrement();
        CompletableFuture<Received> future = registerPending(requestId, timeoutMs);
        try (Arena arena = Arena.ofConfined()) {
            int rc = request.invoke(arena, parts, requestId, timeoutMs);
            if (rc != 0) {
                future.cancel(false);
                throw new SubmitException(SubmitResult.fromValue(rc));
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
        requestViaNative(parts, timeout, request)
          .whenComplete((reply, error) -> callback.accept(error == null
            ? RequestResult.OK : requestResult(error),
            reply == null ? List.of() : reply));
    }

    private void sendViaNative(List<Message> parts, NativeSubmit submitter) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = submitter.invoke(arena, parts);
            if (rc != 0) {
                throw new SubmitException(SubmitResult.fromValue(rc));
            }
        }
    }

    private void submitSpotReplySpot(RoutingId destNodeRid,
                                     RoutingId destSpotRid,
                                     long requestSeq,
                                     List<Message> payload) {
        for (int i = 0; i < payload.size(); i++) {
            int partFlag = i + 1 < payload.size()
                ? Native.PART_MORE : Native.PART_FINAL;
            while (true) {
                int rc = spotReplySpotPartOnce(destNodeRid, destSpotRid,
                    requestSeq, payload.get(i), partFlag);
                if (rc == 0)
                    break;
                int errno = Native.errno();
                if (errno == 4)
                    continue;
                throw submitFailure("zlink_spot_reply_spot_part");
            }
        }
    }

    private void submitSpotSendSpot(RoutingId destNodeRid,
                                    RoutingId destSpotRid,
                                    List<Message> payload,
                                    boolean nonBlocking) {
        for (int i = 0; i < payload.size(); i++) {
            int partFlag = i + 1 < payload.size()
                ? Native.PART_MORE : Native.PART_FINAL;
            while (true) {
                int rc = spotSendSpotPartOnce(destNodeRid, destSpotRid,
                    payload.get(i), nonBlocking ? SEND_DONTWAIT : 0, partFlag);
                if (rc == 0)
                    break;
                int errno = Native.errno();
                if (errno == ERRNO_EINTR)
                    continue;
                throw submitFailure("zlink_spot_send_spot_part");
            }
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
        for (int i = 0; i < payload.size(); i++) {
            int partFlag = i + 1 < payload.size()
                ? Native.PART_MORE : Native.PART_FINAL;
            while (true) {
                int rc = spotRequestSpotPartOnce(destNodeRid, destSpotRid,
                  payload.get(i), i + 1 < payload.size() ? MemorySegment.NULL
                    : handler,
                  i + 1 < payload.size() ? MemorySegment.NULL : userData,
                  flags, partFlag, i + 1 < payload.size() ? 0 : timeoutMs);
                if (rc == 0)
                    break;
                int errno = Native.errno();
                if (errno == ERRNO_EINTR)
                    continue;
                throw submitFailure("zlink_spot_request_spot_part");
            }
        }
    }

    private void submitSpotRequestRouter(RoutingId peerRid,
                                         List<Message> payload,
                                         MemorySegment handler,
                                         MemorySegment userData,
                                         int flags,
                                         int timeoutMs) {
        Objects.requireNonNull(peerRid, "peerRid");
        for (int i = 0; i < payload.size(); i++) {
            int partFlag = i + 1 < payload.size()
                ? Native.PART_MORE : Native.PART_FINAL;
            while (true) {
                int rc = spotRequestRouterPartOnce(peerRid, payload.get(i),
                  i + 1 < payload.size() ? MemorySegment.NULL : handler,
                  i + 1 < payload.size() ? MemorySegment.NULL : userData,
                  flags, partFlag, i + 1 < payload.size() ? 0 : timeoutMs);
                if (rc == 0)
                    break;
                int errno = Native.errno();
                if (errno == ERRNO_EINTR)
                    continue;
                throw submitFailure("zlink_spot_request_router_part");
            }
        }
    }

    private void submitSpotReplyRouter(RoutingId peerRid, long requestSeq,
                                       List<Message> payload) {
        for (int i = 0; i < payload.size(); i++) {
            int partFlag = i + 1 < payload.size()
                ? Native.PART_MORE : Native.PART_FINAL;
            while (true) {
                int rc = spotReplyRouterPartOnce(peerRid, requestSeq,
                    payload.get(i), partFlag);
                if (rc == 0)
                    break;
                int errno = Native.errno();
                if (errno == 4)
                    continue;
                throw submitFailure("zlink_spot_reply_router_part");
            }
        }
    }

    private int spotReplySpotPartOnce(RoutingId destNodeRid,
                                      RoutingId destSpotRid,
                                      long requestSeq,
                                      Message part,
                                      int partFlag) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nodeRid = nativeRoutingId(arena, destNodeRid);
            MemorySegment spotRid = nativeRoutingId(arena, destSpotRid);
            MemorySegment nativeMsg = arena.allocate(NativeLayouts.MSG_LAYOUT);
            InternalAccess.messageCopyTo(part, nativeMsg);
            return Native.spotReplySpotPart(handle(), nodeRid, spotRid,
                requestSeq, nativeMsg, partFlag);
        }
    }

    private int spotSendSpotPartOnce(RoutingId destNodeRid,
                                     RoutingId destSpotRid,
                                     Message part,
                                     int flags,
                                     int partFlag) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nodeRid = nativeRoutingId(arena, destNodeRid);
            MemorySegment spotRid = nativeRoutingId(arena, destSpotRid);
            MemorySegment nativeMsg = arena.allocate(NativeLayouts.MSG_LAYOUT);
            InternalAccess.messageCopyTo(part, nativeMsg);
            return Native.spotSendSpotPart(handle(), nodeRid, spotRid,
                nativeMsg, flags, partFlag);
        }
    }

    private int spotRequestSpotPartOnce(RoutingId destNodeRid,
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
            InternalAccess.messageCopyTo(part, nativeMsg);
            return Native.spotRequestSpotPart(handle(), nodeRid, spotRid,
              nativeMsg, handler, userData, flags, partFlag, timeoutMs);
        }
    }

    private int spotRequestRouterPartOnce(RoutingId peerRid,
                                          Message part,
                                          MemorySegment handler,
                                          MemorySegment userData,
                                          int flags,
                                          int partFlag,
                                          int timeoutMs) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeRid = nativeRoutingId(arena, peerRid);
            MemorySegment nativeMsg = arena.allocate(NativeLayouts.MSG_LAYOUT);
            InternalAccess.messageCopyTo(part, nativeMsg);
            return Native.spotRequestRouterPart(handle(), nativeRid,
              nativeMsg, handler, userData, flags, partFlag, timeoutMs);
        }
    }

    private int spotReplyRouterPartOnce(RoutingId peerRid,
                                        long requestSeq,
                                        Message part,
                                        int partFlag) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeRid = nativeRoutingId(arena, peerRid);
            MemorySegment nativeMsg = arena.allocate(NativeLayouts.MSG_LAYOUT);
            InternalAccess.messageCopyTo(part, nativeMsg);
            return Native.spotReplyRouterPart(handle(), nativeRid,
                requestSeq, nativeMsg, partFlag);
        }
    }

    private SubmitException submitFailure(String apiName) {
        int errno = Native.errno();
        if (errno == 11 || errno == 10035) {
            return new SubmitException(SubmitResult.BACKPRESSURED, errno);
        }
        if (errno == 107 || errno == 10057 || errno == 113 || errno == 10065) {
            return new SubmitException(SubmitResult.NOT_CONNECTED, errno);
        }
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
                if (errno == 4)
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
        ensureNoCallbackFailure();
    }

    private void ensureNoCallbackFailure() {
        RuntimeException failure = callbackFailure;
        if (failure != null) {
            throw failure;
        }
    }

    private ExecutorService ensureCallbackExecutor(String threadName) {
        ExecutorService executor = callbackExecutor;
        if (executor != null) {
            return executor;
        }
        executor = Executors.newSingleThreadExecutor(runnable -> {
            Thread thread = new Thread(runnable, threadName);
            thread.setDaemon(true);
            return thread;
        });
        callbackExecutor = executor;
        return executor;
    }

    private void recordCallbackFailure(RuntimeException failure) {
        callbackFailure = failure;
        Thread current = Thread.currentThread();
        Thread.UncaughtExceptionHandler uncaught =
          current.getUncaughtExceptionHandler();
        if (uncaught != null) {
            uncaught.uncaughtException(current, failure);
        }
    }

    private static CompletableFuture<Received> registerPending(long requestId,
                                                               long timeoutMs) {
        CompletableFuture<Received> future = new CompletableFuture<>();
        PENDING.put(requestId, future);
        ScheduledFuture<?> timeout = REQUEST_TIMEOUTS.schedule(() -> {
            if (PENDING.remove(requestId, future)) {
                future.completeExceptionally(new TimeoutException(
                    "request timed out"));
            }
        }, timeoutMs, TimeUnit.MILLISECONDS);
        future.whenComplete((ignored, error) -> timeout.cancel(false));
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
                    future.completeExceptionally(new RequestException(
                        RequestResult.fromValue(result), result));
                }
                return;
            }
            Message[] frames = InternalAccess.messageFromOwnedMsgVectorShared(
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
            NativeMsg.multipartClose(parts, partCount);
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
            throw new SubmitException(SubmitResult.NOT_SUPPORTED);
        }
    }

    private static RoutingId readRoutingId(MemorySegment nativeRid) {
        if (nativeRid == null || nativeRid.address() == 0) {
            return null;
        }
        int size = nativeRid.get(ValueLayout.JAVA_BYTE,
          NativeLayouts.ROUTING_ID_SIZE_OFFSET) & 0xFF;
        if (size == 0) {
            return null;
        }
        byte[] value = new byte[size];
        MemorySegment.copy(nativeRid, NativeLayouts.ROUTING_ID_DATA_OFFSET,
          MemorySegment.ofArray(value), 0, size);
        return InternalAccess.routingIdFromTrusted(value);
    }

    private static RoutingId readRoutingIdOut(MemorySegment nativeRidOut) {
        MemorySegment nativeRid = nativeRidOut.get(ValueLayout.ADDRESS, 0);
        if (nativeRid.address() != 0) {
            nativeRid = nativeRid.reinterpret(
              NativeLayouts.ROUTING_ID_LAYOUT.byteSize());
        }
        return readRoutingId(nativeRid);
    }

    private static long timeoutMillis(Duration timeout) {
        return timeout == null ? 5_000L : Math.max(1L, timeout.toMillis());
    }

    private static int toTimeoutInt(long timeoutMs) {
        if (timeoutMs <= 1L) {
            return 1;
        }
        return timeoutMs >= Integer.MAX_VALUE ? Integer.MAX_VALUE : (int) timeoutMs;
    }

    private static Throwable unwrap(Throwable error) {
        if (error instanceof java.util.concurrent.CompletionException
            && error.getCause() != null) {
            return error.getCause();
        }
        return error;
    }

    private static RequestResult requestResult(Throwable error) {
        Throwable cause = unwrap(error);
        if (cause instanceof RequestException requestException) {
            return requestException.getResult();
        }
        if (cause instanceof TimeoutException) {
            return RequestResult.TIMED_OUT;
        }
        return RequestResult.PROTOCOL_ERROR;
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

    private static void closeArena(Arena arena) {
        if (arena != null && arena.scope().isAlive()) {
            arena.close();
        }
    }

    private static void shutdownExecutor(ExecutorService executor) {
        if (executor != null) {
            executor.shutdown();
        }
    }

    @FunctionalInterface
    private interface NativeRequest {
        int invoke(Arena arena, List<Message> payload, long requestId,
                   long timeoutMs);
    }

    @FunctionalInterface
    private interface NativeSubmit {
        int invoke(Arena arena, List<Message> payload);
    }

    private static final class TimeoutThreadFactory implements ThreadFactory {
        @Override
        public Thread newThread(Runnable runnable) {
            Thread thread = new Thread(runnable, "zlink-spot-request-timeout");
            thread.setDaemon(true);
            return thread;
        }
    }

    private record RoutedSnapshot(RoutingId sourceRid, RoutingId spotRid,
                                  long requestSeq, Message[] parts) {
    }
}
