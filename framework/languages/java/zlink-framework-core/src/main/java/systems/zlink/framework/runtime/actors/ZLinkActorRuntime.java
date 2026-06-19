package systems.zlink.framework.runtime.actors;

import systems.zlink.framework.runtime.backend.*;

import java.time.Duration;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CompletionStage;
import java.util.function.BiFunction;
import java.util.function.Function;
import java.util.function.Supplier;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorFactory;
import systems.zlink.framework.actors.ZLinkActorJoinEntrySpotCall;
import systems.zlink.framework.actors.ZLinkActorJoinResult;
import systems.zlink.framework.actors.ZLinkActorJoinSpotCall;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.actors.ZLinkActorRef;
import systems.zlink.framework.actors.ZLinkBoundSession;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.execution.ZLinkAsyncSerialQueue;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerFactory;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerStages;
import systems.zlink.framework.runtime.messaging.ZLinkPayloadEncoding;
import systems.zlink.framework.runtime.channels.ZLinkChannelRuntime;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotRemoteAddress;
import systems.zlink.framework.spots.ZLinkSpotRemoteAddressResolver;
import systems.zlink.framework.streams.ZLinkStreamCodec;

public final class ZLinkActorRuntime implements ZLinkActorManager {
    private final ZLinkBackendSpotNode spotNode;
    private final Map<String, Class<? extends ZLinkActorFactory>> factories;
    private final Duration defaultTimeout;
    private final ZLinkMessageSerializer serializer;
    private final ZLinkHandlerFactory handlerFactory;
    private final ZLinkStreamCodec defaultStreamCodec;
    private final Map<String, ZLinkActor> actors = new HashMap<>();
    private final Map<String, String> actorTypes = new HashMap<>();
    private final Map<ZLinkActor, DefaultActorContext> contextsByActor = new HashMap<>();
    private final Map<String, ZLinkAsyncSerialQueue> dispatchQueues = new HashMap<>();
    private BiFunction<RoutingId, ZLinkActor, CompletionStage<Void>> createdNotifier =
        (ignoredNode, ignoredActor) -> CompletableFuture.completedFuture(null);
    private Function<ZLinkActor, CompletionStage<Void>> disconnectedNotifier =
        ignored -> CompletableFuture.completedFuture(null);
    private Function<RoutingId, ZLinkSpot<?>> spotResolver = ignored -> null;
    private ZLinkSpotRemoteAddressResolver remoteAddressResolver;
    private ZLinkChannelRuntime routedTransport;
    private Supplier<RoutingId> sourceEntrySpotRid = () -> RoutingId.from(new byte[0]);

    public ZLinkActorRuntime(
        ZLinkBackendSpotNode spotNode,
        Map<String, Class<? extends ZLinkActorFactory>> factories,
        Duration defaultTimeout,
        ZLinkMessageSerializer serializer) {
        this(spotNode, factories, defaultTimeout, serializer, ZLinkHandlerFactory.reflection());
    }

    public ZLinkActorRuntime(
        ZLinkBackendSpotNode spotNode,
        Map<String, Class<? extends ZLinkActorFactory>> factories,
        Duration defaultTimeout,
        ZLinkMessageSerializer serializer,
        ZLinkHandlerFactory handlerFactory) {
        this(
            spotNode,
            factories,
            defaultTimeout,
            serializer,
            handlerFactory,
            ZLinkStreamCodec.JSON);
    }

    public ZLinkActorRuntime(
        ZLinkBackendSpotNode spotNode,
        Map<String, Class<? extends ZLinkActorFactory>> factories,
        Duration defaultTimeout,
        ZLinkMessageSerializer serializer,
        ZLinkHandlerFactory handlerFactory,
        ZLinkStreamCodec defaultStreamCodec) {
        if (factories.isEmpty()) {
            throw new ZLinkConfigurationException("at least one actor factory is required");
        }
        if (serializer == null) {
            throw new ZLinkConfigurationException("serializer is required");
        }
        if (handlerFactory == null) {
            throw new ZLinkConfigurationException("handlerFactory is required");
        }
        this.spotNode = spotNode;
        this.factories = Map.copyOf(factories);
        this.defaultTimeout = defaultTimeout;
        this.serializer = serializer;
        this.handlerFactory = handlerFactory;
        this.defaultStreamCodec =
            defaultStreamCodec == null ? ZLinkStreamCodec.JSON : defaultStreamCodec;
    }

    @Override
    public CompletionStage<ZLinkActor> create(String actorId, String actorType) {
        requireActorId(actorId);
        Class<? extends ZLinkActorFactory> factoryType = requireFactory(actorType);
        if (actors.containsKey(actorId)) {
            throw new ZLinkConfigurationException("duplicate actor id: " + actorId);
        }
        ZLinkBackendActorRef actorRef = spotNode.createActor(actorId);
        DefaultActorContext context = new DefaultActorContext(actorRef);
        return ZLinkHandlerStages
            .fromSupplier(() -> createFactory(factoryType).create(actorId, context))
            .thenApply(actor -> {
                actors.put(actorId, actor);
                actorTypes.put(actorId, actorType);
                contextsByActor.put(actor, context);
                return actor;
            })
            .thenCompose(actor -> submitActorDispatch(
                    actor.actorId(),
                    () -> createdNotifier.apply(actorRef.nodeRid(), actor))
                .thenApply(ignored -> actor));
    }

    @Override
    public CompletionStage<Optional<ZLinkActor>> find(String actorId) {
        requireActorId(actorId);
        if (!actors.containsKey(actorId)) {
            spotNode.actorLookup(actorId);
        }
        return CompletableFuture.completedFuture(Optional.ofNullable(actors.get(actorId)));
    }

    @Override
    public CompletionStage<ZLinkActor> getOrCreate(String actorId, String actorType) {
        requireActorId(actorId);
        ZLinkActor actor = actors.get(actorId);
        if (actor != null) {
            return CompletableFuture.completedFuture(actor);
        }
        return create(actorId, actorType);
    }

    private Class<? extends ZLinkActorFactory> requireFactory(String actorType) {
        if (actorType == null || actorType.isBlank()) {
            throw new ZLinkConfigurationException("actorType is required");
        }
        Class<? extends ZLinkActorFactory> factory = factories.get(actorType);
        if (factory == null) {
            throw new ZLinkConfigurationException("actor type is not registered: " + actorType);
        }
        return factory;
    }

    private static void requireActorId(String actorId) {
        if (actorId == null || actorId.isBlank()) {
            throw new ZLinkConfigurationException("actorId is required");
        }
    }

    private ZLinkActorFactory createFactory(
        Class<? extends ZLinkActorFactory> factoryType) {
        try {
            return (ZLinkActorFactory) handlerFactory.create(factoryType);
        } catch (RuntimeException ex) {
            throw new ZLinkConfigurationException(
                "failed to create actor factory: " + factoryType.getName(),
                ex);
        }
    }

    ZLinkBackendActorRef refFor(ZLinkActor actor) {
        DefaultActorContext context = contextsByActor.get(actor);
        if (context == null) {
            throw new ZLinkConfigurationException(
                "actor is not managed by this runtime: " + actor.actorId());
        }
        return context.actorRef;
    }

    public ZLinkBackendActorRef currentRef(ZLinkActor actor) {
        return refFor(actor);
    }

    String actorTypeFor(ZLinkActor actor) {
        String actorType = actorTypes.get(actor.actorId());
        if (actorType == null || actorType.isBlank()) {
            throw new ZLinkConfigurationException(
                "actor type is not available: " + actor.actorId());
        }
        return actorType;
    }

    long bindSession(ZLinkActor actor, ZLinkBoundSession boundSession) {
        DefaultActorContext context = contextsByActor.get(actor);
        if (context == null) {
            throw new ZLinkConfigurationException(
                "actor is not managed by this runtime: " + actor.actorId());
        }
        return context.setBoundSession(boundSession);
    }

    boolean clearSessionBinding(ZLinkActor actor, long bindingToken) {
        DefaultActorContext context = contextsByActor.get(actor);
        if (context == null) {
            throw new ZLinkConfigurationException(
                "actor is not managed by this runtime: " + actor.actorId());
        }
        return context.clearBoundSession(bindingToken);
    }

    public CompletionStage<Message> handleEntrySpotRouteJoin(
        RoutingId sourceRoutingId,
        Message payload) {
        ZLinkActorEntrySpotRoutePackets.JoinRequest request =
            ZLinkActorEntrySpotRoutePackets.decodeJoinRequest(payload);
        return getOrCreate(request.actorId(), request.actorType())
            .thenApply(actor -> {
                ZLinkBackendActorRef actorRef = refFor(actor);
                return ZLinkActorEntrySpotRoutePackets.encodeJoinReply(
                    actor.actorId(),
                    actorTypes.getOrDefault(actor.actorId(), request.actorType()),
                    actorRef.nodeRid(),
                    actorRef.epoch());
            });
    }

    public Optional<ZLinkActor> localActor(String actorId) {
        return Optional.ofNullable(actors.get(actorId));
    }

    public Optional<RoutingId> spotRid(ZLinkActor actor) {
        DefaultActorContext context = contextsByActor.get(actor);
        if (context == null) {
            throw new ZLinkConfigurationException(
                "actor is not managed by this runtime: " + actor.actorId());
        }
        return Optional.ofNullable(context.spotRid);
    }

    public boolean canRouteRemoteJoinedSpot(RoutingId spotRid) {
        return spotRid != null
            && spotResolver.apply(spotRid) == null
            && remoteAddressResolver != null
            && routedTransport != null;
    }

    public CompletionStage<Optional<Message>> dispatchRemoteJoinedActor(
        ZLinkBackendActorRef actorRef,
        RoutingId spotRid,
        systems.zlink.framework.streams.ZLinkStreamHeader header,
        Message payload) {
        if (!canRouteRemoteJoinedSpot(spotRid)) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "actor Spot is not routable: " + spotRid));
        }
        return remoteAddressResolver.resolveSpotRemoteAddressAsync(spotRid)
            .thenCompose(address -> {
                List<Message> parts =
                    ZLinkActorSpotRoutePackets.createActorPacketParts(actorRef, header, payload);
                try {
                    if (header.requestSequence().isPresent()
                        || header.kind() == systems.zlink.framework.streams.ZLinkStreamMessageKind.REQUEST) {
                        return routedTransport.requestToSpotViaEgressChannel(
                                address.routerChannelId(),
                                address.targetNodeRid(),
                                address.spotRid(),
                                parts,
                                defaultTimeout)
                            .thenApply(replyParts -> {
                                try {
                                    return replyParts.isEmpty()
                                        ? Optional.<Message>empty()
                                        : Optional.of(Message.from(replyParts.get(0)));
                                } finally {
                                    replyParts.forEach(Message::close);
                                }
                            });
                    }
                    return routedTransport.sendToSpotViaEgressChannel(
                            address.routerChannelId(),
                            address.targetNodeRid(),
                            address.spotRid(),
                            parts)
                        .thenApply(ignored -> Optional.<Message>empty());
                } finally {
                    parts.forEach(Message::close);
                }
            });
    }

    public boolean hasActorsInSpot(RoutingId spotRid) {
        for (DefaultActorContext context : contextsByActor.values()) {
            if (spotRid.equals(context.spotRid)) {
                return true;
            }
        }
        return false;
    }

    public CompletionStage<Optional<ZLinkActor>> getOrCreateLocalActor(
        String actorId,
        Class<?> expectedActorType) {
        requireActorId(actorId);
        ZLinkActor existing = actors.get(actorId);
        if (existing != null) {
            return CompletableFuture.completedFuture(
                expectedActorType.isInstance(existing)
                    ? Optional.of(existing)
                    : Optional.empty());
        }
        if (factories.size() != 1) {
            return CompletableFuture.completedFuture(Optional.empty());
        }
        String actorType = factories.keySet().iterator().next();
        return create(actorId, actorType)
            .thenApply(actor -> expectedActorType.isInstance(actor)
                ? Optional.of(actor)
                : Optional.empty());
    }

    public CompletionStage<Void> submitActorDispatch(
        String actorId,
        Supplier<CompletionStage<Void>> operation) {
        ZLinkAsyncSerialQueue queue;
        synchronized (this) {
            if (!actors.containsKey(actorId)) {
                return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                    "actor is not managed by this runtime: " + actorId));
            }
            queue = dispatchQueues.computeIfAbsent(actorId, ignored -> new ZLinkAsyncSerialQueue());
        }
        return queue.enqueue(operation);
    }

    public void setDisconnectedNotifier(
        Function<ZLinkActor, CompletionStage<Void>> disconnectedNotifier) {
        this.disconnectedNotifier = disconnectedNotifier == null
            ? ignored -> CompletableFuture.completedFuture(null)
            : disconnectedNotifier;
    }

    public void setCreatedNotifier(
        BiFunction<RoutingId, ZLinkActor, CompletionStage<Void>> createdNotifier) {
        this.createdNotifier = createdNotifier == null
            ? (ignoredNode, ignoredActor) -> CompletableFuture.completedFuture(null)
            : createdNotifier;
    }

    public void setSpotResolver(Function<RoutingId, ZLinkSpot<?>> spotResolver) {
        this.spotResolver = spotResolver == null ? ignored -> null : spotResolver;
    }

    public void setRemoteAddressResolver(ZLinkSpotRemoteAddressResolver remoteAddressResolver) {
        this.remoteAddressResolver = remoteAddressResolver;
    }

    public void setRoutedTransport(
        ZLinkChannelRuntime routedTransport,
        Supplier<RoutingId> sourceEntrySpotRid) {
        this.routedTransport = routedTransport;
        this.sourceEntrySpotRid = sourceEntrySpotRid == null
            ? () -> RoutingId.from(new byte[0])
            : sourceEntrySpotRid;
    }

    public CompletionStage<Void> notifyDisconnected(ZLinkActor actor) {
        if (actor == null) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "actor is required"));
        }
        return submitActorDispatch(
            actor.actorId(),
            () -> disconnectedNotifier.apply(actor));
    }

    public void markJoined(
        ZLinkActor actor,
        ZLinkBackendActorRef actorRef,
        RoutingId spotRid) {
        markJoined(actor, actorRef, spotRid, null);
    }

    public void markJoined(
        ZLinkActor actor,
        ZLinkBackendActorRef actorRef,
        RoutingId spotRid,
        ZLinkSpot<?> spot) {
        DefaultActorContext context = contextsByActor.get(actor);
        if (context == null) {
            throw new ZLinkConfigurationException(
                "actor is not managed by this runtime: " + actor.actorId());
        }
        context.actorRef = actorRef;
        context.spotRid = spotRid;
        context.spot = spot;
        context.joined = true;
    }

    public void bindNativeSession(
        ZLinkActor actor,
        ZLinkBackendSpotNode spotNode,
        ZLinkBackendActorRef actorRef) {
        DefaultActorContext context = contextsByActor.get(actor);
        if (context == null) {
            throw new ZLinkConfigurationException(
                "actor is not managed by this runtime: " + actor.actorId());
        }
        context.actorRef = actorRef;
        ZLinkNativeBoundSessionRuntime boundSession = new ZLinkNativeBoundSessionRuntime(
            spotNode,
            actorRef,
            serializer,
            this,
            actor,
            defaultTimeout,
            defaultStreamCodec);
        long bindingToken = bindSession(actor, boundSession);
        boundSession.setBindingToken(bindingToken);
    }

    public void bindRoutedSession(
        ZLinkActor actor,
        String routeChannelName,
        RoutingId targetRoutePeerRid,
        RoutingId targetEntrySpotRid,
        ZLinkBackendActorRef actorRef) {
        ZLinkRoutedBoundSessionRuntime boundSession = new ZLinkRoutedBoundSessionRuntime(
            spotNode.entrySpot(),
            targetRoutePeerRid,
            targetEntrySpotRid,
            actorRef,
            serializer,
            this,
            actor,
            defaultTimeout,
            defaultStreamCodec);
        long bindingToken = bindSession(actor, boundSession);
        boundSession.setBindingToken(bindingToken);
    }

    public void markLeft(ZLinkActor actor) {
        DefaultActorContext context = contextsByActor.get(actor);
        if (context == null) {
            throw new ZLinkConfigurationException(
                "actor is not managed by this runtime: " + actor.actorId());
        }
        context.spotRid = null;
        context.spot = null;
        context.joined = false;
    }

    public CompletionStage<Void> destroyFromEntrySpot(
        RoutingId entryNodeRid,
        ZLinkActor actor) {
        if (entryNodeRid == null) {
            throw new ZLinkConfigurationException("entryNodeRid is required");
        }
        if (actor == null) {
            throw new ZLinkConfigurationException("actor is required");
        }

        DefaultActorContext context;
        ZLinkBackendActorRef actorRef;
        synchronized (this) {
            ZLinkActor current = actors.get(actor.actorId());
            if (current == null || current != actor) {
                return CompletableFuture.completedFuture(null);
            }

            context = contextsByActor.get(actor);
            if (context == null) {
                return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                    "actor does not have a native Actor ref: " + actor.actorId()));
            }
            try {
                actorRef = context.beginDestroy(entryNodeRid, actor.actorId());
            } catch (ZLinkConfigurationException ex) {
                return CompletableFuture.failedFuture(ex);
            }
            if (actorRef == null) {
                return CompletableFuture.completedFuture(null);
            }
        }

        return spotNode.destroyActor(actorRef, defaultTimeout)
            .thenCompose(ignored -> context.disconnectBoundSessionForDestroy())
            .thenRun(() -> {
                synchronized (this) {
                    context.clearAfterDestroy();
                    actors.remove(actor.actorId(), actor);
                    actorTypes.remove(actor.actorId());
                    contextsByActor.remove(actor);
                    dispatchQueues.remove(actor.actorId());
                }
            })
            .whenComplete((ignored, error) -> {
                if (error != null) {
                    synchronized (this) {
                        context.resetDestroying();
                    }
                }
            });
    }

    public void close() {
        List<Map.Entry<ZLinkActor, DefaultActorContext>> snapshot;
        synchronized (this) {
            snapshot = List.copyOf(contextsByActor.entrySet());
        }

        for (Map.Entry<ZLinkActor, DefaultActorContext> entry : snapshot) {
            ZLinkActor actor = entry.getKey();
            DefaultActorContext context = entry.getValue();
            ZLinkBackendActorRef actorRef = context.actorRef;
            if (actorRef == null) {
                continue;
            }

            try {
                context.disconnectBoundSessionForDestroy().toCompletableFuture().join();
            } catch (RuntimeException ignored) {
                // Runtime shutdown is best-effort; native context close will release remaining handles.
            }

            try {
                spotNode.destroyActor(actorRef, defaultTimeout).toCompletableFuture().join();
            } catch (RuntimeException ignored) {
                // Actors outside the Entry Spot may reject destroy. Shutdown must still release maps.
            }

            synchronized (this) {
                context.clearAfterDestroy();
                actors.remove(actor.actorId(), actor);
                actorTypes.remove(actor.actorId());
                contextsByActor.remove(actor);
                dispatchQueues.remove(actor.actorId());
            }
        }
    }

    private final class DefaultActorContext implements ZLinkActorContext {
        private ZLinkBackendActorRef actorRef;
        private ZLinkBoundSession boundSession;
        private long sessionBindingToken;
        private RoutingId spotRid;
        private ZLinkSpot<?> spot;
        private boolean joined;
        private boolean destroying;

        DefaultActorContext(ZLinkBackendActorRef actorRef) {
            this.actorRef = actorRef;
        }

        @Override
        public Optional<RoutingId> spotRid() {
            return Optional.ofNullable(spotRid);
        }

        @Override
        public boolean isJoined() {
            return joined;
        }

        @Override
        public ZLinkBoundSession boundSession() {
            if (boundSession == null) {
                throw new ZLinkConfigurationException("actor has no bound session");
            }
            return boundSession;
        }

        @Override
        public ZLinkSpot<?> getSpot() {
            if (spot == null) {
                throw new ZLinkConfigurationException("actor has not joined a user Spot");
            }
            return spot;
        }

        @Override
        public <TSpot extends ZLinkSpot<?>> TSpot getSpot(Class<TSpot> spotType) {
            if (spotType == null) {
                throw new ZLinkConfigurationException("spotType is required");
            }
            ZLinkSpot<?> current = getSpot();
            if (!spotType.isInstance(current)) {
                throw new ZLinkConfigurationException(
                    "actor joined Spot type "
                        + current.getClass().getName()
                        + ", not "
                        + spotType.getName());
            }
            return spotType.cast(current);
        }

        @Override
        public ZLinkActorJoinEntrySpotCall joinEntrySpot(RoutingId spotNodeRid, Object request) {
            if (spotNodeRid == null) {
                throw new ZLinkConfigurationException("spotNodeRid is required");
            }
            if (request == null) {
                throw new ZLinkConfigurationException("request is required");
            }
            ZLinkPayloadEncoding.EncodedPayload encoded =
                ZLinkPayloadEncoding.encode(serializer, request);
            return new JoinEntrySpotCall(this, spotNodeRid, encoded.payload(), defaultTimeout);
        }

        @Override
        public ZLinkActorJoinSpotCall joinSpot(RoutingId spotRid, Object request) {
            if (spotRid == null) {
                throw new ZLinkConfigurationException("spotRid is required");
            }
            if (request == null) {
                throw new ZLinkConfigurationException("request is required");
            }
            ZLinkPayloadEncoding.EncodedPayload encoded =
                ZLinkPayloadEncoding.encode(serializer, request);
            return new JoinSpotCall(this, spotRid, encoded.payload(), defaultTimeout);
        }

        long setBoundSession(ZLinkBoundSession boundSession) {
            sessionBindingToken++;
            this.boundSession = boundSession;
            return sessionBindingToken;
        }

        boolean clearBoundSession(long bindingToken) {
            if (bindingToken == sessionBindingToken) {
                boundSession = null;
                return true;
            }
            return false;
        }

        CompletionStage<Void> disconnectBoundSessionForDestroy() {
            ZLinkBoundSession current = boundSession;
            if (current == null) {
                return CompletableFuture.completedFuture(null);
            }
            return current.disconnect();
        }

        void clearAfterDestroy() {
            actorRef = null;
            boundSession = null;
            sessionBindingToken++;
            spotRid = null;
            spot = null;
            joined = false;
            destroying = false;
        }

        ZLinkBackendActorRef beginDestroy(RoutingId entryNodeRid, String actorId) {
            if (destroying) {
                return null;
            }
            if (actorRef == null) {
                throw new ZLinkConfigurationException(
                    "actor does not have a native Actor ref: " + actorId);
            }
            if (spot != null) {
                throw new ZLinkConfigurationException(
                    "actor must leave its current Spot before destroy: " + actorId);
            }
            if (!actorRef.nodeRid().equals(entryNodeRid)) {
                throw new ZLinkConfigurationException(
                    "actor is not owned by this Entry Spot: " + actorId);
            }

            destroying = true;
            return actorRef;
        }

        void resetDestroying() {
            destroying = false;
        }
    }

    private final class JoinEntrySpotCall implements ZLinkActorJoinEntrySpotCall {
        private final DefaultActorContext context;
        private final RoutingId spotNodeRid;
        private final Message request;
        private final Duration timeout;

        JoinEntrySpotCall(
            DefaultActorContext context,
            RoutingId spotNodeRid,
            Message request,
            Duration timeout) {
            this.context = context;
            this.spotNodeRid = spotNodeRid;
            this.request = request;
            this.timeout = timeout;
        }

        @Override
        public ZLinkActorJoinEntrySpotCall timeout(Duration timeout) {
            if (timeout == null || timeout.isNegative() || timeout.isZero()) {
                throw new ZLinkConfigurationException("timeout must be positive");
            }
            return new JoinEntrySpotCall(context, spotNodeRid, request, timeout);
        }

        @Override
        public <TReply> CompletionStage<ZLinkActorJoinResult<TReply>> submit(
            Class<TReply> replyType) {
            if (replyType == null) {
                throw new ZLinkConfigurationException("replyType is required");
            }
            Message requestPart = Message.from(request);
            try {
                return spotNode.joinActorEntrySpot(
                        context.actorRef,
                        spotNodeRid,
                        requestPart,
                        timeout)
                    .thenApply(result -> {
                        if (result.result() != ZLinkBackendRequestResult.OK) {
                            throw new ZLinkConfigurationException(
                                "actor entry spot join failed: " + result.result());
                        }
                        Message emptyReply = null;
                        try {
                            if (result.joinResultCode() == 0) {
                                context.actorRef = result.actor();
                                context.spotRid = result.targetNodeRid();
                                context.spot = null;
                                context.joined = true;
                            }
                            Message firstReply = result.replyParts().isEmpty()
                                ? (emptyReply = Message.from(new byte[0]))
                                : result.replyParts().get(0);
                            TReply reply = serializer.deserialize(firstReply, replyType);
                            return new ZLinkActorJoinResult<>(
                                result.joinResultCode(),
                                new ZLinkActorRef(
                                    result.actor().nodeRid(),
                                    result.actor().actorId(),
                                    result.actor().epoch()),
                                reply);
                        } finally {
                            if (emptyReply != null) {
                                emptyReply.close();
                            }
                            result.replyParts().forEach(Message::close);
                        }
                    });
            } finally {
                requestPart.close();
            }
        }
    }

    private final class JoinSpotCall implements ZLinkActorJoinSpotCall {
        private final DefaultActorContext context;
        private final RoutingId spotRid;
        private final Message request;
        private final Duration timeout;

        JoinSpotCall(
            DefaultActorContext context,
            RoutingId spotRid,
            Message request,
            Duration timeout) {
            this.context = context;
            this.spotRid = spotRid;
            this.request = request;
            this.timeout = timeout;
        }

        @Override
        public ZLinkActorJoinSpotCall timeout(Duration timeout) {
            if (timeout == null || timeout.isNegative() || timeout.isZero()) {
                throw new ZLinkConfigurationException("timeout must be positive");
            }
            return new JoinSpotCall(context, spotRid, request, timeout);
        }

        @Override
        public <TReply> CompletionStage<ZLinkActorJoinResult<TReply>> submit(
            Class<TReply> replyType) {
            if (replyType == null) {
                throw new ZLinkConfigurationException("replyType is required");
            }
            Message requestPart = Message.from(request);
            ZLinkSpot<?> localSpot = spotResolver.apply(spotRid);
            if (localSpot == null && remoteAddressResolver != null && routedTransport != null) {
                return joinRemoteRoutedSpot(replyType, requestPart)
                    .whenComplete((ignored, error) -> requestPart.close())
                    .thenApply(result -> decodeJoinResult(result, replyType));
            }
            CompletionStage<RoutingId> targetNode =
                localSpot != null
                    ? CompletableFuture.completedFuture(context.actorRef.nodeRid())
                    : resolveRemoteTargetNode(spotRid);
                return targetNode.handle((nodeRid, error) -> {
                    if (error != null) {
                        requestPart.close();
                        throw new CompletionException(error);
                    }
                    try {
                        return spotNode.joinActor(
                            context.actorRef,
                            nodeRid,
                            spotRid,
                            List.of(requestPart),
                            timeout);
                    } finally {
                        requestPart.close();
                    }
                })
                .thenCompose(stage -> stage)
                    .thenApply(result -> decodeJoinResult(result, replyType));
        }

        private <TReply> ZLinkActorJoinResult<TReply> decodeJoinResult(
            ZLinkBackendActorJoinResult result,
            Class<TReply> replyType) {
            if (result.result() != ZLinkBackendRequestResult.OK) {
                throw new ZLinkConfigurationException(
                    "actor spot join failed: " + result.result());
            }
            Message emptyReply = null;
            try {
                if (result.joinResultCode() == 0) {
                    context.actorRef = result.actor();
                    context.spotRid = result.joinedSpotRid();
                    context.spot = spotResolver.apply(result.joinedSpotRid());
                    context.joined = true;
                }
                Message firstReply = result.replyParts().isEmpty()
                    ? (emptyReply = Message.from(new byte[0]))
                    : result.replyParts().get(0);
                TReply reply = serializer.deserialize(firstReply, replyType);
                return new ZLinkActorJoinResult<>(
                    result.joinResultCode(),
                    new ZLinkActorRef(
                        result.actor().nodeRid(),
                        result.actor().actorId(),
                        result.actor().epoch()),
                    reply);
            } finally {
                if (emptyReply != null) {
                    emptyReply.close();
                }
                result.replyParts().forEach(Message::close);
            }
        }

        private <TReply> CompletionStage<ZLinkBackendActorJoinResult> joinRemoteRoutedSpot(
            Class<TReply> replyType,
            Message requestPart) {
            return remoteAddressResolver.resolveSpotRemoteAddressAsync(spotRid)
                .thenCompose(address -> {
                    List<Message> joinParts = ZLinkActorSpotRoutePackets.createJoinRequestParts(
                        context.actorRef.actorId(),
                        actorTypes.getOrDefault(context.actorRef.actorId(), ""),
                        context.actorRef,
                        sourceEntrySpotRid.get(),
                        requestPart);
                    try {
                        return routedTransport.requestToSpotViaEgressChannel(
                            address.routerChannelId(),
                            address.targetNodeRid(),
                            address.spotRid(),
                            joinParts,
                            timeout);
                    } finally {
                        joinParts.forEach(Message::close);
                    }
                })
                .thenApply(replyParts -> {
                    Message emptyReply = null;
                    try {
                        Message first = replyParts.isEmpty()
                            ? (emptyReply = Message.from(new byte[0]))
                            : replyParts.get(0);
                        ZLinkActorSpotRoutePackets.JoinReply reply =
                            ZLinkActorSpotRoutePackets.decodeJoinReply(first);
                        return new ZLinkBackendActorJoinResult(
                            ZLinkBackendRequestResult.OK,
                            reply.accepted() ? 0 : 1,
                            reply.actorRef(),
                            spotRid,
                            reply.actorRef().epoch(),
                            0,
                            List.of(reply.reply()));
                    } finally {
                        if (emptyReply != null) {
                            emptyReply.close();
                        }
                        replyParts.forEach(Message::close);
                    }
                });
        }

        private CompletionStage<RoutingId> resolveRemoteTargetNode(RoutingId spotRid) {
            if (remoteAddressResolver == null) {
                return CompletableFuture.completedFuture(context.actorRef.nodeRid());
            }
            return remoteAddressResolver.resolveSpotRemoteAddressAsync(spotRid)
                .thenApply(address -> {
                    if (address == null || address.targetNodeRid() == null) {
                        throw new ZLinkConfigurationException(
                            "SPOT remote address resolver returned no target node: " + spotRid);
                    }
                    return address.targetNodeRid();
                });
        }
    }
}
