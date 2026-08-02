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
import systems.zlink.contracts.sockets.SendFlags;

import systems.zlink.framework.runtime.internal.backend.ZLinkBackendActorJoinEntrySpotResult;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendActorJoinRequest;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendActorJoinResult;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendActorLifecycleEvent;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendActorLifecycleEventKind;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendActorLifecycleInfo;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendActorReceived;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendActorRef;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendRequestCallback;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendSpot;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendSpotRouteBridge;
import systems.zlink.framework.runtime.internal.backend.ZLinkInternalMeshNode;
import systems.zlink.framework.runtime.internal.backend.ZLinkInternalSpotNode;
import systems.zlink.framework.runtime.internal.backend.ZLinkMeshApplicationReceiver;
import systems.zlink.framework.runtime.internal.dispatch.ZLinkInboundDispatchBudget;
import systems.zlink.framework.runtime.internal.service.ZLinkServiceM6BWireCodec;
import systems.zlink.framework.runtime.channels.ZLinkChannelContentTypeFrame;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeader;

/**
 * Framework-owned service runtime projected over the raw MeshNode transport.
 * Stateful Spot and Actor identity remains inside the Framework runtime.
 */
final class ZLinkJavaRawSpotNode
    implements ZLinkInternalSpotNode, ZLinkJavaAdmissionBacked {
    private final ZLinkJavaRawMeshNode owner;
    private final Map<String, ZLinkJavaRawSpot> spots =
        new ConcurrentHashMap<>();
    private final Map<String, ZLinkBackendActorRef> actors =
        new ConcurrentHashMap<>();
    private final Map<String, String> actorSpots =
        new ConcurrentHashMap<>();
    private final Map<String, Long> actorMembershipEpochs =
        new ConcurrentHashMap<>();
    private final Map<SpotAuthorityKey, Long> spotAuthorities =
        new ConcurrentHashMap<>();
    private final Map<ActorAuthorityKey, Long> actorAuthorities =
        new ConcurrentHashMap<>();
    private final AtomicLong nextGeneration = new AtomicLong(1);
    private final AtomicLong nextRequestSequence = new AtomicLong(1);
    private final AtomicLong nextActorRequestSequence = new AtomicLong(1);
    private final AtomicLong nextStreamBindingGeneration =
        new AtomicLong();
    private final Map<Long, CompletableFuture<List<Message>>> actorRequests =
        new ConcurrentHashMap<>();
    private final Map<Long, Consumer<List<Message>>> actorRemoteReplies =
        new ConcurrentHashMap<>();
    private final Map<String, StreamBinding> streamBindings =
        new ConcurrentHashMap<>();
    private final Map<String, Long> streamBindingSequences =
        new ConcurrentHashMap<>();
    private final Map<String, RemoteStreamBinding> remoteStreamBindings =
        new ConcurrentHashMap<>();
    private final Map<String, Long> remoteStreamSequences =
        new ConcurrentHashMap<>();
    private final ZLinkJavaInstanceSpotRegistry instanceSpots =
        new ZLinkJavaInstanceSpotRegistry();
    private final Map<String, InstanceAuthority> instanceAuthorities =
        new ConcurrentHashMap<>();
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
    public Optional<Integer> submitLocalNodeSend(
        RoutingId targetNodeRid,
        byte[] metadata,
        List<Message> parts) {
        if (!routingId().equals(targetNodeRid)) {
            return Optional.empty();
        }
        ZLinkMeshApplicationReceiver current = applicationReceiver;
        if (current == null) {
            return Optional.of(ZLinkOneWayCalls.TARGET_NOT_FOUND);
        }
        return Optional.of(current.submitLocalNodeSend(
            routingId(), metadata, parts));
    }

    @Override
    public Optional<Integer> classifyNodeSendTarget(
        RoutingId targetNodeRid) {
        if (owner.isObjectClientNodeDirectTarget(targetNodeRid)) {
            return Optional.of(ZLinkOneWayCalls.TARGET_NOT_FOUND);
        }
        if (routingId().equals(targetNodeRid)
            || owner.peers().stream().anyMatch(
                peer -> peer.routingId().equals(targetNodeRid))) {
            return Optional.empty();
        }
        return Optional.of(ZLinkOneWayCalls.TARGET_NOT_FOUND);
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
    public void publish(
        String channelName,
        String topic,
        List<Message> parts,
        SendFlags flags) {
        owner.publishLogicalMulticast(
            null, channelName, topic, new byte[0], parts);
    }

    @Override
    public void publish(
        String channelName,
        String topic,
        byte[] metadata,
        List<Message> parts,
        SendFlags flags) {
        owner.publishLogicalMulticast(
            null, channelName, topic, metadata, parts);
    }

    @Override
    public ZLinkBackendSpotRouteBridge createRouteBridge() {
        throw new UnsupportedOperationException(
            "raw MeshNode routes Spot records without a route bridge");
    }

    @Override
    public ZLinkBackendSpot createSpot() {
        return createSpot(UUID.randomUUID().toString());
    }

    @Override
    public ZLinkBackendSpot createSpot(String spotId) {
        java.util.Objects.requireNonNull(spotId, "spotId");
        return createSpot(spotId, nextGeneration.getAndIncrement());
    }

    @Override
    public ZLinkBackendSpot createSpot(
        String spotId,
        long lifecycleGeneration) {
        java.util.Objects.requireNonNull(spotId, "spotId");
        if (lifecycleGeneration <= 0) {
            throw new IllegalArgumentException(
                "Spot lifecycle generation must be positive");
        }
        ZLinkJavaRawSpot created = new ZLinkJavaRawSpot(
            this, spotId, lifecycleGeneration);
        ZLinkJavaRawSpot existing = spots.putIfAbsent(spotId, created);
        if (existing != null) {
            return existing;
        }
        rememberSpotAuthority(
            routingId(),
            spotId,
            lifecycleGeneration,
            lifecycleGeneration);
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
                    routingId() + "-entry-" + UUID.randomUUID());
            }
            return entrySpot;
        }
    }

    @Override
    public ZLinkBackendActorRef createActor(
        String actorId,
        Message createRequest) {
        return createActor(actorId, 1, createRequest);
    }

    @Override
    public ZLinkBackendActorRef createActor(
        String actorId,
        long objectGeneration,
        Message createRequest) {
        if (actorId == null || actorId.isBlank()) {
            throw new IllegalArgumentException("actorId is required");
        }
        if (objectGeneration <= 0) {
            throw new IllegalArgumentException(
                "Actor object generation must be positive");
        }
        ZLinkBackendActorRef created = new ZLinkBackendActorRef(
            routingId(), actorId, objectGeneration);
        if (actors.putIfAbsent(actorId, created) != null) {
            throw new IllegalStateException("actor already exists: " + actorId);
        }
        actorSpots.put(actorId, entrySpot().spotId());
        actorMembershipEpochs.put(actorId, 1L);
        rememberActorAuthority(created, created.generation());
        return created;
    }

    @Override
    public ZLinkBackendActorRef actorLookup(String actorId) {
        return actors.get(actorId);
    }

    @Override
    public boolean hasPendingActorRequests() {
        return !actorRequests.isEmpty();
    }

    @Override
    public void rememberActorAuthority(
        ZLinkBackendActorRef actor,
        long authorityOwnerGeneration) {
        if (actor == null || authorityOwnerGeneration <= 0) {
            throw new IllegalArgumentException(
                "Actor authority generation must be positive");
        }
        actorAuthorities.put(
            new ActorAuthorityKey(
                actor.nodeRid(), actor.actorId(), actor.generation()),
            authorityOwnerGeneration);
    }

    @Override
    public void registerTransferredActor(
        ZLinkBackendActorRef actor,
        String spotId,
        long membershipEpoch) {
        java.util.Objects.requireNonNull(actor, "actor");
        if (!routingId().equals(actor.nodeRid())
            || actor.generation() <= 0
            || spotId == null
            || spotId.isBlank()
            || membershipEpoch <= 0) {
            throw new IllegalArgumentException(
                "transferred Actor route requires the local node, exact "
                    + "generation, SpotId, and positive membership epoch");
        }
        ZLinkBackendActorRef current = actors.putIfAbsent(
            actor.actorId(),
            actor);
        if (current != null && !current.equals(actor)) {
            throw new IllegalStateException(
                "actor generation already exists on target: "
                    + actor.actorId());
        }
        actorSpots.put(actor.actorId(), spotId);
        actorMembershipEpochs.put(actor.actorId(), membershipEpoch);
    }

    @Override
    public CompletionStage<ZLinkBackendActorJoinResult> joinActor(
        ZLinkBackendActorRef actor,
        RoutingId targetNodeRid,
        String targetSpotId,
        List<Message> parts,
        Duration timeout) {
        return joinActor(
            actor, targetNodeRid, targetSpotId, 0, parts, timeout);
    }

    @Override
    public CompletionStage<ZLinkBackendActorJoinResult> joinActor(
        ZLinkBackendActorRef actor,
        RoutingId targetNodeRid,
        String targetSpotId,
        long targetSpotGeneration,
        List<Message> parts,
        Duration timeout) {
        ZLinkJavaRawSpot target = localSpot(
            targetNodeRid, targetSpotId, targetSpotGeneration);
        if (target == null || !actors.containsKey(actor.actorId())) {
            return CompletableFuture.completedFuture(
                new ZLinkBackendActorJoinResult(
                    systems.zlink.framework.runtime.internal.backend
                        .ZLinkBackendRequestResult.NOT_FOUND,
                    1,
                    actor,
                    targetSpotId,
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
                actorSpots.put(actor.actorId(), targetSpotId);
                actorMembershipEpochs.put(actor.actorId(), epoch);
            }
            return new ZLinkBackendActorJoinResult(
                systems.zlink.framework.runtime.internal.backend
                    .ZLinkBackendRequestResult.OK,
                reply.resultCode(),
                actor,
                targetSpotId,
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
                    systems.zlink.framework.runtime.internal.backend
                        .ZLinkBackendRequestResult.NOT_FOUND,
                    1,
                    actor,
                    targetNodeRid,
                    entrySpot().spotId(),
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
                actorSpots.put(actor.actorId(), target.spotId());
                actorMembershipEpochs.put(actor.actorId(), epoch);
            }
            return new ZLinkBackendActorJoinEntrySpotResult(
                systems.zlink.framework.runtime.internal.backend
                    .ZLinkBackendRequestResult.OK,
                reply.resultCode(),
                actor,
                targetNodeRid,
                target.spotId(),
                epoch,
                0,
                reply.parts());
        });
    }

    @Override
    public CompletionStage<List<Message>> leaveActor(
        ZLinkBackendActorRef actor,
        String currentSpotId,
        Duration timeout) {
        ZLinkJavaRawSpot current = spots.get(currentSpotId);
        if (current == null || !actors.containsKey(actor.actorId())) {
            return CompletableFuture.failedFuture(
                new IllegalStateException("actor membership is stale"));
        }
        long nextEpoch = actorMembershipEpochs.getOrDefault(
            actor.actorId(), 1L);
        nextEpoch = nextEpoch == Long.MAX_VALUE
            ? Long.MAX_VALUE
            : nextEpoch + 1;
        actorSpots.put(actor.actorId(), entrySpot().spotId());
        actorMembershipEpochs.put(actor.actorId(), nextEpoch);
        ZLinkBackendActorLifecycleEvent left =
            new ZLinkBackendActorLifecycleEvent(
                ZLinkBackendActorLifecycleEventKind.LEFT,
                new ZLinkBackendActorLifecycleInfo(
                    actor,
                    actor,
                    Optional.of(currentSpotId),
                    Optional.of(entrySpot().spotId()),
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
        streamBindingSequences.remove(actor.actorId());
        remoteStreamBindings.remove(actor.actorId());
        remoteStreamSequences.remove(actor.actorId());
        actorAuthorities.remove(new ActorAuthorityKey(
            actor.nodeRid(), actor.actorId(), actor.generation()));
        return CompletableFuture.completedFuture(null);
    }

    @Override
    public boolean sendActorBoundSession(
        ZLinkBackendActorRef actor,
        List<Message> parts,
        SendFlags flags) {
        StreamBinding binding = streamBindings.get(actor.actorId());
        if (binding != null && binding.actor().equals(actor)) {
            return binding.stream().sendBoundSessionPush(
                binding.sessionRid(), parts, flags);
        }
        RemoteStreamBinding remote =
            remoteStreamBindings.get(actor.actorId());
        return remote != null
            && remote.actor().equals(actor)
            && owner.sendBoundSession(remote, parts);
    }

    @Override
    public void replyActorNoBind(
        ZLinkBackendActorRef actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        long requestId,
        int flags,
        List<Message> parts) {
        CompletableFuture<List<Message>> pending =
            actorRequests.remove(requestId);
        if (pending != null) {
            pending.complete(ZLinkJavaRawSpot.copy(parts));
            return;
        }
        Consumer<List<Message>> remote = actorRemoteReplies.remove(requestId);
        if (remote != null) {
            remote.accept(parts);
            return;
        }
        if (!isCurrentActor(actor)) {
            throw new IllegalStateException("actor is not local");
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
            sourceSessionRid,
            0,
            0);
    }

    @Override
    public byte[] encodeLocalSessionActorAccepted(
        ZLinkBackendActorRef actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        long sourceBindingGeneration,
        long sourceSessionSequence,
        long requestSequence,
        String packetName,
        java.util.Map<String, String> metadata,
        byte[] payload) {
        return owner.encodeLocalActorAccepted(
            actor,
            sourceNodeRid,
            sourceSessionRid,
            sourceBindingGeneration,
            sourceSessionSequence,
            requestSequence,
            packetName,
            metadata,
            payload);
    }

    synchronized boolean forwardBoundStreamSession(
        ZLinkBackendActorRef actor,
        RoutingId sourceSessionRid,
        long sourceBindingGeneration,
        long sourceSessionSequence,
        ZLinkJavaStreamSocket stream,
        List<Message> parts) {
        return forwardBoundStreamSession(
            actor,
            sourceSessionRid,
            sourceBindingGeneration,
            sourceSessionSequence,
            stream,
            null,
            parts);
    }

    synchronized boolean forwardBoundStreamSession(
        ZLinkBackendActorRef actor,
        RoutingId sourceSessionRid,
        long sourceBindingGeneration,
        long sourceSessionSequence,
        ZLinkJavaStreamSocket stream,
        ZLinkStreamHeader streamHeader,
        List<Message> parts) {
        StreamBinding binding = streamBindings.get(actor.actorId());
        if (binding == null
            || !binding.actor().equals(actor)
            || !binding.sessionRid().equals(sourceSessionRid)
            || binding.bindingGeneration() != sourceBindingGeneration
            || binding.stream() != stream
            || !acceptStreamBindingSequence(
                actor.actorId(), sourceSessionSequence)) {
            return false;
        }
        if (!routingId().equals(actor.nodeRid())) {
            if (streamHeader != null
                && streamHeader.requestSequence().isPresent()) {
                return owner.requestBoundActor(
                    actor,
                    sourceSessionRid,
                    sourceBindingGeneration,
                    sourceSessionSequence,
                    streamHeader.requestSequence().orElseThrow(),
                    parts,
                    reply -> replyBoundStreamSession(
                        stream,
                        sourceSessionRid,
                        reply));
            }
            return owner.sendBoundActor(
                actor,
                sourceSessionRid,
                sourceBindingGeneration,
                sourceSessionSequence,
                parts);
        }
        if (streamHeader != null
            && streamHeader.requestSequence().isPresent()) {
            requestToActor(actor, parts, SendFlags.DONT_WAIT,
                    Duration.ofSeconds(30))
                .whenComplete((reply, failure) -> {
                    if (failure == null) {
                        replyBoundStreamSession(
                            stream,
                            sourceSessionRid,
                            reply);
                    }
                });
            return true;
        }
        return dispatchLocalActor(
            actor,
            parts,
            0,
            0,
            routingId(),
            sourceSessionRid,
            sourceBindingGeneration,
            sourceSessionSequence);
    }

    private static void replyBoundStreamSession(
        ZLinkJavaStreamSocket stream,
        RoutingId sessionRid,
        List<Message> reply) {
        try {
            stream.sendBoundSessionPush(
                sessionRid,
                reply,
                SendFlags.DONT_WAIT);
        } finally {
            reply.forEach(Message::close);
        }
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
    public java.util.Optional<ZLinkInternalSpotNode.BoundSessionRoute>
        boundSessionRoute(ZLinkBackendActorRef actor) {
        RemoteStreamBinding remote = remoteStreamBindings.get(actor.actorId());
        if (remote == null || !remote.actor().equals(actor)) {
            return java.util.Optional.empty();
        }
        return java.util.Optional.of(
            new ZLinkInternalSpotNode.BoundSessionRoute(
                remote.sessionOwnerNodeRid(),
                remote.sessionOwnerNodeGeneration(),
                remote.sessionRid(),
                remote.bindingGeneration(),
                remoteStreamSequences.getOrDefault(actor.actorId(), 0L)));
    }

    @Override
    public void closeActorBoundSession(
        ZLinkBackendActorRef actor,
        Duration timeout) {
        if (!isCurrentActor(actor)) {
            throw new IllegalStateException("actor is not local");
        }
        streamBindings.remove(actor.actorId());
        streamBindingSequences.remove(actor.actorId());
        remoteStreamBindings.remove(actor.actorId());
        remoteStreamSequences.remove(actor.actorId());
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
        streamBindingSequences.clear();
        remoteStreamBindings.clear();
        remoteStreamSequences.clear();
        spotAuthorities.clear();
        actorAuthorities.clear();
        instanceAuthorities.clear();
        instanceSpots.closeAll();
        owner.close();
    }

    void rekeySpot(
        ZLinkJavaRawSpot spot,
        String previous,
        String current) {
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
        spots.remove(spot.spotId(), spot);
        spotAuthorities.remove(new SpotAuthorityKey(
            routingId(),
            spot.spotId(),
            spot.lifecycleGeneration()));
    }

    void rememberSpotAuthority(
        RoutingId targetNodeRid,
        String spotId,
        long objectGeneration,
        long authorityOwnerGeneration) {
        if (targetNodeRid == null
            || spotId == null
            || objectGeneration <= 0
            || authorityOwnerGeneration <= 0) {
            throw new IllegalArgumentException(
                "Spot authority generations must be positive");
        }
        spotAuthorities.put(
            new SpotAuthorityKey(
                targetNodeRid, spotId, objectGeneration),
            authorityOwnerGeneration);
    }

    long spotAuthorityOwnerGeneration(
        RoutingId targetNodeRid,
        String spotId,
        long objectGeneration) {
        return spotAuthorities.getOrDefault(
            new SpotAuthorityKey(
                targetNodeRid, spotId, objectGeneration),
            0L);
    }

    void forgetSpotAuthority(
        RoutingId targetNodeRid,
        String spotId,
        long objectGeneration,
        long authorityOwnerGeneration) {
        spotAuthorities.remove(
            new SpotAuthorityKey(
                targetNodeRid, spotId, objectGeneration),
            authorityOwnerGeneration);
    }

    long actorAuthorityOwnerGeneration(ZLinkBackendActorRef actor) {
        return actorAuthorities.getOrDefault(
            new ActorAuthorityKey(
                actor.nodeRid(), actor.actorId(), actor.generation()),
            0L);
    }

    boolean enqueueRemoteSpot(
        RoutingId sourceNodeRid,
        ZLinkServiceM6BWireCodec.SpotMessage header,
        byte[] metadata,
        List<Message> parts,
        Consumer<List<Message>> reply) {
        return enqueueRemoteSpot(
            new ZLinkInternalMeshNode.PeerAuthorityFence(
                sourceNodeRid, 1, "test-owner", 1),
            header,
            metadata,
            new byte[0],
            parts,
            reply);
    }

    boolean enqueueRemoteSpot(
        ZLinkInternalMeshNode.PeerAuthorityFence source,
        ZLinkServiceM6BWireCodec.SpotMessage header,
        byte[] metadata,
        byte[] acceptedJournalRecord,
        List<Message> parts,
        Consumer<List<Message>> reply) {
        return enqueueRemoteSpot(
            source,
            header,
            metadata,
            acceptedJournalRecord,
            parts,
            null,
            reply);
    }

    boolean enqueueRemoteSpot(
        ZLinkInternalMeshNode.PeerAuthorityFence source,
        ZLinkServiceM6BWireCodec.SpotMessage header,
        byte[] metadata,
        byte[] acceptedJournalRecord,
        List<Message> parts,
        ZLinkInboundDispatchBudget.Lease inboundDispatchLease,
        Consumer<List<Message>> reply) {
        return enqueueRemoteSpot(
            source,
            header,
            metadata,
            acceptedJournalRecord,
            parts,
            null,
            inboundDispatchLease,
            reply);
    }

    boolean enqueueRemoteSpot(
        ZLinkInternalMeshNode.PeerAuthorityFence source,
        ZLinkServiceM6BWireCodec.SpotMessage header,
        byte[] metadata,
        byte[] acceptedJournalRecord,
        List<Message> parts,
        String contentType,
        ZLinkInboundDispatchBudget.Lease inboundDispatchLease,
        Consumer<List<Message>> reply) {
        ZLinkJavaRawSpot target = localSpot(
            routingId(),
            header.target().spotId(),
            header.target().spotGeneration());
        if (target == null
            || spotAuthorityOwnerGeneration(
                routingId(),
                header.target().spotId(),
                header.target().spotGeneration())
                != header.target().authorityOwnerGeneration()) {
            if (inboundDispatchLease != null) {
                inboundDispatchLease.close();
            }
            return false;
        }
        target.enqueueRoute(new systems.zlink.framework.runtime.internal.backend
            .ZLinkBackendReceived(
                systems.zlink.framework.runtime.internal.backend
                    .ZLinkBackendRequestResult.OK,
                Optional.of(source.sourceNodeRid()),
                Optional.of(header.sourceSpotId()),
                Optional.ofNullable(header.correlation()),
                metadata,
                acceptedJournalRecord,
                parts,
                header.request() ? reply : null,
                () -> { },
                contentType,
                inboundDispatchLease));
        return true;
    }

    boolean enqueueRemoteActor(
        RoutingId sourceNodeRid,
        ZLinkServiceM6BWireCodec.ActorMessage header,
        List<Message> parts,
        Consumer<List<Message>> reply) {
        return enqueueRemoteActor(
            sourceNodeRid,
            0,
            header,
            new byte[0],
            parts,
            reply);
    }

    boolean enqueueRemoteActor(
        ZLinkInternalMeshNode.PeerAuthorityFence source,
        ZLinkServiceM6BWireCodec.ActorMessage header,
        List<Message> parts,
        Consumer<List<Message>> reply) {
        return enqueueRemoteActor(
            source.sourceNodeRid(),
            source.sourceNodeGeneration(),
            header,
            new byte[0],
            parts,
            reply);
    }

    boolean enqueueRemoteActor(
        ZLinkInternalMeshNode.PeerAuthorityFence source,
        ZLinkServiceM6BWireCodec.ActorMessage header,
        byte[] acceptedJournalRecord,
        List<Message> parts,
        Consumer<List<Message>> reply) {
        return enqueueRemoteActor(
            source.sourceNodeRid(),
            source.sourceNodeGeneration(),
            header,
            acceptedJournalRecord,
            parts,
            reply);
    }

    boolean enqueueRemoteActor(
        RoutingId sourceNodeRid,
        long sourceNodeGeneration,
        ZLinkServiceM6BWireCodec.ActorMessage header,
        List<Message> parts,
        Consumer<List<Message>> reply) {
        return enqueueRemoteActor(
            sourceNodeRid,
            sourceNodeGeneration,
            header,
            new byte[0],
            parts,
            reply);
    }

    boolean enqueueRemoteActor(
        RoutingId sourceNodeRid,
        long sourceNodeGeneration,
        ZLinkServiceM6BWireCodec.ActorMessage header,
        byte[] acceptedJournalRecord,
        List<Message> parts,
        Consumer<List<Message>> reply) {
        return enqueueRemoteActor(
            sourceNodeRid,
            sourceNodeGeneration,
            header,
            acceptedJournalRecord,
            parts,
            null,
            reply);
    }

    boolean enqueueRemoteActor(
        RoutingId sourceNodeRid,
        long sourceNodeGeneration,
        ZLinkServiceM6BWireCodec.ActorMessage header,
        byte[] acceptedJournalRecord,
        List<Message> parts,
        ZLinkInboundDispatchBudget.Lease inboundDispatchLease,
        Consumer<List<Message>> reply) {
        return enqueueRemoteActor(
            sourceNodeRid,
            sourceNodeGeneration,
            header,
            acceptedJournalRecord,
            parts,
            null,
            inboundDispatchLease,
            reply);
    }

    boolean enqueueRemoteActor(
        RoutingId sourceNodeRid,
        long sourceNodeGeneration,
        ZLinkServiceM6BWireCodec.ActorMessage header,
        byte[] acceptedJournalRecord,
        List<Message> parts,
        String contentType,
        ZLinkInboundDispatchBudget.Lease inboundDispatchLease,
        Consumer<List<Message>> reply) {
        ZLinkBackendActorRef actor = header.target().actor();
        if (!isCurrentActor(actor)
            || actorAuthorityOwnerGeneration(actor)
                != header.target().authorityOwnerGeneration()) {
            if (inboundDispatchLease != null) {
                inboundDispatchLease.close();
            }
            return false;
        }
        if (header.boundSession() != null) {
            RemoteStreamBinding binding =
                remoteStreamBindings.get(actor.actorId());
            if (binding == null
                || !binding.actor().equals(actor)
                || !binding.sessionOwnerNodeRid().equals(sourceNodeRid)
                || binding.sessionOwnerNodeGeneration()
                    != sourceNodeGeneration
                || !binding.sessionRid().equals(
                    header.boundSession().sourceSessionRid())
                || binding.bindingGeneration()
                    != header.boundSession().sourceBindingGeneration()
                || !acceptRemoteStreamSequence(
                    actor.actorId(),
                    header.boundSession().sourceSessionSequence())) {
                if (inboundDispatchLease != null) {
                    inboundDispatchLease.close();
                }
                return false;
            }
        }
        String targetSpotId = actorSpots.get(actor.actorId());
        ZLinkJavaRawSpot target = spots.get(targetSpotId);
        if (target == null) {
            if (inboundDispatchLease != null) {
                inboundDispatchLease.close();
            }
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
                header.boundSession() == null
                    ? null
                    : header.boundSession().sourceSessionRid(),
                Optional.ofNullable(header.correlation()),
                requestId,
                header.request() ? 1 : 0,
                parts.get(index),
                index + 1 < parts.size(),
                index == 0
                    ? acceptedJournalRecord
                    : new byte[0],
                contentType,
                index == (parts.size() > 1 ? 1 : 0)
                    ? inboundDispatchLease
                    : null));
        }
        target.enqueueActor(messages);
        return true;
    }

    private boolean acceptRemoteStreamSequence(
        String actorId,
        long sequence) {
        AtomicBoolean accepted = new AtomicBoolean();
        remoteStreamSequences.computeIfPresent(actorId, (ignored, current) -> {
            if (sequence > current) {
                accepted.set(true);
                return sequence;
            }
            return current;
        });
        return accepted.get();
    }

    synchronized long allocateStreamBindingGeneration() {
        long current = nextStreamBindingGeneration.get();
        if (current == 0) {
            current = owner.bindingGenerationSeed();
        }
        if (current <= 0 || current == Long.MAX_VALUE) {
            throw new IllegalStateException(
                "STREAM binding generation is exhausted");
        }
        nextStreamBindingGeneration.set(current + 1);
        return current;
    }

    CompletionStage<Void> bindStreamSession(
        RoutingId sessionRid,
        ZLinkBackendActorRef actor,
        long bindingGeneration,
        ZLinkJavaStreamSocket stream,
        Duration timeout) {
        if (bindingGeneration <= 0) {
            return CompletableFuture.failedFuture(
                new IllegalArgumentException(
                    "binding generation must be positive"));
        }
        long authorityOwnerGeneration =
            actorAuthorityOwnerGeneration(actor);
        StreamBinding binding = new StreamBinding(
            sessionRid,
            actor,
            bindingGeneration,
            authorityOwnerGeneration,
            stream);
        if (isCurrentActor(actor)) {
            if (!installStreamBinding(binding)) {
                return CompletableFuture.failedFuture(
                    new IllegalStateException(
                        "Actor has a newer STREAM session binding"));
            }
            remoteStreamBindings.remove(actor.actorId());
            return CompletableFuture.completedFuture(null);
        }
        return owner.bindRemoteStreamSession(
            sessionRid,
            actor,
            authorityOwnerGeneration,
            bindingGeneration,
            true,
            timeout)
            .thenRun(() -> {
                if (!installStreamBinding(binding)) {
                    throw new IllegalStateException(
                        "Actor has a newer STREAM session binding");
                }
            });
    }

    CompletionStage<Void> unbindStreamSession(
        RoutingId sessionRid,
        ZLinkBackendActorRef actor,
        long bindingGeneration,
        ZLinkJavaStreamSocket stream,
        Duration timeout) {
        StreamBinding current = streamBindings.get(actor.actorId());
        if (current == null
            || !current.sessionRid().equals(sessionRid)
            || !current.actor().equals(actor)
            || current.bindingGeneration() != bindingGeneration
            || current.stream() != stream) {
            return CompletableFuture.failedFuture(
                new IllegalStateException(
                    "STREAM session binding is stale"));
        }
        if (isCurrentActor(actor)) {
            removeStreamBinding(
                sessionRid, actor, bindingGeneration, stream);
            return CompletableFuture.completedFuture(null);
        }
        return owner.bindRemoteStreamSession(
            sessionRid,
            actor,
            current.authorityOwnerGeneration(),
            bindingGeneration,
            false,
            timeout)
            .thenRun(() -> removeStreamBinding(
                sessionRid, actor, bindingGeneration, stream));
    }

    void discardStreamSession(
        RoutingId sessionRid,
        ZLinkBackendActorRef actor,
        long bindingGeneration,
        ZLinkJavaStreamSocket stream) {
        removeStreamBinding(
            sessionRid, actor, bindingGeneration, stream);
    }

    synchronized boolean acceptRemoteStreamBinding(
        RoutingId sourceNodeRid,
        long sourceNodeGeneration,
        ZLinkServiceM6BWireCodec.BoundSessionBind command) {
        ZLinkServiceM6BWireCodec.ActorRouteFence route = command.actor();
        ZLinkBackendActorRef actor = route.actor();
        if (!isCurrentActor(actor)
            || !routingId().equals(actor.nodeRid())
            || route.targetNodeGeneration() != owner.lifecycleGeneration()
            || actorAuthorityOwnerGeneration(actor)
                != route.authorityOwnerGeneration()) {
            return false;
        }
        RemoteStreamBinding candidate = new RemoteStreamBinding(
            sourceNodeRid,
            sourceNodeGeneration,
            command.sessionRid(),
            actor,
            command.bindingGeneration(),
            route.authorityOwnerGeneration());
        if (!command.active()) {
            if (remoteStreamBindings.remove(
                    actor.actorId(), candidate)) {
                remoteStreamSequences.remove(actor.actorId());
            }
            return true;
        }
        RemoteStreamBinding current =
            remoteStreamBindings.get(actor.actorId());
        if (candidate.equals(current)) {
            return true;
        }
        if (current != null
            && current.sameSessionOwnerEpoch(candidate)
            && current.bindingGeneration()
                >= candidate.bindingGeneration()) {
            return false;
        }
        remoteStreamBindings.put(actor.actorId(), candidate);
        remoteStreamSequences.put(actor.actorId(), 0L);
        streamBindings.remove(actor.actorId());
        return true;
    }

    boolean acceptBoundSessionPush(
        RoutingId sourceNodeRid,
        long sourceNodeGeneration,
        ZLinkServiceM6BWireCodec.BoundSessionSend command,
        List<Message> parts) {
        StreamBinding binding =
            streamBindings.get(command.actor().actor().actorId());
        return binding != null
            && binding.actor().equals(command.actor().actor())
            && binding.bindingGeneration()
                == command.expectedBindingGeneration()
            && binding.authorityOwnerGeneration()
                == command.actor().authorityOwnerGeneration()
            && command.actor().actor().nodeRid().equals(sourceNodeRid)
            && command.actor().targetNodeGeneration()
                == sourceNodeGeneration
            && binding.stream().sendBoundSessionPush(
                binding.sessionRid(), parts, SendFlags.DONT_WAIT);
    }

    private void removeStreamBinding(
        RoutingId sessionRid,
        ZLinkBackendActorRef actor,
        long bindingGeneration,
        ZLinkJavaStreamSocket stream) {
        synchronized (this) {
            StreamBinding current = streamBindings.get(actor.actorId());
            if (current != null
                && current.sessionRid().equals(sessionRid)
                && current.actor().equals(actor)
                && current.bindingGeneration() == bindingGeneration
                && current.stream() == stream) {
                streamBindings.remove(actor.actorId(), current);
                streamBindingSequences.remove(actor.actorId());
            }
        }
    }

    private synchronized boolean installStreamBinding(
        StreamBinding binding) {
        StreamBinding current =
            streamBindings.get(binding.actor().actorId());
        if (current != null
            && current.bindingGeneration()
                >= binding.bindingGeneration()
            && !current.equals(binding)) {
            return false;
        }
        if (!binding.equals(current)) {
            streamBindings.put(binding.actor().actorId(), binding);
            streamBindingSequences.put(binding.actor().actorId(), 0L);
        }
        return true;
    }

    private boolean acceptStreamBindingSequence(
        String actorId,
        long sequence) {
        long current =
            streamBindingSequences.getOrDefault(actorId, -1L);
        if (sequence <= current) {
            return false;
        }
        streamBindingSequences.put(actorId, sequence);
        return true;
    }

    void registerInstanceSpotType(String stableType) {
        instanceSpots.register(stableType, this::createSpot);
    }

    void registerInstanceSpotType(
        String stableType,
        ZLinkInternalMeshNode.InstanceSpotActivationHandler handler) {
        instanceSpots.register(
            stableType,
            this::createSpot,
            (selectedType, spotId, generation, backendSpot) -> {
                InstanceAuthority authority = instanceAuthorities.get(spotId);
                if (authority == null
                    || authority.route().objectGeneration() != generation) {
                    return CompletableFuture.failedFuture(
                        new IllegalStateException(
                            "Instance Spot authority is missing or stale"));
                }
                return handler.activate(
                    selectedType, authority.route(), backendSpot);
            });
    }

    CompletionStage<ZLinkJavaInstanceSpotRegistry.Activation>
        activateInstanceSpot(
            String spotId,
            String stableType) {
        ZLinkJavaRawSpot current = spots.get(spotId);
        long generation = current == null
            ? nextGeneration.getAndIncrement()
            : current.lifecycleGeneration();
        return instanceSpots.activate(spotId, stableType, generation);
    }

    boolean closeInstanceSpot(String spotId, long generation) {
        return instanceSpots.close(spotId, generation);
    }

    ZLinkBackendSpot localSpot(String spotId) {
        return spots.get(spotId);
    }

    void registerInstanceSpotAuthority(
        String stableType,
        ZLinkServiceM6BWireCodec.InstanceRouteFence route) {
        java.util.Objects.requireNonNull(route, "route");
        if (!routingId().equals(route.targetNodeRid())) {
            throw new IllegalArgumentException(
                "Instance authority target is not local");
        }
        InstanceAuthority authority =
            new InstanceAuthority(stableType, route);
        InstanceAuthority current = instanceAuthorities.putIfAbsent(
            route.targetSpotId(), authority);
        if (current != null && !current.equals(authority)) {
            throw new IllegalStateException(
                "Instance Spot authority fence changed without replacement");
        }
    }

    void reconcileInstanceSpotAuthority(
        String stableType,
        ZLinkServiceM6BWireCodec.InstanceRouteFence route) {
        java.util.Objects.requireNonNull(route, "route");
        if (!routingId().equals(route.targetNodeRid())) {
            return;
        }
        instanceAuthorities.put(
            route.targetSpotId(),
            new InstanceAuthority(stableType, route));
    }

    void forgetInstanceSpotAuthority(
        ZLinkServiceM6BWireCodec.InstanceRouteFence route) {
        InstanceAuthority current =
            instanceAuthorities.get(route.targetSpotId());
        if (current != null && current.route().equals(route)) {
            instanceAuthorities.remove(
                route.targetSpotId(), current);
        }
    }

    boolean enqueueRemoteInstanceSpot(
        RoutingId sourceNodeRid,
        ZLinkServiceM6BWireCodec.InstanceSpotMessage header,
        byte[] metadata,
        List<Message> parts,
        Consumer<List<Message>> reply) {
        return enqueueRemoteInstanceSpot(
            sourceNodeRid,
            header,
            metadata,
            parts,
            null,
            reply);
    }

    boolean enqueueRemoteInstanceSpot(
        RoutingId sourceNodeRid,
        ZLinkServiceM6BWireCodec.InstanceSpotMessage header,
        byte[] metadata,
        List<Message> parts,
        ZLinkInboundDispatchBudget.Lease inboundDispatchLease,
        Consumer<List<Message>> reply) {
        return enqueueRemoteInstanceSpot(
            sourceNodeRid,
            header,
            metadata,
            parts,
            null,
            inboundDispatchLease,
            reply);
    }

    boolean enqueueRemoteInstanceSpot(
        RoutingId sourceNodeRid,
        ZLinkServiceM6BWireCodec.InstanceSpotMessage header,
        byte[] metadata,
        List<Message> parts,
        String contentType,
        ZLinkInboundDispatchBudget.Lease inboundDispatchLease,
        Consumer<List<Message>> reply) {
        InstanceAuthority authority =
            instanceAuthorities.get(header.route().targetSpotId());
        if (authority == null) {
            reconcileInstanceSpotAuthority(
                header.stableType(), header.route());
            authority = instanceAuthorities.get(
                header.route().targetSpotId());
        }
        if (authority == null
            || !authority.stableType().equals(header.stableType())
            || !sameInstanceAuthorityFence(
                authority.route(), header.route())) {
            if (inboundDispatchLease != null) {
                inboundDispatchLease.close();
            }
            return false;
        }
        ZLinkJavaInstanceSpotRegistry.Activation activation;
        try {
            activation = instanceSpots.activate(
                header.route().targetSpotId(),
                authority.stableType(),
                header.route().objectGeneration()).toCompletableFuture().join();
        } catch (RuntimeException failure) {
            if (inboundDispatchLease != null) {
                inboundDispatchLease.close();
            }
            return false;
        }
        if (!(activation.spot() instanceof ZLinkJavaRawSpot target)
            || target.lifecycleGeneration()
                != header.route().objectGeneration()) {
            if (inboundDispatchLease != null) {
                inboundDispatchLease.close();
            }
            return false;
        }
        target.enqueueRoute(new systems.zlink.framework.runtime.internal.backend
            .ZLinkBackendReceived(
                systems.zlink.framework.runtime.internal.backend
                    .ZLinkBackendRequestResult.OK,
                Optional.of(sourceNodeRid),
                Optional.ofNullable(header.sourceSpotId()),
                Optional.ofNullable(header.replyRouteId()),
                metadata,
                new byte[0],
                parts,
                header.request() ? reply : null,
                () -> { },
                contentType,
                inboundDispatchLease));
        return true;
    }

    private static boolean sameInstanceAuthorityFence(
        ZLinkServiceM6BWireCodec.InstanceRouteFence left,
        ZLinkServiceM6BWireCodec.InstanceRouteFence right) {
        return left.targetNodeRid().equals(right.targetNodeRid())
            && left.targetNodeGeneration()
                == right.targetNodeGeneration()
            && left.targetSpotId().equals(right.targetSpotId())
            && left.objectGeneration() == right.objectGeneration()
            && left.ownerId().equals(right.ownerId())
            && left.authorityOwnerGeneration()
                == right.authorityOwnerGeneration()
            && left.leaseGeneration() == right.leaseGeneration();
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
        return dispatchLocalActor(
            actor,
            parts,
            requestId,
            flags,
            sourceNodeRid,
            sourceSessionRid,
            0,
            0);
    }

    private boolean dispatchLocalActor(
        ZLinkBackendActorRef actor,
        List<Message> parts,
        long requestId,
        int flags,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        long sourceBindingGeneration,
        long sourceSessionSequence) {
        if (!isCurrentActor(actor)) {
            return false;
        }
        String targetSpotId = actorSpots.get(actor.actorId());
        ZLinkJavaRawSpot target = spots.get(targetSpotId);
        if (target == null) {
            return false;
        }
        List<Message> copied = ZLinkJavaRawSpot.copy(parts);
        byte[] acceptedRecord = owner.encodeLocalActorAccepted(
            actor,
            sourceNodeRid,
            sourceSessionRid,
            sourceBindingGeneration,
            sourceSessionSequence,
            requestId,
            parts);
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
                index + 1 < copied.size(),
                index == 0
                    ? acceptedRecord
                    : new byte[0]));
        }
        target.enqueueActor(messages);
        return true;
    }

    private boolean isCurrentActor(ZLinkBackendActorRef actor) {
        return actor != null && actor.equals(actors.get(actor.actorId()));
    }

    boolean isCurrentBoundActor(ZLinkBackendActorRef actor) {
        return isCurrentActor(actor);
    }

    private record StreamBinding(
        RoutingId sessionRid,
        ZLinkBackendActorRef actor,
        long bindingGeneration,
        long authorityOwnerGeneration,
        ZLinkJavaStreamSocket stream) {
    }

    record RemoteStreamBinding(
        RoutingId sessionOwnerNodeRid,
        long sessionOwnerNodeGeneration,
        RoutingId sessionRid,
        ZLinkBackendActorRef actor,
        long bindingGeneration,
        long authorityOwnerGeneration) {
        boolean sameSessionOwnerEpoch(RemoteStreamBinding other) {
            return sessionOwnerNodeRid.equals(
                    other.sessionOwnerNodeRid)
                && sessionOwnerNodeGeneration
                    == other.sessionOwnerNodeGeneration;
        }
    }

    private record InstanceAuthority(
        String stableType,
        ZLinkServiceM6BWireCodec.InstanceRouteFence route) {
        private InstanceAuthority {
            if (stableType == null || stableType.isBlank()) {
                throw new IllegalArgumentException(
                    "Instance Spot stable type is required");
            }
        }
    }

    private record SpotAuthorityKey(
        RoutingId nodeRid,
        String spotId,
        long objectGeneration) {
    }

    private record ActorAuthorityKey(
        RoutingId nodeRid,
        String actorId,
        long objectGeneration) {
    }

    boolean publish(
        ZLinkJavaRawSpot source,
        String channelName,
        String topic,
        byte[] metadata,
        List<Message> parts) {
        owner.publishLogicalMulticast(
            source, channelName, topic, metadata, parts);
        return true;
    }

    void enqueueLogicalMulticast(
        String channelName,
        String topic,
        String sourceSpotId,
        RoutingId sourceNodeRid,
        byte[] metadata,
        List<Message> parts) {
        enqueueLogicalMulticast(
            channelName,
            topic,
            sourceSpotId,
            sourceNodeRid,
            metadata,
            null,
            parts);
    }

    void enqueueLogicalMulticast(
        String channelName,
        String topic,
        String sourceSpotId,
        RoutingId sourceNodeRid,
        byte[] metadata,
        String contentType,
        List<Message> parts) {
        for (ZLinkJavaRawSpot target : spots.values()) {
            if (!target.accepts(topic)) {
                continue;
            }
            ZLinkInboundDispatchBudget budget =
                owner.applicationDispatchBudget();
            ZLinkInboundDispatchBudget.Lease lease = budget == null
                ? null
                : budget.track(parts.size() > 1
                    ? parts.get(1).size()
                    : parts.getFirst().size());
            target.enqueueTopic(
                new systems.zlink.framework.runtime.internal.backend
                .ZLinkBackendTopicMessage(
                    Optional.of(sourceNodeRid),
                    channelName,
                    topic,
                    metadata == null ? new byte[0] : metadata.clone(),
                    ZLinkJavaRawSpot.copy(parts),
                    contentType,
                    lease));
        }
    }

    boolean sendToSpot(
        ZLinkJavaRawSpot source,
        RoutingId targetNodeRid,
        String targetSpotId,
        long targetGeneration,
        byte[] metadata,
        List<Message> parts) {
        if (!routingId().equals(targetNodeRid)) {
            return owner.sendSpot(
                source.spotId(),
                targetNodeRid,
                targetSpotId,
                targetGeneration,
                metadata,
                parts);
        }
        ZLinkJavaRawSpot target = localSpot(
            targetNodeRid, targetSpotId, targetGeneration);
        if (target == null) {
            return false;
        }
        byte[] acceptedRecord = owner.encodeLocalSpotAccepted(
            source.spotId(),
            targetSpotId,
            targetGeneration,
            metadata,
            parts,
            null);
        target.enqueueRoute(new systems.zlink.framework.runtime.internal.backend
            .ZLinkBackendReceived(
                systems.zlink.framework.runtime.internal.backend
                    .ZLinkBackendRequestResult.OK,
                Optional.of(routingId()),
                Optional.of(source.spotId()),
                Optional.empty(),
                metadata,
                acceptedRecord,
                ZLinkJavaRawSpot.copy(parts),
                null,
                () -> { },
                ZLinkChannelContentTypeFrame.decode(parts),
                null));
        return true;
    }

    boolean requestToSpot(
        ZLinkJavaRawSpot source,
        RoutingId targetNodeRid,
        String targetSpotId,
        long targetGeneration,
        byte[] metadata,
        List<Message> parts,
        ZLinkBackendRequestCallback callback,
        Duration timeout) {
        if (!routingId().equals(targetNodeRid)) {
            return owner.requestSpot(
                source.spotId(),
                targetNodeRid,
                targetSpotId,
                targetGeneration,
                metadata,
                parts,
                callback,
                timeout);
        }
        ZLinkJavaRawSpot target = localSpot(
            targetNodeRid, targetSpotId, targetGeneration);
        if (target == null) {
            return false;
        }
        long sequence = nextRequestSequence.getAndIncrement();
        byte[] acceptedRecord = owner.encodeLocalSpotAccepted(
            source.spotId(),
            targetSpotId,
            targetGeneration,
            metadata,
            parts,
            sequence);
        AtomicBoolean terminal = new AtomicBoolean();
        CompletionStage<Void> enqueued = target.enqueueRoute(
            new systems.zlink.framework.runtime.internal.backend
            .ZLinkBackendReceived(
                systems.zlink.framework.runtime.internal.backend
                    .ZLinkBackendRequestResult.OK,
                Optional.of(routingId()),
                Optional.of(source.spotId()),
                Optional.of(sequence),
                metadata,
                acceptedRecord,
                ZLinkJavaRawSpot.copy(parts),
                reply -> {
                    if (!terminal.compareAndSet(false, true)) {
                        return;
                    }
                    callback.handle(new systems.zlink.framework.runtime.internal.backend
                        .ZLinkBackendReceived(
                            systems.zlink.framework.runtime.internal.backend
                                .ZLinkBackendRequestResult.OK,
                            Optional.of(targetNodeRid),
                            Optional.of(targetSpotId),
                            Optional.of(sequence),
                            ZLinkJavaRawSpot.copy(reply)));
                },
                () -> { },
                ZLinkChannelContentTypeFrame.decode(parts),
                null));
        enqueued.whenComplete((ignored, failure) -> {
            if (failure != null && terminal.compareAndSet(false, true)) {
                callback.handle(new systems.zlink.framework.runtime.internal.backend
                    .ZLinkBackendReceived(
                        systems.zlink.framework.runtime.internal.backend
                            .ZLinkBackendRequestResult.INTERNAL_ERROR,
                        Optional.of(targetNodeRid),
                        Optional.of(targetSpotId),
                        Optional.of(sequence),
                        List.of()));
            }
        });
        if (timeout != null && !timeout.isNegative() && !timeout.isZero()) {
            CompletableFuture.delayedExecutor(
                timeout.toNanos(),
                java.util.concurrent.TimeUnit.NANOSECONDS).execute(() -> {
                    if (terminal.compareAndSet(false, true)) {
                        callback.handle(new systems.zlink.framework.runtime.internal.backend
                            .ZLinkBackendReceived(
                                systems.zlink.framework.runtime.internal.backend
                                    .ZLinkBackendRequestResult.TIMED_OUT,
                                Optional.of(targetNodeRid),
                                Optional.of(targetSpotId),
                                Optional.of(sequence),
                                List.of()));
                    }
                });
        }
        return true;
    }

    private ZLinkJavaRawSpot localSpot(
        RoutingId targetNodeRid,
        String targetSpotId,
        long targetGeneration) {
        if (!routingId().equals(targetNodeRid)) {
            return null;
        }
        ZLinkJavaRawSpot target = spots.get(targetSpotId);
        if (target == null) {
            return null;
        }
        return targetGeneration == 0
                || target.lifecycleGeneration() == targetGeneration
            ? target
            : null;
    }
}
