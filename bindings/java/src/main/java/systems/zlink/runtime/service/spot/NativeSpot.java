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
import systems.zlink.runtime.nativeapi.NativeLayouts;
import systems.zlink.runtime.nativeapi.NativeMessage;
import systems.zlink.runtime.nativeapi.NativeSubmitErrors;
import systems.zlink.runtime.nativeapi.RequestReplySupport;
import systems.zlink.runtime.nativeapi.RuntimeResources;
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
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.function.BiConsumer;

/**
 * Unified spot service handle aligned to the current core publish/subscribe
 * service model.
 */
public final class NativeSpot implements Spot {
    private static final int ERRNO_ENOTCONN = 107;
    private static final int ERRNO_ENOTCONN_WIN = 10057;
    private static final int ERRNO_EHOSTUNREACH = 113;
    private static final int ERRNO_EHOSTUNREACH_WIN = 10065;
    private static final int ERRNO_ETIMEDOUT = 110;
    private static final int ERRNO_ETIMEDOUT_WIN = 10060;
    private static final Linker LINKER = Linker.nativeLinker();
    private static final FunctionDescriptor FD_SEND_READY_CALLBACK =
      FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.ADDRESS);
    private static final Arena REQUEST_CALLBACK_ARENA = Arena.ofShared();

    private MemorySegment handle;
    private SendReadyHandler sendReadyHandler;
    private Arena sendReadyCallbackArena;
    private MemorySegment sendReadyCallbackStub = MemorySegment.NULL;
    private volatile ExecutorService callbackExecutor;
    private volatile RuntimeException callbackFailure;
    private final SpotSendPlane sendPlane;
    private final SpotRequestPlane requestPlane;
    private final SpotRoutedSupport routedSupport;
    private final SpotSubscriptionSupport subscriptionSupport;
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
        this.sendPlane = new SpotSendPlane(this);
        this.requestPlane = new SpotRequestPlane(this);
        this.routedSupport = new SpotRoutedSupport(this);
        this.subscriptionSupport = new SpotSubscriptionSupport(this);
        this.options = new SpotOptions(this);
    }

    NativeSpot(SpotNode node, MemorySegment handle) {
        Objects.requireNonNull(node, "node");
        Objects.requireNonNull(handle, "handle");
        if (handle.address() == 0)
            throw new IllegalArgumentException("spot handle must not be null");
        this.ownerNode = node;
        this.handle = handle;
        this.sendPlane = new SpotSendPlane(this);
        this.requestPlane = new SpotRequestPlane(this);
        this.routedSupport = new SpotRoutedSupport(this);
        this.subscriptionSupport = new SpotSubscriptionSupport(this);
        this.options = new SpotOptions(this);
    }

    NativeSpot(MemorySegment handle) {
        Objects.requireNonNull(handle, "handle");
        if (handle.address() == 0)
            throw new IllegalArgumentException("spot handle must not be null");
        this.ownerNode = null;
        this.handle = handle;
        this.sendPlane = new SpotSendPlane(this);
        this.requestPlane = new SpotRequestPlane(this);
        this.routedSupport = new SpotRoutedSupport(this);
        this.subscriptionSupport = new SpotSubscriptionSupport(this);
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
                      : RequestReplySupport.requestResult(error), response);
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
        return sendPlane.publish(topicId, part, nonBlocking);
    }

    private boolean publishInternal(String topicId, List<Message> parts,
                                 boolean nonBlocking) {
        return sendPlane.publish(topicId, parts, nonBlocking);
    }

    private boolean sendChannelInternal(String channelName, List<Message> parts,
                                     boolean nonBlocking) {
        return sendPlane.sendToChannel(channelName, parts, nonBlocking);
    }

    private CompletableFuture<List<Message>> requestChannelInternal(
      String channelName, List<Message> parts, Duration timeout,
      SendFlags flags) {
        return requestPlane.requestToChannel(channelName, parts, timeout, flags);
    }

    MemorySegment topicCString(String topic) {
        return sendPlane.topicCString(topic);
    }

    private ZlinkSubmitException submitFailure(String apiName) {
        int errno = Native.errno();
        ZlinkSubmitException submit = NativeSubmitErrors.submitExceptionOrNull(errno);
        if (submit != null)
            return submit;
        throw InternalAccess.zlinkExceptionFromLastError(apiName);
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

    private static MemorySegment spotDispatchSubject(SpotDispatchInfo info) {
        Object state = ContractAccess.spotDispatchSubjectState(info);
        return state instanceof MemorySegment segment ? segment
          : MemorySegment.NULL;
    }

    /** Subscribes to one topic or pattern string. */
    public void setSubscription(String topicId) {
        subscriptionSupport.setSubscription(topicId);
    }

    /** Removes a topic or pattern subscription. */
    public void unsetSubscription(String topicIdOrPattern) {
        subscriptionSupport.unsetSubscription(topicIdOrPattern);
    }

    public Optional<SubscriptionEntry> subscriptionAt(int index) {
        return subscriptionSupport.subscriptionAt(index);
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
            RuntimeResources.closeArena(sendReadyCallbackArena);
            sendReadyCallbackArena = arena;
            sendReadyCallbackStub = stub;
            sendReadyHandler = handler;
        } finally {
            if (!success) {
                if (createdExecutor) {
                    callbackExecutor = null;
                    RuntimeResources.shutdownExecutor(executor);
                }
                RuntimeResources.closeArena(arena);
            }
        }
    }

    public boolean subscribe(TopicMessage result, RecvFlags flags) {
        Objects.requireNonNull(result, "result");
        Objects.requireNonNull(flags, "flags");
        return subscriptionSupport.subscribe(result, flags);
    }

    Optional<TopicMessage> subscribeNoWait() {
        return subscriptionSupport.subscribeNoWait();
    }

    /** Receives the next subscription event for this spot. */
    public boolean receiveSubscriptionEvent(SubscriptionEvent result,
                                            RecvFlags flags) {
        Objects.requireNonNull(result, "result");
        Objects.requireNonNull(flags, "flags");
        return subscriptionSupport.receiveSubscriptionEvent(result, flags);
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
        Arena readyArena = sendReadyCallbackArena;
        sendReadyHandler = null;
        callbackFailure = null;
        callbackExecutor = null;
        sendReadyCallbackArena = null;
        MemorySegment readyStub = sendReadyCallbackStub;
        sendReadyCallbackStub = MemorySegment.NULL;
        handle = MemorySegment.NULL;
        Native.spotDestroy(currentHandle);
        sendPlane.close();
        routedSupport.close();
        subscriptionSupport.close();
        if (ownerNode != null) {
            InternalAccess.spotNodeReleaseSpot(ownerNode, this);
        }
        RuntimeResources.shutdownExecutor(executor);
        RuntimeResources.closeArena(readyArena);
        readyStub = MemorySegment.NULL;
    }

    private static int boundedCount(long value) {
        if (value <= 0)
            return 0;
        if (value > Integer.MAX_VALUE)
            return Integer.MAX_VALUE;
        return (int) value;
    }

    void ensureOpen() {
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

    private void recordCallbackFailure(RuntimeException failure) {
        callbackFailure = failure;
        Thread current = Thread.currentThread();
        Thread.UncaughtExceptionHandler uncaught =
          current.getUncaughtExceptionHandler();
        if (uncaught != null)
            uncaught.uncaughtException(current, failure);
    }

    static String requireChannelName(String channelName) {
        Objects.requireNonNull(channelName, "channelName");
        if (channelName.isEmpty()) {
            throw new IllegalArgumentException("channelName must not be empty");
        }
        return channelName;
    }

    private static ExecutorService newCallbackExecutor() {
        return RuntimeResources.daemonSingleThreadExecutor(
            "zlink-spot-callback");
    }
}
