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
import systems.zlink.runtime.messaging.MessageOperations;
import systems.zlink.runtime.nativeapi.Native;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.nativeapi.NativeLayouts;
import systems.zlink.runtime.nativeapi.NativeListSnapshots;
import systems.zlink.runtime.nativeapi.NativeSubmitErrors;
import systems.zlink.runtime.nativeapi.RequestReplySupport;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.time.Duration;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.function.BiConsumer;

/**
 * Unified spot service handle aligned to the current core publish/subscribe
 * service model.
 */
public final class NativeSpot implements Spot {
    private MemorySegment handle;
    private final SpotSendPlane sendPlane;
    private final SpotRequestPlane requestPlane;
    private final SpotRoutedSupport routedSupport;
    private final SpotSubscriptionSupport subscriptionSupport;
    private final SpotActorJoinSupport actorJoinSupport;
    private final SpotSendReadySupport sendReadySupport;
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
        this.actorJoinSupport = new SpotActorJoinSupport(this);
        this.sendReadySupport = new SpotSendReadySupport();
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
        this.actorJoinSupport = new SpotActorJoinSupport(this);
        this.sendReadySupport = new SpotSendReadySupport();
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
        this.actorJoinSupport = new SpotActorJoinSupport(this);
        this.sendReadySupport = new SpotSendReadySupport();
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
        return MessageOperations.send(
          (part, flags) -> publish(topicId, part, flags),
          (parts, flags) -> publish(topicId, parts, flags));
    }

    public SendOperation sendToChannel(String channelName) {
        return MessageOperations.send(
          (part, flags) -> sendToChannel(channelName, part, flags),
          (parts, flags) -> sendToChannel(channelName, parts, flags));
    }

    public SendOperation sendToSpot(RoutingId destNodeRid, RoutingId destSpotRid) {
        return MessageOperations.send(
          (part, flags) -> sendToSpot(destNodeRid, destSpotRid, part, flags),
          (parts, flags) -> sendToSpot(destNodeRid, destSpotRid, parts, flags));
    }

    public RequestOperation requestToChannel(String channelName) {
        return MessageOperations.request((parts, flags, timeout) ->
            requestToChannel(channelName, parts, flags, timeout),
          (parts, callback, flags, timeout) ->
            requestToChannel(channelName, parts, callback::onComplete, flags,
              timeout));
    }

    public RequestOperation requestToSpot(RoutingId destNodeRid,
                                   RoutingId destSpotRid) {
        return MessageOperations.request((parts, flags, timeout) ->
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
        return MessageOperations.request((parts, flags, timeout) ->
            routedSupport.requestToRouter(peerRid, parts, timeout, flags),
          (parts, callback, flags, timeout) ->
            routedSupport.requestToRouter(peerRid, parts,
              callback::onComplete, flags, timeout));
    }

    public ReplyOperation replyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                               long requestSeq) {
        return MessageOperations.reply((parts, flags) ->
            replyToSpot(destNodeRid, destSpotRid, requestSeq, parts, flags));
    }

    public ReplyOperation replyToRouter(RoutingId peerRid, long requestSeq) {
        return MessageOperations.reply((parts, flags) ->
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
        sendReadySupport.install(handle, handler);
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
        ContractAccess.receivedAdoptFrom(result, fresh);
        return true;
    }

    public void setDispatchHandler(SpotDispatchEventHandler handler) {
        routedSupport.setDispatchHandler(handler);
    }

    public ActorJoinRequest recvActorJoin(RecvFlags flags) {
        return actorJoinSupport.recvActorJoin(flags);
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
        return actorJoinSupport.replyActorJoin(request, joinResultCode);
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

    public List<ActorRef> actors() {
        ensureOpen();
        return NativeListSnapshots.read(
          NativeLayouts.ACTOR_REF_LAYOUT,
          "zlink_spot_actors",
          (arena, entries, count) -> Native.spotActors(handle, entries,
            count),
          ActorInterop::actorRefFromNative);
    }

    @Override
    public void close() {
        MemorySegment currentHandle = handle;
        if (currentHandle == null || currentHandle.address() == 0) {
            return;
        }
        handle = MemorySegment.NULL;
        Native.spotDestroy(currentHandle);
        sendPlane.close();
        routedSupport.close();
        subscriptionSupport.close();
        sendReadySupport.close();
        if (ownerNode != null) {
            InternalAccess.spotNodeReleaseSpot(ownerNode, this);
        }
    }

    void ensureOpen() {
        if (handle == null || handle.address() == 0)
            throw new IllegalStateException("spot is closed");
        ensureNoCallbackFailure();
    }

    private void ensureNoCallbackFailure() {
        sendReadySupport.ensureNoCallbackFailure();
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

    static String requireChannelName(String channelName) {
        Objects.requireNonNull(channelName, "channelName");
        if (channelName.isEmpty()) {
            throw new IllegalArgumentException("channelName must not be empty");
        }
        return channelName;
    }

}
