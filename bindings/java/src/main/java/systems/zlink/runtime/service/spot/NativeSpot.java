/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.spot;

import systems.zlink.contracts.errors.ZlinkConfigException;
import systems.zlink.contracts.errors.ConfigResult;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.errors.ZlinkRecvException;
import systems.zlink.contracts.sockets.RecvFlags;
import systems.zlink.contracts.sockets.RecvResult;
import systems.zlink.contracts.sockets.RequestCallback;
import systems.zlink.contracts.errors.ZlinkRequestException;
import systems.zlink.contracts.sockets.RequestResult;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.sockets.SendReadyHandler;
import systems.zlink.contracts.sockets.Socket;
import systems.zlink.contracts.sockets.SpotDispatchEvent;
import systems.zlink.contracts.sockets.SpotDispatchEventHandler;
import systems.zlink.contracts.sockets.SpotDispatchInfo;
import systems.zlink.contracts.sockets.SpotDispatchSubjectKind;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.sockets.SubmitResult;
import systems.zlink.contracts.messaging.SubscriptionEntry;
import systems.zlink.contracts.messaging.SubscriptionEvent;
import systems.zlink.contracts.messaging.TopicMessage;
import systems.zlink.internal.ContractAccess;
import systems.zlink.contracts.service.spot.*;
import systems.zlink.runtime.nativeapi.ActorInterop;
import systems.zlink.runtime.nativeapi.Native;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.nativeapi.MessagePartsBuffer;
import systems.zlink.runtime.nativeapi.NativeHelpers;
import systems.zlink.runtime.nativeapi.NativeLayouts;
import systems.zlink.runtime.nativeapi.NativeMessage;
import systems.zlink.runtime.nativeapi.NativeSubmitErrors;
import systems.zlink.runtime.nativeapi.RequestProgressPump;
import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentMap;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicLong;
import java.util.function.BiConsumer;

/**
 * Unified spot service handle aligned to the current core publish/subscribe
 * service model.
 */
public final class NativeSpot implements Spot {
    private static final long MSG_SIZE = NativeLayouts.MESSAGE_LAYOUT.byteSize();
    private static final int TOPIC_CAPACITY = 256;
    private static final int TOPIC_CACHE_LIMIT = 1024;
    private static final int TOPIC_SCRATCH_INITIAL_CAPACITY = 64;
    private static final int ERRNO_EINTR = 4;
    private static final int ERRNO_ENOENT = 2;
    private static final int ERRNO_EAGAIN = 11;
    private static final int ERRNO_EINVAL = 22;
    private static final int ERRNO_EWOULDBLOCK_WIN = 10035;
    private static final int ERRNO_ENOTCONN = 107;
    private static final int ERRNO_ENOTCONN_WIN = 10057;
    private static final int ERRNO_EHOSTUNREACH = 113;
    private static final int ERRNO_EHOSTUNREACH_WIN = 10065;
    private static final int ERRNO_ETIMEDOUT = 110;
    private static final int ERRNO_ETIMEDOUT_WIN = 10060;
    private static final int RECV_BLOCKING = 0;
    private static final int RECV_DONTWAIT = 1;
    private static final int SEND_DONTWAIT = 1;
    private static final Linker LINKER = Linker.nativeLinker();
    private static final FunctionDescriptor FD_SUBSCRIBE_CALLBACK =
      FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.ADDRESS,
        ValueLayout.JAVA_LONG, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG,
        ValueLayout.ADDRESS);
    private static final FunctionDescriptor FD_SEND_READY_CALLBACK =
      FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.ADDRESS);
    private static final FunctionDescriptor FD_REPLY_CALLBACK =
      FunctionDescriptor.ofVoid(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.JAVA_LONG, ValueLayout.ADDRESS);
    private static final Arena REQUEST_CALLBACK_ARENA = Arena.ofShared();
    private static final MemorySegment REPLY_CALLBACK;
    private static final AtomicLong NEXT_REQUEST_ID = new AtomicLong(1L);
    private static final ConcurrentMap<Long, CompletableFuture<Received>> PENDING =
      new ConcurrentHashMap<>();
    private static final ScheduledExecutorService REQUEST_TIMEOUTS =
      Executors.newSingleThreadScheduledExecutor(runnable -> {
          Thread thread = new Thread(runnable, "zlink-spot-request-timeout");
          thread.setDaemon(true);
          return thread;
      });

    static {
        try {
            REPLY_CALLBACK = LINKER.upcallStub(MethodHandles.lookup().findStatic(
              NativeSpot.class, "handleReplyCallback",
              MethodType.methodType(void.class, int.class, MemorySegment.class,
                long.class, MemorySegment.class)), FD_REPLY_CALLBACK,
              REQUEST_CALLBACK_ARENA);
        } catch (ReflectiveOperationException ex) {
            throw new ExceptionInInitializerError(ex);
        }
    }

    private final ConcurrentHashMap<String, MemorySegment> topicCache =
      new ConcurrentHashMap<>();
    private final Arena topicCacheArena = Arena.ofShared();
    private final ThreadLocal<MemorySegment> topicScratch =
      ThreadLocal.withInitial(() -> Arena.ofAuto().allocate(
        TOPIC_SCRATCH_INITIAL_CAPACITY));
    private final ThreadLocal<Integer> topicScratchCapacity =
      ThreadLocal.withInitial(() -> TOPIC_SCRATCH_INITIAL_CAPACITY);

    // Subscribe hot path scratch (parity with Socket RecvScratch): persistent
    // off-heap out-params + a last-topic cache so a steady single-part stream
    // on one constant topic does not re-allocate an Arena or re-decode a
    // String per message. The original receiveTopicMessage path allocated an
    // Arena (5 native segments) + Message[] + new String + new TopicMessage
    // per delivered message, which capped MULTI_SPOT per-thread throughput.
    private static final class SpotRecvScratch {
        final Arena arena = Arena.ofAuto();
        final MemorySegment ridOut = arena.allocate(ValueLayout.ADDRESS);
        final MemorySegment topicOut = arena.allocate(TOPIC_CAPACITY);
        final MemorySegment topicLenOut =
          arena.allocate(ValueLayout.JAVA_LONG);
        final MemorySegment hasMoreOut = arena.allocate(ValueLayout.JAVA_INT);
        byte[] cachedTopicBytes;
        String cachedTopicString = "";
        byte[] cachedRoutingIdBytes;
        RoutingId cachedRoutingId;
    }

    private static final class SpotSendScratch {
        final Arena arena = Arena.ofAuto();
        final MemorySegment nativeMsg =
          arena.allocate(NativeLayouts.MESSAGE_LAYOUT);
    }

    private final ThreadLocal<SpotRecvScratch> spotRecvScratch =
      ThreadLocal.withInitial(SpotRecvScratch::new);
    private final ThreadLocal<SpotSendScratch> spotSendScratch =
      ThreadLocal.withInitial(SpotSendScratch::new);

    @FunctionalInterface
    private interface SubscribeCallback {
        void onMessage(RoutingId routingId, String topicId, Received received);
    }

    private MemorySegment handle;
    private SubscribeCallback subscribeHandler;
    private SendReadyHandler sendReadyHandler;
    private Arena subscribeCallbackArena;
    private Arena sendReadyCallbackArena;
    private MemorySegment subscribeCallbackStub = MemorySegment.NULL;
    private MemorySegment sendReadyCallbackStub = MemorySegment.NULL;
    private volatile ExecutorService callbackExecutor;
    private volatile RuntimeException callbackFailure;
    private final SpotRoutedSupport routedSupport;
    private final SpotNode ownerNode;
    private final SpotOptions options;

    static {
        InternalAccess.register(new InternalAccess.SpotAccess() {
            @Override
            public MemorySegment handle(Spot spot) {
                return ((NativeSpot) spot).handle();
            }

            @Override
            public MemorySegment ownerNodeHandle(Spot spot) {
                return ((NativeSpot) spot).ownerNodeHandleInternal();
            }

            @Override
            public Spot createOwned(SpotNode node) {
                return new NativeSpot(node);
            }

            @Override
            public Spot adoptOwned(SpotNode node, MemorySegment handle) {
                return new NativeSpot(node, handle);
            }

            @Override
            public boolean requestToSpotPart(Spot spot, RoutingId destNodeRid,
                                             RoutingId destSpotRid,
                                             Message part,
                                             RequestCallback callback,
                                             SendFlags flags,
                                             Duration timeout) {
                return ((NativeSpot) spot).requestToSpotPart(destNodeRid,
                    destSpotRid, part, callback, flags, timeout);
            }
        });
    }

    /** Creates a unified spot facade bound to the supplied node. */
    NativeSpot(SpotNode node) {
        Objects.requireNonNull(node, "node");
        MemorySegment nativeHandle = Native.spotNew(InternalAccess.spotNodeHandle(node));
        if (nativeHandle == null || nativeHandle.address() == 0)
            throw InternalAccess.zlinkExceptionFromLastError("zlink_spot_new");
        this.ownerNode = node;
        this.handle = nativeHandle;
        this.routedSupport = new SpotRoutedSupport(this);
        this.options = new SpotOptions(this);
    }

    NativeSpot(SpotNode node, MemorySegment handle) {
        Objects.requireNonNull(node, "node");
        Objects.requireNonNull(handle, "handle");
        if (handle.address() == 0)
            throw new IllegalArgumentException("spot handle must not be null");
        this.ownerNode = node;
        this.handle = handle;
        this.routedSupport = new SpotRoutedSupport(this);
        this.options = new SpotOptions(this);
    }

    NativeSpot(MemorySegment handle) {
        Objects.requireNonNull(handle, "handle");
        if (handle.address() == 0)
            throw new IllegalArgumentException("spot handle must not be null");
        this.ownerNode = null;
        this.handle = handle;
        this.routedSupport = new SpotRoutedSupport(this);
        this.options = new SpotOptions(this);
    }

    MemorySegment handle() {
        return handle;
    }

    MemorySegment ownerNodeHandleInternal() {
        return ownerNode == null ? MemorySegment.NULL
            : InternalAccess.spotNodeHandle(ownerNode);
    }

    SpotOptions options() {
        ensureOpen();
        return options;
    }

    /** Sets the logical routing id for this spot. */
    public void setRoutingId(RoutingId rid) {
        Objects.requireNonNull(rid, "rid");
        ensureOpen();
        byte[] value = InternalAccess.routingIdTrustedBytes(rid);
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeValue = arena.allocate(value.length);
            if (value.length > 0) {
                MemorySegment.copy(MemorySegment.ofArray(value), 0, nativeValue,
                  0, value.length);
            }
            int rc = Native.setRoutingId(handle, nativeValue, value.length);
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError("zlink_set_routing_id");
            }
        }
    }

    /** Returns the current logical routing id for this spot. */
    public RoutingId getRoutingId() {
        ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment outRid = arena.allocate(NativeLayouts.ROUTING_ID_LAYOUT);
            int rc = Native.getRoutingId(handle, outRid);
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError("zlink_get_routing_id");
            }
            return readRoutingId(outRid);
        }
    }

    public Duration requestTimeout() {
        return options.requestTimeout();
    }

    public void requestTimeout(Duration value) {
        options.requestTimeout(value);
    }

    public SendOperation publish(String topicId) {
        return new SendBuilder(
          (part, flags) -> publish(topicId, part, flags),
          (parts, flags) -> publish(topicId, parts, flags));
    }

    public SendOperation sendToChannel(String channelName) {
        return new SendBuilder(
          (part, flags) -> sendToChannel(channelName, part, flags),
          (parts, flags) -> sendToChannel(channelName, parts, flags));
    }

    public SendOperation sendToSpot(RoutingId destNodeRid, RoutingId destSpotRid) {
        return new SendBuilder(
          (part, flags) -> sendToSpot(destNodeRid, destSpotRid, part, flags),
          (parts, flags) -> sendToSpot(destNodeRid, destSpotRid, parts, flags));
    }

    public RequestOperation requestToChannel(String channelName) {
        return new RequestBuilder((parts, timeout, flags) ->
            requestToChannel(channelName, parts, timeout),
          (parts, callback, flags, timeout) ->
            requestToChannel(channelName, parts, callback::onComplete, flags,
              timeout));
    }

    public RequestOperation requestToSpot(RoutingId destNodeRid,
                                   RoutingId destSpotRid) {
        return new RequestBuilder((parts, timeout, flags) ->
            routedSupport.requestToSpot(destNodeRid, destSpotRid, parts,
              timeout, flags),
          (parts, callback, flags, timeout) ->
            routedSupport.requestToSpot(destNodeRid, destSpotRid, parts,
              callback::onComplete, flags, timeout));
    }

    boolean requestToSpotPart(RoutingId destNodeRid,
                              RoutingId destSpotRid,
                              Message part,
                              RequestCallback callback,
                              SendFlags flags,
                              Duration timeout) {
        return routedSupport.requestToSpot(destNodeRid, destSpotRid,
          List.of(Objects.requireNonNull(part, "part")),
          Objects.requireNonNull(callback, "callback")::onComplete,
          Objects.requireNonNull(flags, "flags"),
          Objects.requireNonNull(timeout, "timeout"));
    }

    public RequestOperation requestToRouter(RoutingId peerRid) {
        return new RequestBuilder((parts, timeout, flags) ->
            routedSupport.requestToRouter(peerRid, parts, timeout, flags),
          (parts, callback, flags, timeout) ->
            routedSupport.requestToRouter(peerRid, parts,
              callback::onComplete, flags, timeout));
    }

    public ReplyOperation replyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                               long requestSeq) {
        return new ReplyBuilder((parts, flags) ->
            replyToSpot(destNodeRid, destSpotRid, requestSeq, parts, flags));
    }

    public ReplyOperation replyToRouter(RoutingId peerRid, long requestSeq) {
        return new ReplyBuilder((parts, flags) ->
            replyToRouter(peerRid, requestSeq, parts, flags));
    }

    /** Publishes one payload part through the owning Spot topic plane. */
    boolean publish(String topicId, Message part) {
        Objects.requireNonNull(part, "part");
        return publish(topicId, part, SendFlags.NONE);
    }

    boolean publish(String topicId, Message part, SendFlags flags) {
        Objects.requireNonNull(flags, "flags");
        try {
            return publishInternal(topicId, part,
              flags == SendFlags.DONT_WAIT);
        } catch (ZlinkSubmitException ex) {
            if (flags == SendFlags.DONT_WAIT
                && ex.getResult() == SubmitResult.BACKPRESSURED) {
                return false;
            }
            throw ex;
        }
    }

    /** Publishes a multipart payload through the owning Spot topic plane. */
    boolean publish(String topicId, List<Message> parts) {
        return publish(topicId, parts, SendFlags.NONE);
    }

    boolean publish(String topicId, List<Message> parts, SendFlags flags) {
        Objects.requireNonNull(flags, "flags");
        try {
            return publishInternal(topicId, parts,
              flags == SendFlags.DONT_WAIT);
        } catch (ZlinkSubmitException ex) {
            if (flags == SendFlags.DONT_WAIT
                && ex.getResult() == SubmitResult.BACKPRESSURED) {
                return false;
            }
            throw ex;
        }
    }

    boolean sendToChannel(String channelName, Message part) {
        return sendToChannel(channelName, part, SendFlags.NONE);
    }

    boolean sendToChannel(String channelName, Message part, SendFlags flags) {
        Objects.requireNonNull(part, "part");
        return sendToChannel(channelName, List.of(part), flags);
    }

    boolean sendToChannel(String channelName, List<Message> parts) {
        return sendToChannel(channelName, parts, SendFlags.NONE);
    }

    boolean sendToChannel(String channelName, List<Message> parts,
                            SendFlags flags) {
        Objects.requireNonNull(flags, "flags");
        try {
            return sendChannelInternal(channelName, parts,
              flags == SendFlags.DONT_WAIT);
        } catch (ZlinkSubmitException ex) {
            if (flags == SendFlags.DONT_WAIT
                && ex.getResult() == SubmitResult.BACKPRESSURED) {
                return false;
            }
            throw ex;
        }
    }

    boolean sendToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                              Message part) {
        return sendToSpot(destNodeRid, destSpotRid, List.of(part),
          SendFlags.NONE);
    }

    boolean sendToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                              Message part, SendFlags flags) {
        Objects.requireNonNull(part, "part");
        return sendToSpot(destNodeRid, destSpotRid, List.of(part), flags);
    }

    boolean sendToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                              List<Message> parts) {
        return sendToSpot(destNodeRid, destSpotRid, parts, SendFlags.NONE);
    }

    boolean sendToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                              List<Message> parts, SendFlags flags) {
        Objects.requireNonNull(flags, "flags");
        try {
            return routedSupport.sendToSpot(destNodeRid, destSpotRid, parts,
              flags);
        } catch (ZlinkSubmitException ex) {
            if (flags == SendFlags.DONT_WAIT
                && ex.getResult() == SubmitResult.BACKPRESSURED) {
                return false;
            }
            throw ex;
        }
    }

    CompletableFuture<List<Message>> requestToChannel(String channelName,
                                                           Message part) {
        return requestToChannel(channelName, List.of(part));
    }

    private CompletableFuture<List<Message>> requestToChannel(String channelName,
                                                            Message part,
                                                            SendFlags flags) {
        return requestToChannel(channelName, List.of(part), flags);
    }

    CompletableFuture<List<Message>> requestToChannel(String channelName,
                                                           Message part,
                                                           Duration timeout) {
        return requestToChannel(channelName, List.of(part), timeout);
    }

    private CompletableFuture<List<Message>> requestToChannel(String channelName,
                                                            Message part,
                                                            SendFlags flags,
                                                            Duration timeout) {
        return requestToChannel(channelName, List.of(part), flags, timeout);
    }

    CompletableFuture<List<Message>> requestToChannel(String channelName,
                                                           List<Message> parts) {
        return requestToChannel(channelName, parts, SendFlags.NONE);
    }

    private CompletableFuture<List<Message>> requestToChannel(String channelName,
                                                            List<Message> parts,
                                                            SendFlags flags) {
        return requestToChannel(channelName, parts, flags,
          Duration.ofMillis(5_000L));
    }

    CompletableFuture<List<Message>> requestToChannel(String channelName,
                                                           List<Message> parts,
                                                           Duration timeout) {
        return requestToChannel(channelName, parts, SendFlags.NONE, timeout);
    }

    private CompletableFuture<List<Message>> requestToChannel(String channelName,
                                                            List<Message> parts,
                                                            SendFlags flags,
                                                            Duration timeout) {
        Objects.requireNonNull(flags, "flags");
        return requestChannelInternal(channelName, parts, timeout, flags);
    }

    boolean requestToChannel(String channelName, Message part,
                                  BiConsumer<RequestResult, List<Message>> callback) {
        return requestToChannel(channelName, List.of(part), callback,
          SendFlags.NONE, Duration.ofMillis(5_000L));
    }

    boolean requestToChannel(String channelName, Message part,
                                  BiConsumer<RequestResult, List<Message>> callback,
                                  SendFlags flags) {
        return requestToChannel(channelName, List.of(part), callback, flags,
          Duration.ofMillis(5_000L));
    }

    boolean requestToChannel(String channelName, Message part,
                                  BiConsumer<RequestResult, List<Message>> callback,
                                  Duration timeout) {
        return requestToChannel(channelName, List.of(part), callback,
          SendFlags.NONE, timeout);
    }

    boolean requestToChannel(String channelName, Message part,
                                  BiConsumer<RequestResult, List<Message>> callback,
                                  SendFlags flags, Duration timeout) {
        return requestToChannel(channelName, List.of(part), callback, flags, timeout);
    }

    boolean requestToChannel(String channelName, List<Message> parts,
                                  BiConsumer<RequestResult, List<Message>> callback) {
        return requestToChannel(channelName, parts, callback, SendFlags.NONE,
          Duration.ofMillis(5_000L));
    }

    boolean requestToChannel(String channelName, List<Message> parts,
                                  BiConsumer<RequestResult, List<Message>> callback,
                                  SendFlags flags) {
        return requestToChannel(channelName, parts, callback, flags,
          Duration.ofMillis(5_000L));
    }

    boolean requestToChannel(String channelName, List<Message> parts,
                                  BiConsumer<RequestResult, List<Message>> callback,
                                  Duration timeout) {
        return requestToChannel(channelName, parts, callback, SendFlags.NONE,
          timeout);
    }

    boolean requestToChannel(String channelName, List<Message> parts,
                                  BiConsumer<RequestResult, List<Message>> callback,
                                  SendFlags flags, Duration timeout) {
        Objects.requireNonNull(callback, "callback");
        try {
            requestChannelInternal(channelName, parts, timeout, flags)
              .whenComplete((reply, error) -> {
                  List<Message> response = List.of();
                  if (reply != null) {
                      response = reply;
                  }
                  callback.accept(error == null ? RequestResult.OK
                      : requestResult(error), response);
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

    private boolean publishInternal(String topicId, Message part,
                                 boolean nonBlocking) {
        Objects.requireNonNull(topicId, "topicId");
        Objects.requireNonNull(part, "part");
        if (!nonBlocking && InternalAccess.inCallback()) {
            throw new IllegalStateException(
                "blocking publish is not supported from callback context; use SendFlags.DONT_WAIT");
        }
        while (true) {
            int rc = spotPublishPartOnce(topicId, part,
                nonBlocking ? SEND_DONTWAIT : 0, Native.PART_FINAL);
            if (rc == 0)
                return true;
            int errno = Native.errno();
            if (errno == ERRNO_EINTR)
                continue;
            throw submitFailure("zlink_spot_publish_part");
        }
    }

    private boolean publishInternal(String topicId, List<Message> parts,
                                 boolean nonBlocking) {
        validateMessages(parts, "parts");
        if (!nonBlocking && InternalAccess.inCallback()) {
            throw new IllegalStateException(
                "blocking publish is not supported from callback context; use SendFlags.DONT_WAIT");
        }
        MemorySegment topic = topicCString(topicId);
        try (Arena arena = Arena.ofConfined()) {
            for (int i = 0; i < parts.size(); i++) {
                int partFlag = i + 1 < parts.size()
                    ? Native.PART_MORE : Native.PART_FINAL;
                while (true) {
                    int rc = spotPublishPartOnce(topic,
                        Objects.requireNonNull(parts.get(i), "parts[" + i + "]"),
                        nonBlocking ? SEND_DONTWAIT : 0, partFlag, arena);
                    if (rc == 0)
                        break;
                    int errno = Native.errno();
                    if (errno == ERRNO_EINTR)
                        continue;
                    throw submitFailure("zlink_spot_publish_part");
                }
            }
        }
        return true;
    }

    private boolean sendChannelInternal(String channelName, List<Message> parts,
                                     boolean nonBlocking) {
        validateMessages(parts, "parts");
        if (!nonBlocking && InternalAccess.inCallback()) {
            throw new IllegalStateException(
                "blocking sendToChannel is not supported from callback context; use non-blocking send");
        }
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment service = NativeHelpers.toCString(arena,
              requireChannelName(channelName));
            for (int i = 0; i < parts.size(); i++) {
                int partFlag = i + 1 < parts.size()
                    ? Native.PART_MORE : Native.PART_FINAL;
                while (true) {
                    int rc = spotSendChannelPartOnce(service,
                        Objects.requireNonNull(parts.get(i), "parts[" + i + "]"),
                        nonBlocking ? SEND_DONTWAIT : 0, partFlag, arena);
                    if (rc == 0)
                        break;
                    int errno = Native.errno();
                    if (errno == ERRNO_EINTR)
                        continue;
                    throw submitFailure("zlink_spot_send_channel_part");
                }
            }
        }
        return true;
    }

    private CompletableFuture<List<Message>> requestChannelInternal(
      String channelName, List<Message> parts, Duration timeout,
      SendFlags flags) {
        long timeoutMs = timeoutMillis(timeout);
        long requestId = NEXT_REQUEST_ID.getAndIncrement();
        CompletableFuture<Received> future = registerPending(requestId,
          timeoutMs);
        startRequestProgress(future);
        try {
            submitSpotRequestChannel(channelName, parts, REPLY_CALLBACK,
                MemorySegment.ofAddress(requestId),
                Objects.requireNonNull(flags, "flags").value(),
                toTimeoutInt(timeoutMs));
        } catch (RuntimeException ex) {
            PENDING.remove(requestId);
            future.cancel(false);
            throw ex;
        }
        return future.thenApply(InternalAccess::receivedTakeParts);
    }

    private void startRequestProgress(CompletableFuture<?> future) {
        RequestProgressPump.trackSpotRequest(future, handle,
            "zlink-spot-request-progress");
    }

    private void drainChannelReplyFrom(MemorySegment dealerSubject) {
        Objects.requireNonNull(dealerSubject, "dealerSubject");
        ensureOpen();
    }

    void drainChannelReply(SpotDispatchInfo info) {
        Objects.requireNonNull(info, "info");
        if (info.event() != SpotDispatchEvent.CHANNEL_REPLY_READABLE
            || info.subjectKind() != SpotDispatchSubjectKind.CHANNEL_DEALER) {
            throw new ZlinkConfigException(ConfigResult.INVALID_ARGUMENT);
        }
        MemorySegment subject = spotDispatchSubject(info);
        if (subject == null || subject.address() == 0) {
            throw new ZlinkConfigException(ConfigResult.INVALID_HANDLE);
        }
        drainChannelReplyFrom(subject);
    }

    private ZlinkSubmitException submitFailure(String apiName) {
        int errno = Native.errno();
        ZlinkSubmitException submit = NativeSubmitErrors.submitExceptionOrNull(errno);
        if (submit != null)
            return submit;
        throw InternalAccess.zlinkExceptionFromLastError(apiName);
    }

    private static MemorySegment spotDispatchSubject(SpotDispatchInfo info) {
        Object state = ContractAccess.spotDispatchSubjectState(info);
        return state instanceof MemorySegment segment ? segment
          : MemorySegment.NULL;
    }

    /** Subscribes to one topic or pattern string. */
    public void setSubscription(String topicId) {
        MemorySegment filter = topicCString(topicId);
        int rc = Native.setSubscription(handle, filter);
        if (rc != 0)
            throw InternalAccess.zlinkExceptionFromLastError("zlink_set_subscription");
    }

    /** Removes a topic or pattern subscription. */
    public void unsetSubscription(String topicIdOrPattern) {
        MemorySegment filter = topicCString(topicIdOrPattern);
        int rc = Native.unsetSubscription(handle, filter);
        if (rc != 0)
            throw InternalAccess.zlinkExceptionFromLastError("zlink_unset_subscription");
    }

    public Optional<SubscriptionEntry> subscriptionAt(int index) {
        if (index < 0)
            throw new IndexOutOfBoundsException("subscription index " + index);
        int capacity = TOPIC_SCRATCH_INITIAL_CAPACITY;
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment lenInOut = arena.allocate(ValueLayout.JAVA_LONG);
            MemorySegment isPatternOut = arena.allocate(ValueLayout.JAVA_INT);
            while (true) {
                MemorySegment filterOut = capacity == 0 ? MemorySegment.NULL
                    : arena.allocate(capacity);
                lenInOut.set(ValueLayout.JAVA_LONG, 0, capacity);
                isPatternOut.set(ValueLayout.JAVA_INT, 0, 0);
                int rc = Native.subscriptionAt(handle, index, filterOut,
                  lenInOut, isPatternOut);
                if (rc == 0) {
                    int actual = boundedCount(
                      lenInOut.get(ValueLayout.JAVA_LONG, 0));
                    byte[] bytes = new byte[actual];
                    if (actual > 0) {
                        MemorySegment.copy(filterOut, 0,
                          MemorySegment.ofArray(bytes), 0, actual);
                    }
                    return Optional.of(new SubscriptionEntry(
                      new String(bytes, StandardCharsets.UTF_8),
                      isPatternOut.get(ValueLayout.JAVA_INT, 0) != 0));
                }
                int errno = Native.errno();
                if (errno == ERRNO_ENOENT)
                    return Optional.empty();
                if (errno == ERRNO_EINVAL) {
                    capacity = boundedCount(
                      lenInOut.get(ValueLayout.JAVA_LONG, 0));
                    continue;
                }
                throw InternalAccess.zlinkExceptionFromLastError("zlink_subscription_at");
            }
        }
    }

    /** Installs the send-ready callback. */
    public void setSendReadyHandler(SendReadyHandler handler) {
        Objects.requireNonNull(handler, "handler");
        ensureOpen();
        ensureNoCallbackFailure();
        ExecutorService executor = callbackExecutor;
        boolean createdExecutor = false;
        if (executor == null) {
            executor = newCallbackExecutor();
            callbackExecutor = executor;
            createdExecutor = true;
        }
        Arena arena = Arena.ofShared();
        MemorySegment stub = LINKER.upcallStub(callbackHandle(
          "handleSendReadyCallback", MethodType.methodType(void.class,
            MemorySegment.class, MemorySegment.class)),
          FD_SEND_READY_CALLBACK, arena);
        boolean success = false;
        try {
            int rc = Native.sendReadyHandler(handle, stub, MemorySegment.NULL);
            if (rc != 0)
                throw InternalAccess.zlinkExceptionFromLastError("zlink_send_ready_handler");
            success = true;
            closeArena(sendReadyCallbackArena);
            sendReadyCallbackArena = arena;
            sendReadyCallbackStub = stub;
            sendReadyHandler = handler;
        } finally {
            if (!success) {
                if (createdExecutor) {
                    callbackExecutor = null;
                    shutdownExecutor(executor);
                }
                closeArena(arena);
            }
        }
    }

    public boolean subscribe(TopicMessage result, RecvFlags flags) {
        Objects.requireNonNull(result, "result");
        Objects.requireNonNull(flags, "flags");
        if (flags == RecvFlags.DONT_WAIT) {
            return subscribeIntoFastNoWait(result);
        }
        Optional<TopicMessage> fresh = receiveTopicMessage(false);
        if (fresh.isEmpty())
            return false;
        result.adoptFrom(fresh.get());
        return true;
    }

    // Non-allocating spot subscribe hot path for the DONT_WAIT single-part
    // case. Mirrors Socket.subscribeIntoFastNoWait: thread-local scratch (no
    // per-call Arena), critical downcall, cached topic String, reused result.
    // Multipart payloads fall back to the general allocating reader.
    private boolean subscribeIntoFastNoWait(TopicMessage result) {
        ensureOpen();
        SpotRecvScratch scratch = spotRecvScratch.get();
        while (true) {
            scratch.topicLenOut.set(ValueLayout.JAVA_LONG, 0, TOPIC_CAPACITY);
            Message part =
              InternalAccess.topicMessagePrepareReusableSinglePart(result);
            boolean success = false;
            try {
                int rc = Native.spotSubscribePartNoWaitCritical(handle,
                  scratch.ridOut, scratch.topicOut, TOPIC_CAPACITY,
                  scratch.topicLenOut,
                  InternalAccess.messageNativeHandle(part),
                  scratch.hasMoreOut, RECV_DONTWAIT);
                if (rc == 0) {
                    boolean more =
                      scratch.hasMoreOut.get(ValueLayout.JAVA_INT, 0) != 0;
                    InternalAccess.messageFinishReceive(part, more);
                    if (more) {
                        Message firstPart = part.move();
                        success = true;
                        Optional<TopicMessage> fresh =
                          assembleRemainder(scratch, firstPart);
                        if (fresh.isEmpty())
                            return false;
                        result.adoptFrom(fresh.get());
                        return true;
                    }
                    RoutingId routingId = cachedSpotRoutingId(scratch,
                      scratch.ridOut.get(ValueLayout.ADDRESS, 0));
                    int topicLength = normalizeTopicLength(scratch.topicOut,
                      TOPIC_CAPACITY,
                      scratch.topicLenOut.get(ValueLayout.JAVA_LONG, 0));
                    String topicId = cachedSpotTopic(scratch, topicLength);
                    success = true;
                    InternalAccess.topicMessageAdoptSingle(result, routingId,
                      topicId, part);
                    return true;
                }
            } finally {
                if (!success) {
                    try {
                        part.close();
                    } catch (RuntimeException ignored) {
                    }
                }
            }
            int errno = Native.errno();
            if (errno == ERRNO_EINTR) {
                continue;
            }
            if (errno == ERRNO_EAGAIN || errno == ERRNO_EWOULDBLOCK_WIN) {
                return false;
            }
            throw InternalAccess.zlinkExceptionFromLastError("zlink_spot_subscribe_part");
        }
    }

    private String cachedSpotTopic(SpotRecvScratch scratch, int topicLength) {
        // The C buffer can include a trailing NUL; normalizeTopicLength has
        // already trimmed it from topicLength.
        if (topicLength == 0) {
            return "";
        }
        byte[] cached = scratch.cachedTopicBytes;
        if (cached != null && cached.length == topicLength) {
            boolean same = true;
            for (int i = 0; i < topicLength; i++) {
                if (cached[i]
                    != scratch.topicOut.get(ValueLayout.JAVA_BYTE, i)) {
                    same = false;
                    break;
                }
            }
            if (same) {
                return scratch.cachedTopicString;
            }
        }
        byte[] raw = scratch.topicOut.asSlice(0, topicLength)
          .toArray(ValueLayout.JAVA_BYTE);
        String decoded = new String(raw, StandardCharsets.UTF_8);
        scratch.cachedTopicBytes = raw;
        scratch.cachedTopicString = decoded;
        return decoded;
    }

    private static RoutingId cachedSpotRoutingId(SpotRecvScratch scratch,
                                                 MemorySegment nativeRidPtr) {
        if (nativeRidPtr == null || nativeRidPtr.address() == 0) {
            return null;
        }
        MemorySegment routingId = nativeRidPtr.reinterpret(
          NativeLayouts.ROUTING_ID_LAYOUT.byteSize());
        int size = routingId.get(ValueLayout.JAVA_BYTE,
          NativeLayouts.ROUTING_ID_SIZE_OFFSET) & 0xFF;
        if (size == 0) {
            return null;
        }
        byte[] cached = scratch.cachedRoutingIdBytes;
        if (cached != null && cached.length == size) {
            boolean same = true;
            for (int i = 0; i < size; i++) {
                if (cached[i] != routingId.get(ValueLayout.JAVA_BYTE,
                        NativeLayouts.ROUTING_ID_DATA_OFFSET + i)) {
                    same = false;
                    break;
                }
            }
            if (same) {
                return scratch.cachedRoutingId;
            }
        }
        byte[] value = new byte[size];
        MemorySegment.copy(routingId, NativeLayouts.ROUTING_ID_DATA_OFFSET,
          MemorySegment.ofArray(value), 0, size);
        RoutingId decoded = InternalAccess.routingIdFromTrusted(value);
        scratch.cachedRoutingIdBytes = value;
        scratch.cachedRoutingId = decoded;
        return decoded;
    }

    private Optional<TopicMessage> assembleRemainder(SpotRecvScratch scratch,
                                                     Message firstPart) {
        RoutingId routingId = readRoutingIdPtr(
          scratch.ridOut.get(ValueLayout.ADDRESS, 0));
        int topicLength = normalizeTopicLength(scratch.topicOut,
          TOPIC_CAPACITY,
          scratch.topicLenOut.get(ValueLayout.JAVA_LONG, 0));
        String topicId = decodeTopic(scratch.topicOut, topicLength);
        java.util.ArrayList<Message> parts = new java.util.ArrayList<>();
        parts.add(firstPart);
        while (true) {
            Message next = new Message();
            boolean ok = false;
            try {
                int rc = Native.spotSubscribePart(handle, scratch.ridOut,
                  scratch.topicOut, TOPIC_CAPACITY, scratch.topicLenOut,
                  InternalAccess.messageNativeHandle(next),
                  scratch.hasMoreOut, RECV_BLOCKING);
                if (rc == 0) {
                    ok = true;
                    boolean more =
                      scratch.hasMoreOut.get(ValueLayout.JAVA_INT, 0) != 0;
                    InternalAccess.messageFinishReceive(next, more);
                    parts.add(next);
                    if (!more) {
                        return Optional.of(InternalAccess.topicMessage(
                          routingId, topicId,
                          parts.toArray(Message[]::new)));
                    }
                    continue;
                }
            } finally {
                if (!ok) {
                    try {
                        next.close();
                    } catch (RuntimeException ignored) {
                    }
                }
            }
            int errno = Native.errno();
            if (errno == ERRNO_EINTR) {
                continue;
            }
            for (Message m : parts) {
                try {
                    m.close();
                } catch (RuntimeException ignored) {
                }
            }
            throw InternalAccess.zlinkExceptionFromLastError("zlink_spot_subscribe_part");
        }
    }

    Optional<TopicMessage> subscribeNoWait() {
        return receiveTopicMessage(true);
    }

    /** Receives the next subscription event for this spot. */
    public boolean receiveSubscriptionEvent(SubscriptionEvent result,
                                            RecvFlags flags) {
        Objects.requireNonNull(result, "result");
        Objects.requireNonNull(flags, "flags");
        Optional<SubscriptionEvent> fresh =
          receiveSubscriptionEvent(flags == RecvFlags.DONT_WAIT);
        if (fresh.isEmpty())
            return false;
        result.adoptFrom(fresh.get());
        return true;
    }

    void replyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                            long requestSeq, Message message) {
        replyToSpot(destNodeRid, destSpotRid, requestSeq, List.of(message),
          SendFlags.NONE);
    }

    void replyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                            long requestSeq, Message message, SendFlags flags) {
        replyToSpot(destNodeRid, destSpotRid, requestSeq, List.of(message),
          flags);
    }

    void replyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                            long requestSeq, List<Message> parts) {
        replyToSpot(destNodeRid, destSpotRid, requestSeq, parts, SendFlags.NONE);
    }

    void replyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                            long requestSeq, List<Message> parts,
                            SendFlags flags) {
        routedSupport.replyToSpot(destNodeRid, destSpotRid, requestSeq, parts,
          flags);
    }

    void replyToRouter(RoutingId peerRid, long requestSeq, Message message) {
        replyToRouter(peerRid, requestSeq, List.of(message), SendFlags.NONE);
    }

    void replyToRouter(RoutingId peerRid, long requestSeq, Message message,
                              SendFlags flags) {
        replyToRouter(peerRid, requestSeq, List.of(message), flags);
    }

    void replyToRouter(RoutingId peerRid, long requestSeq,
                              List<Message> parts) {
        replyToRouter(peerRid, requestSeq, parts, SendFlags.NONE);
    }

    void replyToRouter(RoutingId peerRid, long requestSeq,
                              List<Message> parts, SendFlags flags) {
        routedSupport.replyToRouter(peerRid, requestSeq, parts, flags);
    }

    public boolean recvRouted(Received result, RecvFlags flags) {
        Objects.requireNonNull(result, "result");
        Received fresh = routedSupport.recvRouted(flags);
        if (fresh == null)
            return false;
        result.adoptFrom(fresh);
        return true;
    }

    public void setDispatchHandler(SpotDispatchEventHandler handler) {
        routedSupport.setDispatchHandler(handler);
    }

    public ActorJoinRequest recvActorJoin(RecvFlags flags) {
        Objects.requireNonNull(flags, "flags");
        ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment infoOut = arena.allocate(
              NativeLayouts.ACTOR_JOIN_INFO_LAYOUT);
            MemorySegment partsOut = arena.allocate(ValueLayout.ADDRESS);
            MemorySegment partCountOut = arena.allocate(ValueLayout.JAVA_LONG);
            Message message = null;
            boolean success = false;
            try {
                int rc = Native.spotActorJoinRecv(handle, infoOut, partsOut,
                  partCountOut, flags.value());
                if (rc != 0) {
                    if (flags == RecvFlags.DONT_WAIT
                        && rc == RecvResult.NO_DATA.value()) {
                        return null;
                    }
                    throw new ZlinkRecvException(RecvResult.fromValue(rc));
                }
                MemorySegment parts = partsOut.get(ValueLayout.ADDRESS, 0);
                long partCount = partCountOut.get(ValueLayout.JAVA_LONG, 0);
                Message[] messages = partCount > 0
                  ? InternalAccess.messageFromOwnedMessageVector(parts, partCount)
                  : new Message[] { new Message() };
                NativeMessage.multipartClose(parts, partCount);
                message = messages[0];
                for (int i = 1; i < messages.length; i++) {
                    messages[i].close();
                }
                success = true;
                return ContractAccess.actorJoinRequest(
                  readActorJoinInfo(infoOut), message);
            } finally {
                if (!success) {
                    message.close();
                }
            }
        }
    }

    public ActorJoinRequest recvActorJoin() {
        return recvActorJoin(RecvFlags.NONE);
    }

    /**
     * Reply to an Actor join admission request. Returns a multipart-reply
     * builder; a zero-message submit is allowed. {@code joinResultCode == 0}
     * accepts the join, and non-zero values reject it with an
     * application-defined code.
     */
    public ActorJoinReplyOperation replyActorJoin(ActorJoinRequest request,
                                           int joinResultCode) {
        Objects.requireNonNull(request, "request");
        ensureOpen();
        return new ActorJoinReplyBuilder(request, joinResultCode);
    }

    public SpotActorLifecycleEvent recvActorLifecycle(RecvFlags flags) {
        ensureOpen();
        Objects.requireNonNull(flags, "flags");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment eventOut = arena.allocate(
              NativeLayouts.SPOT_ACTOR_LIFECYCLE_EVENT_LAYOUT);
            int rc = Native.spotRecvActorLifecycle(handle, eventOut,
              flags.value());
            if (rc != 0) {
                if (flags == RecvFlags.DONT_WAIT
                    && rc == RecvResult.NO_DATA.value()) {
                    return null;
                }
                throw new ZlinkRecvException(RecvResult.fromValue(rc));
            }
            return new SpotActorLifecycleEvent(
              ContractAccess.actorLifecycleKindFromValue(eventOut.get(
                ValueLayout.JAVA_INT,
                NativeLayouts.SPOT_ACTOR_LIFECYCLE_EVENT_KIND_OFFSET)),
              ActorInterop.lifecycleInfoFromNative(eventOut.asSlice(
                NativeLayouts.SPOT_ACTOR_LIFECYCLE_EVENT_INFO_OFFSET,
                NativeLayouts.SPOT_ACTOR_LIFECYCLE_INFO_LAYOUT.byteSize())));
        }
    }

    public SpotActorLifecycleEvent recvActorLifecycle() {
        return recvActorLifecycle(RecvFlags.NONE);
    }

    private final class ActorJoinReplyBuilder implements ActorJoinReplyOperation {
        private final ActorJoinRequest request;
        private final int joinResultCode;
        private final MessagePartsBuffer parts = new MessagePartsBuffer();
        private boolean submitted;

        ActorJoinReplyBuilder(ActorJoinRequest request, int joinResultCode) {
            this.request = request;
            this.joinResultCode = joinResultCode;
        }

        @Override
        public ActorJoinReplyOperation message(Message part) {
            ensureNotSubmitted();
            parts.add(Objects.requireNonNull(part, "part"));
            return this;
        }

        @Override
        public void submit() {
            ensureNotSubmitted();
            submitted = true;
            ensureOpen();
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment nativeInfo = arena.allocate(
                  NativeLayouts.ACTOR_JOIN_INFO_LAYOUT);
                writeActorJoinInfo(nativeInfo, request.info());
                MemorySegment partsArr = parts.copyToNativeArray(arena);
                int rc = Native.spotActorJoinReply(handle, nativeInfo,
                  joinResultCode, partsArr, parts.size());
                if (rc != 0) {
                    MessagePartsBuffer.closeNativeArray(partsArr, parts.size());
                    throw new ZlinkSubmitException(SubmitResult.fromValue(rc));
                }
            }
        }

        private void ensureNotSubmitted() {
            if (submitted)
                throw new IllegalStateException("operation already submitted");
        }
    }

    private static ActorJoinInfo readActorJoinInfo(MemorySegment segment) {
        MemorySegment view = segment.reinterpret(
          NativeLayouts.ACTOR_JOIN_INFO_LAYOUT.byteSize());
        return ContractAccess.actorJoinInfoFromNative(
          ActorInterop.actorRefFromNative(view.asSlice(
            NativeLayouts.ACTOR_JOIN_INFO_SOURCE_ACTOR_OFFSET,
            NativeLayouts.ACTOR_REF_LAYOUT.byteSize())),
          ActorInterop.actorRefFromNative(view.asSlice(
            NativeLayouts.ACTOR_JOIN_INFO_TARGET_ACTOR_OFFSET,
            NativeLayouts.ACTOR_REF_LAYOUT.byteSize())),
          ActorInterop.readRoutingId(view.asSlice(
            NativeLayouts.ACTOR_JOIN_INFO_SOURCE_NODE_RID_OFFSET,
            NativeLayouts.ROUTING_ID_LAYOUT.byteSize())),
          ActorInterop.readRoutingId(view.asSlice(
            NativeLayouts.ACTOR_JOIN_INFO_SOURCE_SPOT_RID_OFFSET,
            NativeLayouts.ROUTING_ID_LAYOUT.byteSize())),
          ActorInterop.readRoutingId(view.asSlice(
            NativeLayouts.ACTOR_JOIN_INFO_TARGET_NODE_RID_OFFSET,
            NativeLayouts.ROUTING_ID_LAYOUT.byteSize())),
          ActorInterop.readRoutingId(view.asSlice(
            NativeLayouts.ACTOR_JOIN_INFO_TARGET_SPOT_RID_OFFSET,
            NativeLayouts.ROUTING_ID_LAYOUT.byteSize())),
          view.get(ValueLayout.JAVA_LONG_UNALIGNED,
            NativeLayouts.ACTOR_JOIN_INFO_JOIN_EPOCH_OFFSET),
          view.get(ValueLayout.ADDRESS,
            NativeLayouts.ACTOR_JOIN_INFO_REQUEST_OFFSET),
          view.get(ValueLayout.JAVA_INT,
            NativeLayouts.ACTOR_JOIN_INFO_FLAGS_OFFSET));
    }

    private static void writeActorJoinInfo(MemorySegment out,
                                           ActorJoinInfo info) {
        ActorInterop.writeActorRef(out.asSlice(
          NativeLayouts.ACTOR_JOIN_INFO_SOURCE_ACTOR_OFFSET,
          NativeLayouts.ACTOR_REF_LAYOUT.byteSize()), info.sourceActor());
        ActorInterop.writeActorRef(out.asSlice(
          NativeLayouts.ACTOR_JOIN_INFO_TARGET_ACTOR_OFFSET,
          NativeLayouts.ACTOR_REF_LAYOUT.byteSize()), info.targetActor());
        writeRoutingId(out.asSlice(
          NativeLayouts.ACTOR_JOIN_INFO_SOURCE_NODE_RID_OFFSET,
          NativeLayouts.ROUTING_ID_LAYOUT.byteSize()),
          ContractAccess.actorJoinInfoSourceNodeRidRaw(info));
        writeRoutingId(out.asSlice(
          NativeLayouts.ACTOR_JOIN_INFO_SOURCE_SPOT_RID_OFFSET,
          NativeLayouts.ROUTING_ID_LAYOUT.byteSize()),
          ContractAccess.actorJoinInfoSourceSpotRidRaw(info));
        writeRoutingId(out.asSlice(
          NativeLayouts.ACTOR_JOIN_INFO_TARGET_NODE_RID_OFFSET,
          NativeLayouts.ROUTING_ID_LAYOUT.byteSize()),
          ContractAccess.actorJoinInfoTargetNodeRidRaw(info));
        writeRoutingId(out.asSlice(
          NativeLayouts.ACTOR_JOIN_INFO_TARGET_SPOT_RID_OFFSET,
          NativeLayouts.ROUTING_ID_LAYOUT.byteSize()),
          ContractAccess.actorJoinInfoTargetSpotRidRaw(info));
        out.set(ValueLayout.JAVA_LONG_UNALIGNED,
          NativeLayouts.ACTOR_JOIN_INFO_JOIN_EPOCH_OFFSET, info.joinEpoch());
        out.set(ValueLayout.ADDRESS,
          NativeLayouts.ACTOR_JOIN_INFO_REQUEST_OFFSET,
          actorJoinRequestState(info));
        out.set(ValueLayout.JAVA_INT,
          NativeLayouts.ACTOR_JOIN_INFO_FLAGS_OFFSET, info.flags());
    }

    private static void writeRoutingId(MemorySegment out, RoutingId rid) {
        byte[] value = rid == null ? new byte[0]
          : InternalAccess.routingIdTrustedBytes(rid);
        out.set(ValueLayout.JAVA_BYTE, NativeLayouts.ROUTING_ID_SIZE_OFFSET,
          (byte) value.length);
        if (value.length > 0) {
            MemorySegment.copy(MemorySegment.ofArray(value), 0, out,
              NativeLayouts.ROUTING_ID_DATA_OFFSET, value.length);
        }
    }

    private static MemorySegment actorJoinRequestState(ActorJoinInfo info) {
        Object state = ContractAccess.actorJoinInfoRequestState(info);
        return state instanceof MemorySegment segment ? segment
          : MemorySegment.NULL;
    }

    public List<ActorRef> actors() {
        ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment count = arena.allocate(ValueLayout.JAVA_LONG);
            int rc = Native.spotActors(handle, MemorySegment.NULL,
              count);
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError("zlink_spot_actors");
            }
            int available = boundedCount(count.get(ValueLayout.JAVA_LONG, 0));
            if (available == 0) {
                return List.of();
            }
            MemorySegment entries = arena.allocate(
              NativeLayouts.ACTOR_REF_LAYOUT, available);
            count.set(ValueLayout.JAVA_LONG, 0, available);
            rc = Native.spotActors(handle, entries, count);
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError("zlink_spot_actors");
            }
            int actual = Math.min(available, boundedCount(
              count.get(ValueLayout.JAVA_LONG, 0)));
            long stride = NativeLayouts.ACTOR_REF_LAYOUT.byteSize();
            ArrayList<ActorRef> out = new ArrayList<>(actual);
            for (int i = 0; i < actual; i++) {
                out.add(ActorInterop.actorRefFromNative(entries.asSlice(
                  (long) i * stride, stride)));
            }
            return List.copyOf(out);
        }
    }

    private final class SendBuilder implements SendOperation, SendSubmitOperation {
        private final SingleSendInvoker singleInvoker;
        private final SendInvoker invoker;
        private Message singlePart;
        private MessagePartsBuffer parts;
        private int partCount;
        private SendFlags flags = SendFlags.NONE;
        private boolean submitted;

        private SendBuilder(SingleSendInvoker singleInvoker,
                            SendInvoker invoker) {
            this.singleInvoker = Objects.requireNonNull(singleInvoker,
              "singleInvoker");
            this.invoker = invoker;
        }

        @Override
        public SendSubmitOperation message(Message part) {
            ensureNotSubmitted();
            Objects.requireNonNull(part, "part");
            if (partCount == 0) {
                singlePart = part;
            } else {
                if (parts == null) {
                    parts = new MessagePartsBuffer();
                    parts.add(singlePart);
                    singlePart = null;
                }
                parts.add(part);
            }
            partCount++;
            return this;
        }

        @Override
        public SendSubmitOperation flags(SendFlags value) {
            ensureNotSubmitted();
            flags = Objects.requireNonNull(value, "flags");
            return this;
        }

        @Override
        public boolean submit() {
            markSubmitted();
            if (partCount == 1) {
                return singleInvoker.submit(singlePart, flags);
            }
            return invoker.submit(parts.asList(), flags);
        }

        private void markSubmitted() {
            ensureNotSubmitted();
            if (partCount == 0)
                throw new IllegalArgumentException("at least one message required");
            submitted = true;
        }

        private void ensureNotSubmitted() {
            if (submitted)
                throw new IllegalStateException("operation already submitted");
        }
    }

    private final class RequestBuilder implements RequestOperation, RequestSubmitOperation {
        private final RequestAsyncInvoker asyncInvoker;
        private final RequestCallbackInvoker callbackInvoker;
        private final MessagePartsBuffer parts = new MessagePartsBuffer();
        private Duration timeout = Duration.ofMillis(5_000L);
        private SendFlags flags = SendFlags.NONE;
        private boolean submitted;

        private RequestBuilder(RequestAsyncInvoker asyncInvoker,
                               RequestCallbackInvoker callbackInvoker) {
            this.asyncInvoker = asyncInvoker;
            this.callbackInvoker = callbackInvoker;
        }

        @Override
        public RequestSubmitOperation message(Message part) {
            addMessage(part);
            return this;
        }

        @Override
        public RequestSubmitOperation timeout(Duration value) {
            ensureNotSubmitted();
            timeout = Objects.requireNonNull(value, "timeout");
            return this;
        }

        @Override
        public RequestCallbackSubmitOperation flags(SendFlags value) {
            ensureNotSubmitted();
            return new CallbackRequestBuilder(this,
              Objects.requireNonNull(value, "flags"));
        }

        @Override
        public CompletableFuture<List<Message>> submitAsync() {
            markSubmitted();
            return asyncInvoker.submit(parts.asList(), timeout, flags);
        }

        @Override
        public boolean submit(RequestCallback callback) {
            markSubmitted();
            return callbackInvoker.submit(parts.asList(),
              Objects.requireNonNull(callback, "callback"), flags, timeout);
        }

        private void addMessage(Message part) {
            ensureNotSubmitted();
            parts.add(Objects.requireNonNull(part, "part"));
        }

        private void markSubmitted() {
            ensureNotSubmitted();
            if (parts.isEmpty())
                throw new IllegalArgumentException("at least one message required");
            submitted = true;
        }

        private void ensureNotSubmitted() {
            if (submitted)
                throw new IllegalStateException("operation already submitted");
        }
    }

    private final class CallbackRequestBuilder
      implements RequestCallbackSubmitOperation {
        private final RequestBuilder source;
        private SendFlags flags;

        private CallbackRequestBuilder(RequestBuilder source,
                                       SendFlags flags) {
            this.source = source;
            this.flags = flags;
        }

        @Override
        public RequestCallbackSubmitOperation message(Message part) {
            source.addMessage(part);
            return this;
        }

        @Override
        public RequestCallbackSubmitOperation timeout(Duration timeout) {
            source.timeout(timeout);
            return this;
        }

        @Override
        public RequestCallbackSubmitOperation flags(SendFlags value) {
            source.ensureNotSubmitted();
            flags = Objects.requireNonNull(value, "flags");
            return this;
        }

        @Override
        public boolean submit(RequestCallback callback) {
            source.markSubmitted();
            return source.callbackInvoker.submit(source.parts.asList(),
              Objects.requireNonNull(callback, "callback"), flags,
              source.timeout);
        }
    }

    private final class ReplyBuilder implements ReplyOperation, ReplySubmitOperation {
        private final ReplyInvoker invoker;
        private final MessagePartsBuffer parts = new MessagePartsBuffer();
        private SendFlags flags = SendFlags.NONE;
        private boolean submitted;

        private ReplyBuilder(ReplyInvoker invoker) {
            this.invoker = invoker;
        }

        @Override
        public ReplySubmitOperation message(Message part) {
            ensureNotSubmitted();
            parts.add(Objects.requireNonNull(part, "part"));
            return this;
        }

        @Override
        public ReplySubmitOperation flags(SendFlags value) {
            ensureNotSubmitted();
            flags = Objects.requireNonNull(value, "flags");
            return this;
        }

        @Override
        public void submit() {
            ensureNotSubmitted();
            if (parts.isEmpty())
                throw new IllegalArgumentException("at least one message required");
            submitted = true;
            invoker.submit(parts.asList(), flags);
        }

        private void ensureNotSubmitted() {
            if (submitted)
                throw new IllegalStateException("operation already submitted");
        }
    }

    @FunctionalInterface
    private interface SingleSendInvoker {
        boolean submit(Message part, SendFlags flags);
    }

    @FunctionalInterface
    private interface SendInvoker {
        boolean submit(List<Message> parts, SendFlags flags);
    }

    @FunctionalInterface
    private interface RequestAsyncInvoker {
        CompletableFuture<List<Message>> submit(List<Message> parts,
                                                Duration timeout,
                                                SendFlags flags);
    }

    @FunctionalInterface
    private interface RequestCallbackInvoker {
        boolean submit(List<Message> parts, RequestCallback callback,
                       SendFlags flags, Duration timeout);
    }

    @FunctionalInterface
    private interface ReplyInvoker {
        void submit(List<Message> parts, SendFlags flags);
    }

    @Override
    public void close() {
        MemorySegment currentHandle = handle;
        if (currentHandle == null || currentHandle.address() == 0) {
            return;
        }
        ExecutorService executor = callbackExecutor;
        Arena subscribeArena = subscribeCallbackArena;
        Arena readyArena = sendReadyCallbackArena;
        subscribeHandler = null;
        sendReadyHandler = null;
        callbackFailure = null;
        callbackExecutor = null;
        subscribeCallbackArena = null;
        sendReadyCallbackArena = null;
        MemorySegment subscribeStub = subscribeCallbackStub;
        MemorySegment readyStub = sendReadyCallbackStub;
        subscribeCallbackStub = MemorySegment.NULL;
        sendReadyCallbackStub = MemorySegment.NULL;
        handle = MemorySegment.NULL;
        Native.spotDestroy(currentHandle);
        routedSupport.close();
        if (ownerNode != null) {
            InternalAccess.spotNodeReleaseSpot(ownerNode, this);
        }
        shutdownExecutor(executor);
        closeArena(subscribeArena);
        closeArena(readyArena);
        subscribeStub = MemorySegment.NULL;
        readyStub = MemorySegment.NULL;
        topicCache.clear();
        topicScratch.remove();
        topicScratchCapacity.remove();
        if (topicCacheArena.scope().isAlive())
            topicCacheArena.close();
    }

    private static int normalizeTopicLength(MemorySegment topic, int capacity,
                                            long reportedLength) {
        long len = reportedLength;
        if (len < 0)
            len = 0;
        if (len > capacity)
            len = capacity;
        int bounded = (int) len;
        if (bounded > 0 && topic.get(ValueLayout.JAVA_BYTE, bounded - 1) == 0)
            bounded--;
        return bounded;
    }

    private static int validateMessages(List<Message> messages, String name) {
        Objects.requireNonNull(messages, name);
        if (messages.isEmpty())
            throw new IllegalArgumentException(name + " required");
        return messages.size();
    }

    private static int boundedCount(long value) {
        if (value <= 0)
            return 0;
        if (value > Integer.MAX_VALUE)
            return Integer.MAX_VALUE;
        return (int) value;
    }

    private void ensureOpen() {
        if (handle == null || handle.address() == 0)
            throw new IllegalStateException("spot is closed");
        ensureNoCallbackFailure();
    }

    private void ensureNoCallbackFailure() {
        RuntimeException failure = callbackFailure;
        if (failure != null)
            throw failure;
    }

    private MethodHandle callbackHandle(String name, MethodType type) {
        try {
            return MethodHandles.lookup().findVirtual(NativeSpot.class, name, type)
              .bindTo(this);
        } catch (ReflectiveOperationException ex) {
            throw new IllegalStateException("failed to bind callback " + name,
              ex);
        }
    }

    private void handleSubscribeCallback(MemorySegment sourceRid,
                                         MemorySegment topic,
                                         long topicLen,
                                         MemorySegment parts,
                                         long partCount,
                                         MemorySegment userdata) {
        SubscribeCallback handler = subscribeHandler;
        ExecutorService executor = callbackExecutor;
        if (handler == null || executor == null)
            return;
        CallbackSubscribeData snapshot = null;
        try {
            snapshot = snapshotSubscribe(sourceRid, topic, topicLen, parts,
                partCount);
            CallbackSubscribeData callbackSnapshot = snapshot;
            executor.execute(() -> dispatchSubscribe(handler, callbackSnapshot));
            snapshot = null;
        } catch (RejectedExecutionException ex) {
            recordCallbackFailure(ex);
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
        } finally {
            closeSnapshot(snapshot);
        }
    }

    private void dispatchSubscribe(SubscribeCallback handler,
                                   CallbackSubscribeData snapshot) {
        try {
            Received received = materializeReceived(snapshot);
            InternalAccess.enterCallback();
            try (received) {
                handler.onMessage(snapshot.routingId(), snapshot.topicId(),
                    received);
            } finally {
                InternalAccess.leaveCallback();
            }
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
        }
    }

    private void handleSendReadyCallback(MemorySegment subject,
                                         MemorySegment userdata) {
        SendReadyHandler handler = sendReadyHandler;
        ExecutorService executor = callbackExecutor;
        if (handler == null || executor == null)
            return;
        try {
            executor.execute(() -> dispatchSendReady(handler));
        } catch (RejectedExecutionException ex) {
            recordCallbackFailure(ex);
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
        }
    }

    private void dispatchSendReady(SendReadyHandler handler) {
        InternalAccess.enterCallback();
        try {
            handler.onReady();
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
        } finally {
            InternalAccess.leaveCallback();
        }
    }

    private static CallbackSubscribeData snapshotSubscribe(MemorySegment sourceRid,
                                                           MemorySegment topic,
                                                           long topicLen,
                                                           MemorySegment parts,
                                                           long partCount) {
        Message[] snapshotParts = InternalAccess.messageFromOwnedMessageVectorShared(
            parts, partCount);
        NativeMessage.multipartClose(parts, partCount);
        return new CallbackSubscribeData(readRoutingId(sourceRid),
            decodeTopic(topic, topicLen), snapshotParts);
    }

    private static Received materializeReceived(CallbackSubscribeData snapshot) {
        return InternalAccess.received(snapshot.routingId(), null,
          snapshot.parts(), 0L,
          false, null);
    }

    private static RoutingId readRoutingId(MemorySegment sourceRid) {
        if (sourceRid == null || sourceRid.address() == 0)
            return null;
        MemorySegment routingId = sourceRid.reinterpret(
          NativeLayouts.ROUTING_ID_LAYOUT.byteSize());
        int size = routingId.get(ValueLayout.JAVA_BYTE,
          NativeLayouts.ROUTING_ID_SIZE_OFFSET) & 0xFF;
        if (size == 0)
            return null;
        byte[] value = new byte[size];
        MemorySegment.copy(routingId, NativeLayouts.ROUTING_ID_DATA_OFFSET,
          MemorySegment.ofArray(value), 0, size);
        return InternalAccess.routingIdFromTrusted(value);
    }

    private static String decodeTopic(MemorySegment topic, long topicLen) {
        int length = Math.max(0, Math.toIntExact(topicLen));
        if (length == 0)
            return "";
        MemorySegment topicBytes = topic.reinterpret(length);
        if (length > 0 && topicBytes.get(ValueLayout.JAVA_BYTE, length - 1) == 0)
            length--;
        if (length == 0)
            return "";
        ByteBuffer buffer = topicBytes.asSlice(0, length).asByteBuffer();
        return StandardCharsets.UTF_8.decode(buffer).toString();
    }

    private void recordCallbackFailure(RuntimeException failure) {
        callbackFailure = failure;
        Thread current = Thread.currentThread();
        Thread.UncaughtExceptionHandler uncaught =
          current.getUncaughtExceptionHandler();
        if (uncaught != null)
            uncaught.uncaughtException(current, failure);
    }

    private Optional<TopicMessage> receiveTopicMessage(boolean nonBlocking) {
        ensureOpen();
        while (true) {
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment ridOut = arena.allocate(ValueLayout.ADDRESS);
                MemorySegment topicOut = arena.allocate(TOPIC_CAPACITY);
                MemorySegment topicLenOut = arena.allocate(ValueLayout.JAVA_LONG);
                topicLenOut.set(ValueLayout.JAVA_LONG, 0, TOPIC_CAPACITY);
                MemorySegment hasMoreOut = arena.allocate(ValueLayout.JAVA_INT);
                Message[] parts = new Message[4];
                int partCount = 0;
                RoutingId routingId = null;
                String topicId = "";
                while (true) {
                    Message part = new Message();
                    boolean success = false;
                    int rc = RecvResult.INTERNAL_ERROR.value();
                    try {
                        rc = Native.spotSubscribePart(handle, ridOut,
                          topicOut, TOPIC_CAPACITY, topicLenOut,
                          InternalAccess.messageNativeHandle(part),
                          hasMoreOut, nonBlocking ? RECV_DONTWAIT : RECV_BLOCKING);
                        if (rc == 0) {
                            success = true;
                            InternalAccess.messageFinishReceive(part,
                              hasMoreOut.get(ValueLayout.JAVA_INT, 0) != 0);
                            if (partCount == 0) {
                                routingId = readRoutingIdPtr(
                                  ridOut.get(ValueLayout.ADDRESS, 0));
                                int topicLength = normalizeTopicLength(topicOut,
                                  TOPIC_CAPACITY,
                                  topicLenOut.get(ValueLayout.JAVA_LONG, 0));
                                topicId = decodeTopic(topicOut, topicLength);
                            }
                            if (partCount == parts.length) {
                                parts = java.util.Arrays.copyOf(parts,
                                  partCount * 2);
                            }
                            parts[partCount++] = part;
                            if (!InternalAccess.messageMore(part)) {
                                return Optional.of(InternalAccess.topicMessage(
                                  routingId,
                                  topicId,
                                  partCount == parts.length ? parts
                                    : java.util.Arrays.copyOf(parts, partCount)));
                            }
                            continue;
                        }
                    } finally {
                        if (!success) {
                            try {
                                part.close();
                            } catch (RuntimeException ignored) {
                            }
                        }
                    }
                    RecvResult result;
                    try {
                        result = RecvResult.fromValue(rc);
                    } catch (IllegalArgumentException ex) {
                        int errno = Native.errno();
                        if (errno == ERRNO_EINTR)
                            break;
                        throw InternalAccess.zlinkExceptionFromLastError(
                          "zlink_spot_subscribe_part");
                    }
                    for (int i = 0; i < partCount; i++) {
                        try {
                            parts[i].close();
                        } catch (RuntimeException ignored) {
                        }
                    }
                    if (nonBlocking && (result == RecvResult.NO_DATA
                        || result == RecvResult.BUSY)) {
                        return Optional.empty();
                    }
                    throw new ZlinkRecvException(result, Native.errno());
                }
            }
        }
    }

    private Optional<SubscriptionEvent> receiveSubscriptionEvent(
      boolean nonBlocking) {
        ensureOpen();
        while (true) {
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment rid = arena.allocate(NativeLayouts.ROUTING_ID_LAYOUT);
                rid.set(ValueLayout.JAVA_BYTE, NativeLayouts.ROUTING_ID_SIZE_OFFSET,
                  (byte) 0);
                MemorySegment subscribedOut = arena.allocate(ValueLayout.JAVA_INT);
                MemorySegment topicOut = arena.allocate(TOPIC_CAPACITY);
                MemorySegment topicLenOut = arena.allocate(ValueLayout.JAVA_LONG);
                topicLenOut.set(ValueLayout.JAVA_LONG, 0, TOPIC_CAPACITY);

                int rc = Native.subscriptionEvent(handle, rid, subscribedOut,
                  topicOut, topicLenOut,
                  nonBlocking ? RECV_DONTWAIT : RECV_BLOCKING);
                if (rc == 0) {
                    int topicLength = normalizeTopicLength(topicOut,
                      TOPIC_CAPACITY, topicLenOut.get(ValueLayout.JAVA_LONG, 0));
                    String topicId = decodeTopic(topicOut, topicLength);
                    return Optional.of(new SubscriptionEvent(
                      Optional.ofNullable(readRoutingId(rid)),
                      topicId, subscribedOut.get(ValueLayout.JAVA_INT, 0) != 0));
                }
                RecvResult result;
                try {
                    result = RecvResult.fromValue(rc);
                } catch (IllegalArgumentException ex) {
                    int errno = Native.errno();
                    if (errno == ERRNO_EINTR)
                        continue;
                    throw InternalAccess.zlinkExceptionFromLastError("zlink_xpub_recv_part");
                }
                if (result == RecvResult.NO_DATA && nonBlocking) {
                    return Optional.empty();
                }
                if (result == RecvResult.NO_DATA) {
                    throw new ZlinkRecvException(result, Native.errno());
                }
                if (result == RecvResult.BUSY && nonBlocking) {
                    return Optional.empty();
                }
                throw new ZlinkRecvException(result, Native.errno());
            }
        }
    }

    private static void closeMsgVector(MemorySegment vec, int count) {
        for (int i = 0; i < count; i++) {
            MemorySegment msg = vec.asSlice((long) i * MSG_SIZE, MSG_SIZE);
            NativeMessage.messageClose(msg);
        }
    }

    private static void closeArena(Arena arena) {
        if (arena != null && arena.scope().isAlive())
            arena.close();
    }

    private static void closeReceived(Received received) {
        if (received != null) {
            try {
                received.close();
            } catch (RuntimeException ignored) {
            }
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
                    future.completeExceptionally(new ZlinkRequestException(
                        RequestResult.fromValue(result), result));
                }
                return;
            }
            Message[] frames = InternalAccess.messageFromOwnedMessageVectorShared(
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
            NativeMessage.multipartClose(parts, partCount);
        }
    }

    private int spotPublishPartOnce(String topicId, Message part, int flags,
                                    int partFlag) {
        return spotPublishPartOnce(topicCString(topicId), part, flags,
          partFlag, spotSendScratch.get().nativeMsg);
    }

    private int spotPublishPartOnce(MemorySegment topic, Message part,
                                    int flags, int partFlag, Arena arena) {
        return spotPublishPartOnce(topic, part, flags, partFlag,
          arena.allocate(NativeLayouts.MESSAGE_LAYOUT));
    }

    private int spotPublishPartOnce(MemorySegment topic, Message part,
                                    int flags, int partFlag,
                                    MemorySegment nativeMsg) {
        Object anchor = InternalAccess.messageTransferTo(part, nativeMsg);
        try {
            int rc = Native.spotPublishPart(handle, topic, nativeMsg, flags,
              partFlag);
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

    private int spotSendChannelPartOnce(String channelName, Message part,
                                        int flags, int partFlag) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment service = NativeHelpers.toCString(arena,
              requireChannelName(channelName));
            return spotSendChannelPartOnce(service, part, flags, partFlag,
                arena);
        }
    }

    private int spotSendChannelPartOnce(MemorySegment service, Message part,
                                        int flags, int partFlag, Arena arena) {
        MemorySegment nativeMsg = arena.allocate(NativeLayouts.MESSAGE_LAYOUT);
        Object anchor = InternalAccess.messageTransferTo(part, nativeMsg);
        try {
            int rc = Native.spotSendChannelPart(handle, service, nativeMsg,
              flags, partFlag);
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

    private void submitSpotRequestChannel(String channelName,
                                          List<Message> payload,
                                          MemorySegment handler,
                                          MemorySegment userData,
                                          int flags,
                                          int timeoutMs) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment service = NativeHelpers.toCString(arena,
              requireChannelName(channelName));
            for (int i = 0; i < payload.size(); i++) {
                boolean last = i + 1 >= payload.size();
                int partFlag = last ? Native.PART_FINAL : Native.PART_MORE;
                while (true) {
                    int rc = spotRequestChannelPartOnce(service, payload.get(i),
                      last ? handler : MemorySegment.NULL,
                      last ? userData : MemorySegment.NULL,
                      flags, partFlag, last ? timeoutMs : 0, arena);
                    if (rc == 0)
                        break;
                    int errno = Native.errno();
                    if (errno == ERRNO_EINTR)
                        continue;
                    throw submitFailure("zlink_spot_request_channel_part");
                }
            }
        }
    }

    private int spotRequestChannelPartOnce(String channelName, Message part,
                                           MemorySegment handler,
                                           MemorySegment userData,
                                           int flags,
                                           int partFlag,
                                           int timeoutMs) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment service = NativeHelpers.toCString(arena,
              requireChannelName(channelName));
            return spotRequestChannelPartOnce(service, part, handler, userData,
                flags, partFlag, timeoutMs, arena);
        }
    }

    private int spotRequestChannelPartOnce(MemorySegment service, Message part,
                                           MemorySegment handler,
                                           MemorySegment userData,
                                           int flags,
                                           int partFlag,
                                           int timeoutMs,
                                           Arena arena) {
        MemorySegment nativeMsg = arena.allocate(NativeLayouts.MESSAGE_LAYOUT);
        InternalAccess.messageCopyTo(part, nativeMsg);
        return Native.spotRequestChannelPart(handle, service,
          nativeMsg, handler, userData, flags, partFlag, timeoutMs);
    }

    private static RoutingId readRoutingIdPtr(MemorySegment nativeRidPtr) {
        if (nativeRidPtr == null || nativeRidPtr.address() == 0)
            return null;
        return readRoutingId(nativeRidPtr.reinterpret(
          NativeLayouts.ROUTING_ID_LAYOUT.byteSize()));
    }

    private static long timeoutMillis(Duration timeout) {
        return timeout == null ? 5_000L : Math.max(1L, timeout.toMillis());
    }

    private static int toTimeoutInt(long timeoutMs) {
        if (timeoutMs <= 1L)
            return 1;
        return timeoutMs >= Integer.MAX_VALUE ? Integer.MAX_VALUE
          : (int) timeoutMs;
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
        if (cause instanceof ZlinkRequestException requestException) {
            return requestException.getResult();
        }
        if (cause instanceof TimeoutException) {
            return RequestResult.TIMED_OUT;
        }
        return RequestResult.PROTOCOL_ERROR;
    }

    private static String requireChannelName(String channelName) {
        Objects.requireNonNull(channelName, "channelName");
        if (channelName.isEmpty()) {
            throw new IllegalArgumentException("channelName must not be empty");
        }
        return channelName;
    }

    private static void closeSnapshot(CallbackSubscribeData snapshot) {
        if (snapshot == null)
            return;
        for (Message part : snapshot.parts()) {
            if (part == null)
                continue;
            try {
                part.close();
            } catch (RuntimeException ignored) {
            }
        }
    }

    private static ExecutorService newCallbackExecutor() {
        return Executors.newSingleThreadExecutor(runnable -> {
            Thread thread = new Thread(runnable, "zlink-spot-callback");
            thread.setDaemon(true);
            return thread;
        });
    }

    private static void shutdownExecutor(ExecutorService executor) {
        if (executor != null)
            executor.shutdown();
    }

    private record CallbackSubscribeData(RoutingId routingId, String topicId,
                                         Message[] parts) {}

    private MemorySegment topicCString(String topic) {
        Objects.requireNonNull(topic, "topic");
        if (topic.isEmpty())
            throw new IllegalArgumentException("topic must not be empty");
        if (topic.length() >= TOPIC_CAPACITY)
            throw new IllegalArgumentException("topic too long");
        MemorySegment cached = topicCache.get(topic);
        if (cached != null)
            return cached;
        if (topicCache.size() >= TOPIC_CACHE_LIMIT)
            return topicScratchCString(topic);
        MemorySegment encoded = NativeHelpers.toCString(topicCacheArena, topic);
        MemorySegment previous = topicCache.putIfAbsent(topic, encoded);
        return previous == null ? encoded : previous;
    }

    private MemorySegment topicScratchCString(String value) {
        byte[] utf8 = value.getBytes(StandardCharsets.UTF_8);
        int needed = utf8.length + 1;
        MemorySegment scratch = topicScratch.get();
        int capacity = topicScratchCapacity.get();
        if (capacity < needed) {
            int grown = Math.max(capacity << 1, needed);
            scratch = Arena.ofAuto().allocate(grown);
            topicScratch.set(scratch);
            topicScratchCapacity.set(grown);
        }
        MemorySegment.copy(MemorySegment.ofArray(utf8), 0, scratch, 0,
          utf8.length);
        scratch.set(ValueLayout.JAVA_BYTE, utf8.length, (byte) 0);
        return scratch.asSlice(0, needed);
    }
}
