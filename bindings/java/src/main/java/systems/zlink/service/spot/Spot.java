/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.service.spot;

import systems.zlink.Message;
import systems.zlink.ConfigException;
import systems.zlink.ConfigResult;
import systems.zlink.RequestResult;
import systems.zlink.RequestCallback;
import systems.zlink.Received;
import systems.zlink.RoutingId;
import systems.zlink.RecvException;
import systems.zlink.RecvFlags;
import systems.zlink.RecvResult;
import systems.zlink.RequestException;
import systems.zlink.SendFlags;
import systems.zlink.SendReadyHandler;
import systems.zlink.TopicMessage;
import systems.zlink.SubscriptionEntry;
import systems.zlink.SubscriptionEvent;
import systems.zlink.ZlinkException;
import systems.zlink.SpotDispatchEvent;
import systems.zlink.SpotDispatchEventHandler;
import systems.zlink.SpotDispatchInfo;
import systems.zlink.SpotDispatchSubjectKind;
import systems.zlink.SpotRoutedHandler;
import systems.zlink.SubmitException;
import systems.zlink.SubmitResult;
import systems.zlink.internal.ActorInterop;
import systems.zlink.internal.Native;
import systems.zlink.internal.InternalAccess;
import systems.zlink.internal.MessagePartsBuffer;
import systems.zlink.internal.NativeHelpers;
import systems.zlink.internal.NativeLayouts;
import systems.zlink.internal.NativeMsg;
import systems.zlink.internal.RequestProgressPump;
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
public final class Spot implements AutoCloseable {
    private static final long MSG_SIZE = NativeLayouts.MSG_LAYOUT.byteSize();
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
              Spot.class, "handleReplyCallback",
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

    /** Creates a unified spot facade bound to the supplied node. */
    Spot(SpotNode node) {
        Objects.requireNonNull(node, "node");
        MemorySegment nativeHandle = Native.spotNew(node.handle());
        if (nativeHandle == null || nativeHandle.address() == 0)
            throw InternalAccess.zlinkExceptionFromLastError("zlink_spot_new");
        this.ownerNode = node;
        this.handle = nativeHandle;
        this.routedSupport = new SpotRoutedSupport(this);
        this.options = new SpotOptions(this);
    }

    Spot(SpotNode node, MemorySegment handle) {
        Objects.requireNonNull(node, "node");
        Objects.requireNonNull(handle, "handle");
        if (handle.address() == 0)
            throw new IllegalArgumentException("spot handle must not be null");
        this.ownerNode = node;
        this.handle = handle;
        this.routedSupport = new SpotRoutedSupport(this);
        this.options = new SpotOptions(this);
    }

    Spot(MemorySegment handle) {
        Objects.requireNonNull(handle, "handle");
        if (handle.address() == 0)
            throw new IllegalArgumentException("spot handle must not be null");
        this.ownerNode = null;
        this.handle = handle;
        this.routedSupport = new SpotRoutedSupport(this);
        this.options = new SpotOptions(this);
    }

    /** Returns the native spot handle. */
    MemorySegment handle() {
        return handle;
    }

    /** Internal bridge for binding helpers. */
    MemorySegment handleInternal() {
        return handle();
    }

    MemorySegment ownerNodeHandleInternal() {
        return ownerNode == null ? MemorySegment.NULL : ownerNode.handleInternal();
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
    public RoutingId routingId() {
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

    public SendOp publish(String topicId) {
        return new SendBuilder((parts, flags) ->
            publish(topicId, parts, flags));
    }

    public SendOp sendChannel(String channelName) {
        return new SendBuilder((parts, flags) ->
            sendChannel(channelName, parts, flags));
    }

    public SendOp sendToSpot(RoutingId destNodeRid, RoutingId destSpotRid) {
        return new SendBuilder((parts, flags) ->
            sendToSpot(destNodeRid, destSpotRid, parts, flags));
    }

    public RequestOp requestChannel(String channelName) {
        return new RequestBuilder((parts, timeout, flags) ->
            requestChannel(channelName, parts, timeout),
          (parts, callback, flags, timeout) ->
            requestChannel(channelName, parts, callback::onComplete, flags,
              timeout));
    }

    public RequestOp requestToSpot(RoutingId destNodeRid,
                                   RoutingId destSpotRid) {
        return new RequestBuilder((parts, timeout, flags) ->
            routedSupport.requestToSpot(destNodeRid, destSpotRid, parts,
              timeout, flags),
          (parts, callback, flags, timeout) ->
            routedSupport.requestToSpot(destNodeRid, destSpotRid, parts,
              callback::onComplete, flags, timeout));
    }

    public RequestOp requestToRouter(RoutingId peerRid) {
        return new RequestBuilder((parts, timeout, flags) ->
            routedSupport.requestToRouter(peerRid, parts, timeout, flags),
          (parts, callback, flags, timeout) ->
            routedSupport.requestToRouter(peerRid, parts,
              callback::onComplete, flags, timeout));
    }

    public ReplyOp replyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                               long requestSeq) {
        return new ReplyBuilder((parts, flags) ->
            replyToSpot(destNodeRid, destSpotRid, requestSeq, parts, flags));
    }

    public ReplyOp replyToRouter(RoutingId peerRid, long requestSeq) {
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
        } catch (SubmitException ex) {
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
        } catch (SubmitException ex) {
            if (flags == SendFlags.DONT_WAIT
                && ex.getResult() == SubmitResult.BACKPRESSURED) {
                return false;
            }
            throw ex;
        }
    }

    boolean sendChannel(String channelName, Message part) {
        return sendChannel(channelName, part, SendFlags.NONE);
    }

    boolean sendChannel(String channelName, Message part, SendFlags flags) {
        Objects.requireNonNull(part, "part");
        return sendChannel(channelName, List.of(part), flags);
    }

    boolean sendChannel(String channelName, List<Message> parts) {
        return sendChannel(channelName, parts, SendFlags.NONE);
    }

    boolean sendChannel(String channelName, List<Message> parts,
                            SendFlags flags) {
        Objects.requireNonNull(flags, "flags");
        try {
            return sendChannelInternal(channelName, parts,
              flags == SendFlags.DONT_WAIT);
        } catch (SubmitException ex) {
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
        } catch (SubmitException ex) {
            if (flags == SendFlags.DONT_WAIT
                && ex.getResult() == SubmitResult.BACKPRESSURED) {
                return false;
            }
            throw ex;
        }
    }

    CompletableFuture<List<Message>> requestChannel(String channelName,
                                                           Message part) {
        return requestChannel(channelName, List.of(part));
    }

    private CompletableFuture<List<Message>> requestChannel(String channelName,
                                                            Message part,
                                                            SendFlags flags) {
        return requestChannel(channelName, List.of(part), flags);
    }

    CompletableFuture<List<Message>> requestChannel(String channelName,
                                                           Message part,
                                                           Duration timeout) {
        return requestChannel(channelName, List.of(part), timeout);
    }

    private CompletableFuture<List<Message>> requestChannel(String channelName,
                                                            Message part,
                                                            SendFlags flags,
                                                            Duration timeout) {
        return requestChannel(channelName, List.of(part), flags, timeout);
    }

    CompletableFuture<List<Message>> requestChannel(String channelName,
                                                           List<Message> parts) {
        return requestChannel(channelName, parts, SendFlags.NONE);
    }

    private CompletableFuture<List<Message>> requestChannel(String channelName,
                                                            List<Message> parts,
                                                            SendFlags flags) {
        return requestChannel(channelName, parts, flags,
          Duration.ofMillis(5_000L));
    }

    CompletableFuture<List<Message>> requestChannel(String channelName,
                                                           List<Message> parts,
                                                           Duration timeout) {
        return requestChannel(channelName, parts, SendFlags.NONE, timeout);
    }

    private CompletableFuture<List<Message>> requestChannel(String channelName,
                                                            List<Message> parts,
                                                            SendFlags flags,
                                                            Duration timeout) {
        Objects.requireNonNull(flags, "flags");
        return requestChannelInternal(channelName, parts, timeout, flags);
    }

    boolean requestChannel(String channelName, Message part,
                                  BiConsumer<RequestResult, List<Message>> callback) {
        return requestChannel(channelName, List.of(part), callback,
          SendFlags.NONE, Duration.ofMillis(5_000L));
    }

    boolean requestChannel(String channelName, Message part,
                                  BiConsumer<RequestResult, List<Message>> callback,
                                  SendFlags flags) {
        return requestChannel(channelName, List.of(part), callback, flags,
          Duration.ofMillis(5_000L));
    }

    boolean requestChannel(String channelName, Message part,
                                  BiConsumer<RequestResult, List<Message>> callback,
                                  Duration timeout) {
        return requestChannel(channelName, List.of(part), callback,
          SendFlags.NONE, timeout);
    }

    boolean requestChannel(String channelName, Message part,
                                  BiConsumer<RequestResult, List<Message>> callback,
                                  SendFlags flags, Duration timeout) {
        return requestChannel(channelName, List.of(part), callback, flags, timeout);
    }

    boolean requestChannel(String channelName, List<Message> parts,
                                  BiConsumer<RequestResult, List<Message>> callback) {
        return requestChannel(channelName, parts, callback, SendFlags.NONE,
          Duration.ofMillis(5_000L));
    }

    boolean requestChannel(String channelName, List<Message> parts,
                                  BiConsumer<RequestResult, List<Message>> callback,
                                  SendFlags flags) {
        return requestChannel(channelName, parts, callback, flags,
          Duration.ofMillis(5_000L));
    }

    boolean requestChannel(String channelName, List<Message> parts,
                                  BiConsumer<RequestResult, List<Message>> callback,
                                  Duration timeout) {
        return requestChannel(channelName, parts, callback, SendFlags.NONE,
          timeout);
    }

    boolean requestChannel(String channelName, List<Message> parts,
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
        } catch (SubmitException ex) {
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
        for (int i = 0; i < parts.size(); i++) {
            int partFlag = i + 1 < parts.size()
                ? Native.PART_MORE : Native.PART_FINAL;
            while (true) {
                int rc = spotPublishPartOnce(topicId,
                    Objects.requireNonNull(parts.get(i), "parts[" + i + "]"),
                    nonBlocking ? SEND_DONTWAIT : 0, partFlag);
                if (rc == 0)
                    break;
                int errno = Native.errno();
                if (errno == ERRNO_EINTR)
                    continue;
                throw submitFailure("zlink_spot_publish_part");
            }
        }
        return true;
    }

    private boolean sendChannelInternal(String channelName, List<Message> parts,
                                     boolean nonBlocking) {
        validateMessages(parts, "parts");
        if (!nonBlocking && InternalAccess.inCallback()) {
            throw new IllegalStateException(
                "blocking sendChannel is not supported from callback context; use non-blocking send");
        }
        for (int i = 0; i < parts.size(); i++) {
            int partFlag = i + 1 < parts.size()
                ? Native.PART_MORE : Native.PART_FINAL;
            while (true) {
                int rc = spotSendChannelPartOnce(channelName,
                    Objects.requireNonNull(parts.get(i), "parts[" + i + "]"),
                    nonBlocking ? SEND_DONTWAIT : 0, partFlag);
                if (rc == 0)
                    break;
                int errno = Native.errno();
                if (errno == ERRNO_EINTR)
                    continue;
                throw submitFailure("zlink_spot_send_channel_part");
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
        int rc = Native.spotChannelReplyProgressFrom(handle, dealerSubject);
        if (rc != 0) {
            throw InternalAccess.zlinkExceptionFromLastError(
              "zlink_spot_channel_reply_progress_from");
        }
    }

    void drainChannelReply(SpotDispatchInfo info) {
        Objects.requireNonNull(info, "info");
        if (info.event() != SpotDispatchEvent.CHANNEL_REPLY_READABLE
            || info.subjectKind() != SpotDispatchSubjectKind.CHANNEL_DEALER) {
            throw new ConfigException(ConfigResult.INVALID_ARGUMENT);
        }
        MemorySegment subject = InternalAccess.spotDispatchSubject(info);
        if (subject == null || subject.address() == 0) {
            throw new ConfigException(ConfigResult.INVALID_HANDLE);
        }
        drainChannelReplyFrom(subject);
    }

    private SubmitException submitFailure(String apiName) {
        int errno = Native.errno();
        if (errno == ERRNO_EAGAIN || errno == ERRNO_EWOULDBLOCK_WIN)
            return new SubmitException(SubmitResult.BACKPRESSURED, errno);
        if (errno == ERRNO_ENOTCONN || errno == ERRNO_ENOTCONN_WIN
            || errno == ERRNO_EHOSTUNREACH || errno == ERRNO_EHOSTUNREACH_WIN) {
            return new SubmitException(SubmitResult.NOT_CONNECTED, errno);
        }
        throw InternalAccess.zlinkExceptionFromLastError(apiName);
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
    public void onSendReady(SendReadyHandler handler) {
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

    public TopicMessage subscribe() {
        return receiveTopicMessage(false).orElseThrow(
            () -> new IllegalStateException(
                "blocking subscribe returned no delivery"));
    }

    public TopicMessage subscribe(RecvFlags flags) {
        Objects.requireNonNull(flags, "flags");
        if (flags == RecvFlags.DONT_WAIT) {
            return receiveTopicMessage(true).orElse(null);
        }
        return receiveTopicMessage(false).orElseThrow(
            () -> new IllegalStateException(
                "blocking subscribe returned no delivery"));
    }

    Optional<TopicMessage> subscribeNoWait() {
        return receiveTopicMessage(true);
    }

    /** Receives the next subscription event for this spot. */
    public SubscriptionEvent receiveSubscriptionEvent() {
        return receiveSubscriptionEvent(RecvFlags.NONE);
    }

    public SubscriptionEvent receiveSubscriptionEvent(RecvFlags flags) {
        Objects.requireNonNull(flags, "flags");
        return receiveSubscriptionEvent(flags == RecvFlags.DONT_WAIT)
          .orElseThrow(() -> new RecvException(RecvResult.NO_DATA,
            ERRNO_EAGAIN));
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

    public Received recvRouted() {
        return routedSupport.recvRouted(RecvFlags.NONE);
    }

    public Received recvRouted(RecvFlags flags) {
        return routedSupport.recvRouted(flags);
    }

    public void onRoutedReceive(SpotRoutedHandler handler) {
        routedSupport.onRoutedReceive(handler);
    }

    public void onDispatchEvent(SpotDispatchEventHandler handler) {
        routedSupport.onDispatchEvent(handler);
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
                    throw new RecvException(RecvResult.fromValue(rc));
                }
                MemorySegment parts = partsOut.get(ValueLayout.ADDRESS, 0);
                long partCount = partCountOut.get(ValueLayout.JAVA_LONG, 0);
                Message[] messages = partCount > 0
                  ? InternalAccess.messageFromOwnedMsgVector(parts, partCount)
                  : new Message[] { new Message() };
                NativeMsg.multipartClose(parts, partCount);
                message = messages[0];
                for (int i = 1; i < messages.length; i++) {
                    messages[i].close();
                }
                success = true;
                return new ActorJoinRequest(readActorJoinInfo(infoOut),
                  message);
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
     * builder; a zero-message submit is allowed.
     */
    public ActorJoinReplyOp replyActorJoin(ActorJoinRequest request,
                                           boolean accepted) {
        Objects.requireNonNull(request, "request");
        ensureOpen();
        return new ActorJoinReplyBuilder(request, accepted);
    }

    /**
     * Register Actor lifecycle callbacks on this Spot. Passing {@code null}
     * for both removes the registration.
     */
    public void onActorLifecycle(ActorLifecycleHandler onJoin,
                                 ActorLifecycleHandler onLeave) {
        ensureOpen();
        if (onJoin == null && onLeave == null) {
            long old = lifecycleRegistrationId;
            lifecycleRegistrationId = 0L;
            if (old != 0L) {
                int rc = Native.spotActorLifecycleHandler(handle,
                  MemorySegment.NULL, MemorySegment.NULL,
                  MemorySegment.NULL);
                ActorRequestCallbacks.unregisterLifecycle(old);
                if (rc != 0) {
                    throw new systems.zlink.HandlerException(
                      systems.zlink.HandlerResult.fromValue(rc));
                }
            }
            return;
        }
        Spot self = this;
        long id = ActorRequestCallbacks.registerLifecycle(
          onJoin == null ? null : (spotHandle, info) ->
            onJoin.onLifecycleEvent(self, info),
          onLeave == null ? null : (spotHandle, info) ->
            onLeave.onLifecycleEvent(self, info));
        long previous = lifecycleRegistrationId;
        int rc = Native.spotActorLifecycleHandler(handle,
          onJoin == null ? MemorySegment.NULL
            : ActorRequestCallbacks.ACTOR_LIFECYCLE_JOIN_CALLBACK,
          onLeave == null ? MemorySegment.NULL
            : ActorRequestCallbacks.ACTOR_LIFECYCLE_LEAVE_CALLBACK,
          MemorySegment.ofAddress(id));
        if (rc != 0) {
            ActorRequestCallbacks.unregisterLifecycle(id);
            throw new systems.zlink.HandlerException(
              systems.zlink.HandlerResult.fromValue(rc));
        }
        if (previous != 0L) {
            ActorRequestCallbacks.unregisterLifecycle(previous);
        }
        lifecycleRegistrationId = id;
    }

    private long lifecycleRegistrationId;

    private final class ActorJoinReplyBuilder implements ActorJoinReplyOp {
        private final ActorJoinRequest request;
        private final boolean accepted;
        private final MessagePartsBuffer parts = new MessagePartsBuffer();
        private boolean submitted;

        ActorJoinReplyBuilder(ActorJoinRequest request, boolean accepted) {
            this.request = request;
            this.accepted = accepted;
        }

        @Override
        public ActorJoinReplyOp message(Message part) {
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
                MemorySegment partsArr = MemorySegment.NULL;
                int allocated = 0;
                if (!parts.isEmpty()) {
                    partsArr = arena.allocate(NativeLayouts.MSG_LAYOUT,
                      parts.size());
                    long stride = NativeLayouts.MSG_LAYOUT.byteSize();
                    for (int i = 0; i < parts.size(); i++) {
                        InternalAccess.messageCopyTo(parts.get(i),
                          partsArr.asSlice(i * stride, stride));
                        allocated++;
                    }
                }
                int rc = Native.spotActorJoinReply(handle, nativeInfo,
                  accepted ? 1 : 0, partsArr, parts.size());
                if (rc != 0) {
                    long stride = NativeLayouts.MSG_LAYOUT.byteSize();
                    for (int i = 0; i < allocated; i++) {
                        NativeMsg.msgClose(partsArr.asSlice(i * stride, stride));
                    }
                    throw new SubmitException(SubmitResult.fromValue(rc));
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
        return ActorJoinInfo.fromNative(
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
          NativeLayouts.ROUTING_ID_LAYOUT.byteSize()), info.sourceNodeRidRaw());
        writeRoutingId(out.asSlice(
          NativeLayouts.ACTOR_JOIN_INFO_SOURCE_SPOT_RID_OFFSET,
          NativeLayouts.ROUTING_ID_LAYOUT.byteSize()), info.sourceSpotRidRaw());
        writeRoutingId(out.asSlice(
          NativeLayouts.ACTOR_JOIN_INFO_TARGET_NODE_RID_OFFSET,
          NativeLayouts.ROUTING_ID_LAYOUT.byteSize()), info.targetNodeRidRaw());
        writeRoutingId(out.asSlice(
          NativeLayouts.ACTOR_JOIN_INFO_TARGET_SPOT_RID_OFFSET,
          NativeLayouts.ROUTING_ID_LAYOUT.byteSize()), info.targetSpotRidRaw());
        out.set(ValueLayout.JAVA_LONG_UNALIGNED,
          NativeLayouts.ACTOR_JOIN_INFO_JOIN_EPOCH_OFFSET, info.joinEpoch());
        out.set(ValueLayout.ADDRESS,
          NativeLayouts.ACTOR_JOIN_INFO_REQUEST_OFFSET, info.request());
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

    public List<ActorRef> actorsSnapshot() {
        ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment count = arena.allocate(ValueLayout.JAVA_LONG);
            int rc = Native.spotActorsSnapshot(handle, MemorySegment.NULL,
              count);
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError("zlink_spot_actors_snapshot");
            }
            int available = boundedCount(count.get(ValueLayout.JAVA_LONG, 0));
            if (available == 0) {
                return List.of();
            }
            MemorySegment entries = arena.allocate(
              NativeLayouts.ACTOR_REF_LAYOUT, available);
            count.set(ValueLayout.JAVA_LONG, 0, available);
            rc = Native.spotActorsSnapshot(handle, entries, count);
            if (rc != 0) {
                throw InternalAccess.zlinkExceptionFromLastError("zlink_spot_actors_snapshot");
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

    private final class SendBuilder implements SendOp, SendSubmitOp {
        private final SendInvoker invoker;
        private final MessagePartsBuffer parts = new MessagePartsBuffer();
        private SendFlags flags = SendFlags.NONE;
        private boolean submitted;

        private SendBuilder(SendInvoker invoker) {
            this.invoker = invoker;
        }

        @Override
        public SendSubmitOp message(Message part) {
            ensureNotSubmitted();
            parts.add(Objects.requireNonNull(part, "part"));
            return this;
        }

        @Override
        public SendSubmitOp flags(SendFlags value) {
            ensureNotSubmitted();
            flags = Objects.requireNonNull(value, "flags");
            return this;
        }

        @Override
        public boolean submit() {
            markSubmitted();
            return invoker.submit(parts.asList(), flags);
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

    private final class RequestBuilder implements RequestOp, RequestSubmitOp {
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
        public RequestSubmitOp message(Message part) {
            addMessage(part);
            return this;
        }

        @Override
        public RequestSubmitOp timeout(Duration value) {
            ensureNotSubmitted();
            timeout = Objects.requireNonNull(value, "timeout");
            return this;
        }

        @Override
        public RequestCallbackSubmitOp flags(SendFlags value) {
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
      implements RequestCallbackSubmitOp {
        private final RequestBuilder source;
        private SendFlags flags;

        private CallbackRequestBuilder(RequestBuilder source,
                                       SendFlags flags) {
            this.source = source;
            this.flags = flags;
        }

        @Override
        public RequestCallbackSubmitOp message(Message part) {
            source.addMessage(part);
            return this;
        }

        @Override
        public RequestCallbackSubmitOp timeout(Duration timeout) {
            source.timeout(timeout);
            return this;
        }

        @Override
        public RequestCallbackSubmitOp flags(SendFlags value) {
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

    private final class ReplyBuilder implements ReplyOp, ReplySubmitOp {
        private final ReplyInvoker invoker;
        private final MessagePartsBuffer parts = new MessagePartsBuffer();
        private SendFlags flags = SendFlags.NONE;
        private boolean submitted;

        private ReplyBuilder(ReplyInvoker invoker) {
            this.invoker = invoker;
        }

        @Override
        public ReplySubmitOp message(Message part) {
            ensureNotSubmitted();
            parts.add(Objects.requireNonNull(part, "part"));
            return this;
        }

        @Override
        public ReplySubmitOp flags(SendFlags value) {
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
            ownerNode.releaseSpot(this);
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
            return MethodHandles.lookup().findVirtual(Spot.class, name, type)
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
        Message[] snapshotParts = InternalAccess.messageFromOwnedMsgVectorShared(
            parts, partCount);
        NativeMsg.multipartClose(parts, partCount);
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
                    throw new RecvException(result, Native.errno());
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
                    throw new RecvException(result, Native.errno());
                }
                if (result == RecvResult.BUSY && nonBlocking) {
                    return Optional.empty();
                }
                throw new RecvException(result, Native.errno());
            }
        }
    }

    private static void closeMsgVector(MemorySegment vec, int count) {
        for (int i = 0; i < count; i++) {
            MemorySegment msg = vec.asSlice((long) i * MSG_SIZE, MSG_SIZE);
            NativeMsg.msgClose(msg);
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

    private int spotPublishPartOnce(String topicId, Message part, int flags,
                                    int partFlag) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment topic = topicCString(topicId);
            MemorySegment nativeMsg = arena.allocate(NativeLayouts.MSG_LAYOUT);
            Object anchor = InternalAccess.messageTransferTo(part, nativeMsg);
            try {
                int rc = Native.spotPublishPart(handle, topic, nativeMsg, flags,
                  partFlag);
                if (rc != 0) {
                    InternalAccess.messageRestoreFromNative(part, nativeMsg,
                        false, anchor);
                }
                return rc;
            } catch (RuntimeException ex) {
                InternalAccess.messageRestoreFromNative(part, nativeMsg, false,
                    anchor);
                throw ex;
            }
        }
    }

    private int spotSendChannelPartOnce(String channelName, Message part,
                                        int flags, int partFlag) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment service = NativeHelpers.toCString(arena,
              requireChannelName(channelName));
            MemorySegment nativeMsg = arena.allocate(NativeLayouts.MSG_LAYOUT);
            Object anchor = InternalAccess.messageTransferTo(part, nativeMsg);
            try {
                int rc = Native.spotSendChannelPart(handle, service, nativeMsg,
                  flags, partFlag);
                if (rc != 0) {
                    InternalAccess.messageRestoreFromNative(part, nativeMsg,
                        false, anchor);
                }
                return rc;
            } catch (RuntimeException ex) {
                InternalAccess.messageRestoreFromNative(part, nativeMsg, false,
                    anchor);
                throw ex;
            }
        }
    }

    private void submitSpotRequestChannel(String channelName,
                                          List<Message> payload,
                                          MemorySegment handler,
                                          MemorySegment userData,
                                          int flags,
                                          int timeoutMs) {
        for (int i = 0; i < payload.size(); i++) {
            int partFlag = i + 1 < payload.size()
              ? Native.PART_MORE : Native.PART_FINAL;
            while (true) {
                int rc = spotRequestChannelPartOnce(channelName, payload.get(i),
                  i + 1 < payload.size() ? MemorySegment.NULL : handler,
                  i + 1 < payload.size() ? MemorySegment.NULL : userData,
                  flags, partFlag, i + 1 < payload.size() ? 0 : timeoutMs);
                if (rc == 0)
                    break;
                int errno = Native.errno();
                if (errno == ERRNO_EINTR)
                    continue;
                throw submitFailure("zlink_spot_request_channel_part");
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
            MemorySegment nativeMsg = arena.allocate(NativeLayouts.MSG_LAYOUT);
            InternalAccess.messageCopyTo(part, nativeMsg);
            return Native.spotRequestChannelPart(handle, service,
              nativeMsg, handler, userData, flags, partFlag, timeoutMs);
        }
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
        if (cause instanceof RequestException requestException) {
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
