package systems.zlink.framework.runtime.binding;

import java.time.Duration;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;
import java.util.function.Consumer;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.spot.PublishDetail;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.framework.channels.ZLinkSubmitStatus;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorJoinEntrySpotResult;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorJoinRequest;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorJoinResult;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorLifecycleEvent;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorLifecycleEventKind;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorLifecycleInfo;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorReceived;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorRef;
import systems.zlink.framework.runtime.backend.ZLinkBackendRequestCallback;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpot;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpotRouteBridge;
import systems.zlink.framework.runtime.internal.backend.ZLinkInternalSpotNode;
import systems.zlink.framework.runtime.internal.backend.ZLinkMeshApplicationReceiver;
import systems.zlink.framework.runtime.service.ZLinkServiceM6BWireCodec;

/**
 * Framework-owned service runtime projected over the raw MeshNode transport.
 * Stateful Spot and Actor identity remains inside the Framework runtime.
 */
final class ZLinkJavaRawSpotNode implements ZLinkInternalSpotNode {
    private final ZLinkJavaRawMeshNode owner;
    private final Map<RoutingId, ZLinkJavaRawSpot> spots =
        new ConcurrentHashMap<>();
    private final Map<String, ZLinkBackendActorRef> actors =
        new ConcurrentHashMap<>();
    private final Map<String, RoutingId> actorSpots =
        new ConcurrentHashMap<>();
    private final Map<String, Long> actorMembershipEpochs =
        new ConcurrentHashMap<>();
    private final AtomicLong nextGeneration = new AtomicLong(1);
    private final AtomicLong nextRequestSequence = new AtomicLong(1);
    private final AtomicLong nextActorRequestSequence = new AtomicLong(1);
    private final Map<Long, CompletableFuture<List<Message>>> actorRequests =
        new ConcurrentHashMap<>();
    private final Map<Long, Consumer<List<Message>>> actorRemoteReplies =
        new ConcurrentHashMap<>();
    private final Map<String, StreamBinding> streamBindings =
        new ConcurrentHashMap<>();
    private final ZLinkJavaInstanceSpotRegistry instanceSpots =
        new ZLinkJavaInstanceSpotRegistry();
    private volatile ZLinkJavaRawSpot entrySpot;
    private volatile ZLinkMeshApplicationReceiver applicationReceiver;

    ZLinkJavaRawSpotNode(ZLinkJavaRawMeshNode owner) {
        this.owner = owner;
    }

    @Override
    public String name() {
        return owner.name();
    }

    @Override
    public RoutingId routingId() {
        return owner.routingId();
    }

    @Override
    public void setRoutingId(RoutingId routingId) {
        owner.setRoutingId(routingId);
    }

    @Override
    public void setPublisherRoutingId(RoutingId routingId) {
    }

    @Override
    public void setSubscriberRoutingId(RoutingId routingId) {
    }

    @Override
    public void setRouterBind(String endpoint) {
        owner.setBind(endpoint);
    }

    @Override
    public void setPubBind(String endpoint) {
    }

    @Override
    public void connectPeer(String endpoint) {
        owner.connectPeer(endpoint);
    }

    @Override
    public void connectPeer(RoutingId peerRid, String endpoint) {
        owner.connectPeer(endpoint, peerRid);
    }

    @Override
    public void disconnectPeer(String endpoint) {
        owner.connectionIntentIds().stream()
            .filter(intent -> owner.peers().stream().anyMatch(
                peer -> peer.connectionIntentId() == intent
                    && peer.endpoint().equals(endpoint)))
            .findFirst()
            .ifPresent(owner::removePeerConnection);
    }

    @Override
    public void disconnectPeer(RoutingId peerRid) {
        owner.peers().stream()
            .filter(peer -> peer.routingId().equals(peerRid))
            .findFirst()
            .ifPresent(peer -> owner.removePeerConnection(
                peer.connectionIntentId()));
    }

    @Override
    public void setApplicationReceiver(ZLinkMeshApplicationReceiver receiver) {
        applicationReceiver = java.util.Objects.requireNonNull(receiver, "receiver");
        receiver.setLocalNodeReadyHandler(() -> { });
    }

    @Override
    public Optional<ZLinkSubmitStatus> submitLocalNodeSend(
        RoutingId targetNodeRid,
        byte[] metadata,
        List<Message> parts) {
        if (!routingId().equals(targetNodeRid)) {
            return Optional.empty();
        }
        ZLinkMeshApplicationReceiver current = applicationReceiver;
        if (current == null) {
            return Optional.of(ZLinkSubmitStatus.TARGET_NOT_FOUND);
        }
        return Optional.of(current.submitLocalNodeSend(
            routingId(), metadata, parts));
    }

    @Override
    public Optional<ZLinkSubmitStatus> classifyNodeSendTarget(
        RoutingId targetNodeRid) {
        if (routingId().equals(targetNodeRid)
            || owner.peers().stream().anyMatch(
                peer -> peer.routingId().equals(targetNodeRid))) {
            return Optional.empty();
        }
        return Optional.of(ZLinkSubmitStatus.TARGET_NOT_FOUND);
    }

    @Override
    public boolean sendToNode(
        RoutingId targetNodeRid,
        List<Message> parts,
        SendFlags flags) {
        return sendToNode(targetNodeRid, new byte[0], parts, flags);
    }

    @Override
    public boolean sendToNode(
        RoutingId targetNodeRid,
        byte[] metadata,
        List<Message> parts,
        SendFlags flags) {
        return owner.sendNode(
            targetNodeRid, metadata, parts, false, null);
    }

    @Override
    public boolean requestToNode(
        RoutingId targetNodeRid,
        List<Message> parts,
        ZLinkBackendRequestCallback callback,
        SendFlags flags,
        Duration timeout) {
        return requestToNode(
            targetNodeRid, new byte[0], parts, callback, flags, timeout);
    }

    @Override
    public boolean requestToNode(
        RoutingId targetNodeRid,
        byte[] metadata,
        List<Message> parts,
        ZLinkBackendRequestCallback callback,
        SendFlags flags,
        Duration timeout) {
        return owner.requestNode(
            targetNodeRid, metadata, parts, callback, timeout);
    }

    @Override
    public boolean sendToChannel(
        String channelName,
        List<Message> parts,
        SendFlags flags) {
        return sendToChannel(channelName, new byte[0], parts, flags);
    }

    @Override
    public boolean sendToChannel(
        String channelName,
        byte[] metadata,
        List<Message> parts,
        SendFlags flags) {
        return owner.sendChannel(channelName, metadata, parts);
    }

    @Override
    public boolean requestToChannel(
        String channelName,
        List<Message> parts,
        ZLinkBackendRequestCallback callback,
        SendFlags flags,
        Duration timeout) {
        return requestToChannel(
            channelName, new byte[0], parts, callback, flags, timeout);
    }

    @Override
    public boolean requestToChannel(
        String channelName,
        byte[] metadata,
        List<Message> parts,
        ZLinkBackendRequestCallback callback,
        SendFlags flags,
        Duration timeout) {
        return owner.requestChannel(
            channelName, metadata, parts, callback, timeout);
    }

    @Override
    public PublishDetail publishDetailed(
        String channelName,
        String topic,
        List<Message> parts,
        SendFlags flags) {
        return new PublishDetail(0, 0, 0, 0, 0, 0, 0);
    }

    @Override
    public PublishDetail publishDetailed(
        String channelName,
        String topic,
        byte[] metadata,
        List<Message> parts,
        SendFlags flags) {
        return publishDetailed(channelName, topic, parts, flags);
    }

    @Override
    public ZLinkBackendSpotRouteBridge createRouteBridge() {
        throw new UnsupportedOperationException(
            "raw MeshNode routes Spot records without a route bridge");
    }

    @Override
    public ZLinkBackendSpot createSpot() {
        return createSpot(RoutingId.from(UUID.randomUUID()));
    }

    @Override
    public ZLinkBackendSpot createSpot(RoutingId spotRid) {
        java.util.Objects.requireNonNull(spotRid, "spotRid");
        ZLinkJavaRawSpot created = new ZLinkJavaRawSpot(
            this, spotRid, nextGeneration.getAndIncrement());
        ZLinkJavaRawSpot existing = spots.putIfAbsent(spotRid, created);
        if (existing != null) {
            return existing;
        }
        return created;
    }

    @Override
    public ZLinkBackendSpot entrySpot() {
        ZLinkJavaRawSpot current = entrySpot;
        if (current != null) {
            return current;
        }
        synchronized (this) {
            if (entrySpot == null) {
                entrySpot = (ZLinkJavaRawSpot) createSpot(
                    RoutingId.from(UUID.randomUUID()));
            }
            return entrySpot;
        }
    }

    @Override
    public ZLinkBackendActorRef createActor(
        String actorId,
        Message createRequest) {
        if (actorId == null || actorId.isBlank()) {
            throw new IllegalArgumentException("actorId is required");
        }
        ZLinkBackendActorRef created = new ZLinkBackendActorRef(
            routingId(), actorId, 1);
        if (actors.putIfAbsent(actorId, created) != null) {
            throw new IllegalStateException("actor already exists: " + actorId);
        }
        actorSpots.put(actorId, entrySpot().routingId());
        actorMembershipEpochs.put(actorId, 1L);
        return created;
    }

    @Override
    public ZLinkBackendActorRef actorLookup(String actorId) {
        return actors.get(actorId);
    }

    @Override
    public CompletionStage<ZLinkBackendActorJoinResult> joinActor(
        ZLinkBackendActorRef actor,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        List<Message> parts,
        Duration timeout) {
        return joinActor(
            actor, targetNodeRid, targetSpotRid, 0, parts, timeout);
    }

    @Override
    public CompletionStage<ZLinkBackendActorJoinResult> joinActor(
        ZLinkBackendActorRef actor,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        long targetSpotGeneration,
        List<Message> parts,
        Duration timeout) {
        ZLinkJavaRawSpot target = localSpot(
            targetNodeRid, targetSpotRid, targetSpotGeneration);
        if (target == null || !actors.containsKey(actor.actorId())) {
            return CompletableFuture.completedFuture(
                new ZLinkBackendActorJoinResult(
                    systems.zlink.framework.runtime.backend
                        .ZLinkBackendRequestResult.NOT_FOUND,
                    1,
                    actor,
                    targetSpotRid,
                    actorMembershipEpochs.getOrDefault(actor.actorId(), 0L),
                    0,
                    List.of()));
        }
        ZLinkJavaRawSpot.PendingJoin pending =
            new ZLinkJavaRawSpot.PendingJoin();
        ZLinkBackendActorJoinRequest request =
            new ZLinkBackendActorJoinRequest(
                actor,
                actor,
                ZLinkJavaRawSpot.copy(parts),
                pending);
        target.enqueueJoin(request).whenComplete((ignored, failure) -> {
            if (failure != null) {
                pending.fail(failure);
            }
        });
        return pending.completion().thenApply(reply -> {
            long epoch = actorMembershipEpochs.getOrDefault(
                actor.actorId(), 1L);
            if (reply.resultCode() == 0) {
                epoch = epoch == Long.MAX_VALUE ? Long.MAX_VALUE : epoch + 1;
                actorSpots.put(actor.actorId(), targetSpotRid);
                actorMembershipEpochs.put(actor.actorId(), epoch);
            }
            return new ZLinkBackendActorJoinResult(
                systems.zlink.framework.runtime.backend
                    .ZLinkBackendRequestResult.OK,
                reply.resultCode(),
                actor,
                targetSpotRid,
                epoch,
                0,
                reply.parts());
        });
    }

    @Override
    public CompletionStage<ZLinkBackendActorJoinEntrySpotResult> joinActorEntrySpot(
        ZLinkBackendActorRef actor,
        RoutingId targetNodeRid,
        Message request,
        Duration timeout) {
        if (!routingId().equals(targetNodeRid)
            || !actors.containsKey(actor.actorId())) {
            return CompletableFuture.completedFuture(
                new ZLinkBackendActorJoinEntrySpotResult(
                    systems.zlink.framework.runtime.backend
                        .ZLinkBackendRequestResult.NOT_FOUND,
                    1,
                    actor,
                    targetNodeRid,
                    entrySpot().routingId(),
                    actorMembershipEpochs.getOrDefault(actor.actorId(), 0L),
                    0,
                    List.of()));
        }
        ZLinkJavaRawSpot target = (ZLinkJavaRawSpot) entrySpot();
        ZLinkJavaRawSpot.PendingJoin pending =
            new ZLinkJavaRawSpot.PendingJoin();
        ZLinkBackendActorJoinRequest join =
            new ZLinkBackendActorJoinRequest(
                actor,
                actor,
                List.of(Message.from(request.toByteArray())),
                pending);
        target.enqueueJoin(join).whenComplete((ignored, failure) -> {
            if (failure != null) {
                pending.fail(failure);
            }
        });
        return pending.completion().thenApply(reply -> {
            long epoch = actorMembershipEpochs.getOrDefault(
                actor.actorId(), 1L);
            if (reply.resultCode() == 0) {
                epoch = epoch == Long.MAX_VALUE ? Long.MAX_VALUE : epoch + 1;
                actorSpots.put(actor.actorId(), target.routingId());
                actorMembershipEpochs.put(actor.actorId(), epoch);
            }
            return new ZLinkBackendActorJoinEntrySpotResult(
                systems.zlink.framework.runtime.backend
                    .ZLinkBackendRequestResult.OK,
                reply.resultCode(),
                actor,
                targetNodeRid,
                target.routingId(),
                epoch,
                0,
                reply.parts());
        });
    }

    @Override
    public CompletionStage<List<Message>> leaveActor(
        ZLinkBackendActorRef actor,
        RoutingId currentSpotRid,
        Duration timeout) {
        ZLinkJavaRawSpot current = spots.get(currentSpotRid);
        if (current == null || !actors.containsKey(actor.actorId())) {
            return CompletableFuture.failedFuture(
                new IllegalStateException("actor membership is stale"));
        }
        long nextEpoch = actorMembershipEpochs.getOrDefault(
            actor.actorId(), 1L);
        nextEpoch = nextEpoch == Long.MAX_VALUE
            ? Long.MAX_VALUE
            : nextEpoch + 1;
        actorSpots.put(actor.actorId(), entrySpot().routingId());
        actorMembershipEpochs.put(actor.actorId(), nextEpoch);
        ZLinkBackendActorLifecycleEvent left =
            new ZLinkBackendActorLifecycleEvent(
                ZLinkBackendActorLifecycleEventKind.LEFT,
                new ZLinkBackendActorLifecycleInfo(
                    actor,
                    actor,
                    Optional.of(currentSpotRid),
                    Optional.of(entrySpot().routingId()),
                    nextEpoch,
                    0));
        return current.enqueueLifecycle(left).thenApply(ignored -> List.of());
    }

    @Override
    public CompletionStage<Void> destroyActor(
        ZLinkBackendActorRef actor,
        Duration timeout) {
        actors.remove(actor.actorId());
        actorSpots.remove(actor.actorId());
        actorMembershipEpochs.remove(actor.actorId());
        streamBindings.remove(actor.actorId());
        return CompletableFuture.completedFuture(null);
    }

    @Override
    public boolean sendActorBoundSession(
        ZLinkBackendActorRef actor,
        List<Message> parts,
        SendFlags flags) {
        StreamBinding binding = streamBindings.get(actor.actorId());
        return binding != null
            && binding.actor().equals(actor)
            && binding.stream().send(
                binding.sessionRid(), parts, flags);
    }

    @Override
    public void replyActorNoBind(
        ZLinkBackendActorRef actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        long requestId,
        int flags,
        List<Message> parts) {
        if (!isCurrentActor(actor)) {
            throw new IllegalStateException("actor is not local");
        }
        CompletableFuture<List<Message>> pending =
            actorRequests.remove(requestId);
        if (pending != null) {
            pending.complete(ZLinkJavaRawSpot.copy(parts));
            return;
        }
        Consumer<List<Message>> remote = actorRemoteReplies.remove(requestId);
        if (remote != null) {
            remote.accept(parts);
        }
    }

    @Override
    public boolean sendToActor(
        ZLinkBackendActorRef actor,
        List<Message> parts,
        SendFlags flags) {
        if (!routingId().equals(actor.nodeRid())) {
            return owner.sendActor(actor, parts);
        }
        return dispatchLocalActor(actor, parts, 0, 0);
    }

    @Override
    public CompletionStage<List<Message>> requestToActor(
        ZLinkBackendActorRef actor,
        List<Message> parts,
        SendFlags flags,
        Duration timeout) {
        if (!routingId().equals(actor.nodeRid())) {
            return owner.requestActor(actor, parts, timeout);
        }
        if (!isCurrentActor(actor)) {
            return CompletableFuture.failedFuture(
                new IllegalStateException("actor is not local"));
        }
        long requestId = nextActorRequestSequence.getAndIncrement();
        CompletableFuture<List<Message>> completion = new CompletableFuture<>();
        actorRequests.put(requestId, completion);
        if (!dispatchLocalActor(actor, parts, requestId, 1)) {
            actorRequests.remove(requestId);
            return CompletableFuture.failedFuture(
                new IllegalStateException("actor Spot is not local"));
        }
        if (timeout != null && !timeout.isNegative() && !timeout.isZero()) {
            CompletableFuture.delayedExecutor(
                timeout.toNanos(),
                java.util.concurrent.TimeUnit.NANOSECONDS).execute(() -> {
                    if (actorRequests.remove(requestId, completion)) {
                        completion.completeExceptionally(
                            new java.util.concurrent.TimeoutException(
                                "Actor request timed out"));
                    }
                });
        }
        return completion;
    }

    @Override
    public boolean forwardActorBoundSession(
        ZLinkBackendActorRef actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        List<Message> parts,
        SendFlags flags) {
        return dispatchLocalActor(
            actor,
            parts,
            0,
            0,
            sourceNodeRid,
            sourceSessionRid);
    }

    @Override
    public void bindRemoteActorBoundSession(
        ZLinkBackendActorRef actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid) {
        if (!isCurrentActor(actor)) {
            throw new IllegalStateException("actor is not local");
        }
    }

    @Override
    public void closeActorBoundSession(
        ZLinkBackendActorRef actor,
        Duration timeout) {
        if (!isCurrentActor(actor)) {
            throw new IllegalStateException("actor is not local");
        }
        streamBindings.remove(actor.actorId());
    }

    @Override
    public void close() {
        spots.values().forEach(ZLinkJavaRawSpot::close);
        spots.clear();
        actors.clear();
        actorSpots.clear();
        actorMembershipEpochs.clear();
        actorRequests.values().forEach(completion ->
            completion.completeExceptionally(
                new IllegalStateException("raw SpotNode is closed")));
        actorRequests.clear();
        actorRemoteReplies.clear();
        streamBindings.clear();
        instanceSpots.closeAll();
        owner.close();
    }

    void rekeySpot(
        ZLinkJavaRawSpot spot,
        RoutingId previous,
        RoutingId current) {
        if (previous.equals(current)) {
            return;
        }
        synchronized (this) {
            ZLinkJavaRawSpot conflict = spots.get(current);
            if (conflict != null && conflict != spot) {
                throw new IllegalStateException(
                    "Spot routing id is already registered: " + current);
            }
            spots.remove(previous, spot);
            spots.put(current, spot);
        }
    }

    void removeSpot(ZLinkJavaRawSpot spot) {
        spots.remove(spot.routingId(), spot);
    }

    boolean enqueueRemoteSpot(
        RoutingId sourceNodeRid,
        ZLinkServiceM6BWireCodec.SpotMessage header,
        byte[] metadata,
        List<Message> parts,
        Consumer<List<Message>> reply) {
        ZLinkJavaRawSpot target = localSpot(
            routingId(),
            header.target().spotRid(),
            header.target().spotGeneration());
        if (target == null) {
            return false;
        }
        target.enqueueRoute(new systems.zlink.framework.runtime.backend
            .ZLinkBackendReceived(
                systems.zlink.framework.runtime.backend
                    .ZLinkBackendRequestResult.OK,
                Optional.of(sourceNodeRid),
                Optional.of(header.sourceSpotRid()),
                Optional.ofNullable(header.correlation()),
                metadata,
                parts,
                header.request() ? reply : null,
                () -> { }));
        return true;
    }

    boolean enqueueRemoteActor(
        RoutingId sourceNodeRid,
        ZLinkServiceM6BWireCodec.ActorMessage header,
        List<Message> parts,
        Consumer<List<Message>> reply) {
        ZLinkBackendActorRef actor = header.target().actor();
        if (!isCurrentActor(actor)) {
            return false;
        }
        RoutingId targetSpotRid = actorSpots.get(actor.actorId());
        ZLinkJavaRawSpot target = spots.get(targetSpotRid);
        if (target == null) {
            return false;
        }
        long requestId = header.request()
            ? nextActorRequestSequence.getAndIncrement()
            : 0;
        if (header.request()) {
            actorRemoteReplies.put(requestId, reply);
            CompletableFuture.delayedExecutor(
                30,
                java.util.concurrent.TimeUnit.SECONDS).execute(
                    () -> actorRemoteReplies.remove(requestId, reply));
        }
        List<ZLinkBackendActorReceived> messages =
            new java.util.ArrayList<>(parts.size());
        for (int index = 0; index < parts.size(); index++) {
            messages.add(new ZLinkBackendActorReceived(
                actor,
                sourceNodeRid,
                null,
                Optional.ofNullable(header.correlation()),
                requestId,
                header.request() ? 1 : 0,
                parts.get(index),
                index + 1 < parts.size()));
        }
        target.enqueueActor(messages);
        return true;
    }

    void bindStreamSession(
        RoutingId sessionRid,
        ZLinkBackendActorRef actor,
        ZLinkJavaStreamSocket stream) {
        if (!isCurrentActor(actor)) {
            throw new IllegalStateException(
                "STREAM Actor binding is not local or is stale");
        }
        StreamBinding binding = new StreamBinding(
            sessionRid, actor, stream);
        StreamBinding current =
            streamBindings.putIfAbsent(actor.actorId(), binding);
        if (current != null && !current.equals(binding)) {
            throw new IllegalStateException(
                "Actor already has a different STREAM session binding");
        }
    }

    void unbindStreamSession(
        RoutingId sessionRid,
        ZLinkBackendActorRef actor,
        ZLinkJavaStreamSocket stream) {
        streamBindings.remove(
            actor.actorId(),
            new StreamBinding(sessionRid, actor, stream));
    }

    void registerInstanceSpotType(String stableType) {
        instanceSpots.register(stableType, this::createSpot);
    }

    CompletionStage<ZLinkJavaInstanceSpotRegistry.Activation>
        activateInstanceSpot(
            RoutingId spotRid,
            String stableType) {
        return instanceSpots.activate(spotRid, stableType);
    }

    boolean closeInstanceSpot(RoutingId spotRid, long generation) {
        return instanceSpots.close(spotRid, generation);
    }

    private boolean dispatchLocalActor(
        ZLinkBackendActorRef actor,
        List<Message> parts,
        long requestId,
        int flags) {
        return dispatchLocalActor(
            actor, parts, requestId, flags, routingId(), null);
    }

    private boolean dispatchLocalActor(
        ZLinkBackendActorRef actor,
        List<Message> parts,
        long requestId,
        int flags,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid) {
        if (!isCurrentActor(actor)) {
            return false;
        }
        RoutingId targetSpotRid = actorSpots.get(actor.actorId());
        ZLinkJavaRawSpot target = spots.get(targetSpotRid);
        if (target == null) {
            return false;
        }
        List<Message> copied = ZLinkJavaRawSpot.copy(parts);
        List<ZLinkBackendActorReceived> messages =
            new java.util.ArrayList<>(copied.size());
        for (int index = 0; index < copied.size(); index++) {
            messages.add(new ZLinkBackendActorReceived(
                actor,
                sourceNodeRid,
                sourceSessionRid,
                Optional.empty(),
                requestId,
                flags,
                copied.get(index),
                index + 1 < copied.size()));
        }
        target.enqueueActor(messages);
        return true;
    }

    private boolean isCurrentActor(ZLinkBackendActorRef actor) {
        return actor != null && actor.equals(actors.get(actor.actorId()));
    }

    private record StreamBinding(
        RoutingId sessionRid,
        ZLinkBackendActorRef actor,
        ZLinkJavaStreamSocket stream) {
    }

    boolean publish(
        ZLinkJavaRawSpot source,
        String channelName,
        String topic,
        byte[] metadata,
        List<Message> parts) {
        AtomicBoolean delivered = new AtomicBoolean();
        for (ZLinkJavaRawSpot target : spots.values()) {
            if (!target.accepts(topic)) {
                continue;
            }
            delivered.set(true);
            target.enqueueTopic(new systems.zlink.framework.runtime.backend
                .ZLinkBackendTopicMessage(
                    Optional.of(routingId()),
                    channelName,
                    topic,
                    metadata == null ? new byte[0] : metadata.clone(),
                    ZLinkJavaRawSpot.copy(parts)));
        }
        return delivered.get();
    }

    boolean sendToSpot(
        ZLinkJavaRawSpot source,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        long targetGeneration,
        byte[] metadata,
        List<Message> parts) {
        if (!routingId().equals(targetNodeRid)) {
            return owner.sendSpot(
                source.routingId(),
                targetNodeRid,
                targetSpotRid,
                targetGeneration,
                metadata,
                parts);
        }
        ZLinkJavaRawSpot target = localSpot(
            targetNodeRid, targetSpotRid, targetGeneration);
        if (target == null) {
            return false;
        }
        target.enqueueRoute(new systems.zlink.framework.runtime.backend
            .ZLinkBackendReceived(
                systems.zlink.framework.runtime.backend
                    .ZLinkBackendRequestResult.OK,
                Optional.of(routingId()),
                Optional.of(source.routingId()),
                Optional.empty(),
                metadata,
                ZLinkJavaRawSpot.copy(parts),
                null,
                () -> { }));
        return true;
    }

    boolean requestToSpot(
        ZLinkJavaRawSpot source,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        long targetGeneration,
        byte[] metadata,
        List<Message> parts,
        ZLinkBackendRequestCallback callback,
        Duration timeout) {
        if (!routingId().equals(targetNodeRid)) {
            return owner.requestSpot(
                source.routingId(),
                targetNodeRid,
                targetSpotRid,
                targetGeneration,
                metadata,
                parts,
                callback,
                timeout);
        }
        ZLinkJavaRawSpot target = localSpot(
            targetNodeRid, targetSpotRid, targetGeneration);
        if (target == null) {
            return false;
        }
        long sequence = nextRequestSequence.getAndIncrement();
        AtomicBoolean terminal = new AtomicBoolean();
        CompletionStage<Void> enqueued = target.enqueueRoute(
            new systems.zlink.framework.runtime.backend
            .ZLinkBackendReceived(
                systems.zlink.framework.runtime.backend
                    .ZLinkBackendRequestResult.OK,
                Optional.of(routingId()),
                Optional.of(source.routingId()),
                Optional.of(sequence),
                metadata,
                ZLinkJavaRawSpot.copy(parts),
                reply -> {
                    if (!terminal.compareAndSet(false, true)) {
                        return;
                    }
                    callback.handle(new systems.zlink.framework.runtime.backend
                        .ZLinkBackendReceived(
                            systems.zlink.framework.runtime.backend
                                .ZLinkBackendRequestResult.OK,
                            Optional.of(targetNodeRid),
                            Optional.of(targetSpotRid),
                            Optional.of(sequence),
                            ZLinkJavaRawSpot.copy(reply)));
                },
                () -> { }));
        enqueued.whenComplete((ignored, failure) -> {
            if (failure != null && terminal.compareAndSet(false, true)) {
                callback.handle(new systems.zlink.framework.runtime.backend
                    .ZLinkBackendReceived(
                        systems.zlink.framework.runtime.backend
                            .ZLinkBackendRequestResult.INTERNAL_ERROR,
                        Optional.of(targetNodeRid),
                        Optional.of(targetSpotRid),
                        Optional.of(sequence),
                        List.of()));
            }
        });
        if (timeout != null && !timeout.isNegative() && !timeout.isZero()) {
            CompletableFuture.delayedExecutor(
                timeout.toNanos(),
                java.util.concurrent.TimeUnit.NANOSECONDS).execute(() -> {
                    if (terminal.compareAndSet(false, true)) {
                        callback.handle(new systems.zlink.framework.runtime.backend
                            .ZLinkBackendReceived(
                                systems.zlink.framework.runtime.backend
                                    .ZLinkBackendRequestResult.TIMED_OUT,
                                Optional.of(targetNodeRid),
                                Optional.of(targetSpotRid),
                                Optional.of(sequence),
                                List.of()));
                    }
                });
        }
        return true;
    }

    private ZLinkJavaRawSpot localSpot(
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        long targetGeneration) {
        if (!routingId().equals(targetNodeRid)) {
            return null;
        }
        ZLinkJavaRawSpot target = spots.get(targetSpotRid);
        if (target == null) {
            return null;
        }
        return targetGeneration == 0
                || target.lifecycleGeneration() == targetGeneration
            ? target
            : null;
    }
}
