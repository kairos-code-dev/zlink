package systems.zlink.framework.runtime.actors;

import systems.zlink.framework.runtime.backend.*;

import java.time.Duration;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
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
import systems.zlink.framework.handlers.ZLinkPacket;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerFactory;
import systems.zlink.framework.spots.ZLinkSpot;

public final class ZLinkActorRuntime implements ZLinkActorManager {
    private final ZLinkBackendSpotNode spotNode;
    private final Map<String, Class<? extends ZLinkActorFactory>> factories;
    private final Duration defaultTimeout;
    private final ZLinkMessageSerializer serializer;
    private final ZLinkHandlerFactory handlerFactory;
    private final Map<String, ZLinkActor> actors = new HashMap<>();
    private final Map<String, String> actorTypes = new HashMap<>();
    private final Map<ZLinkActor, DefaultActorContext> contextsByActor = new HashMap<>();
    private final Map<String, systems.zlink.framework.execution.ZLinkAsyncSerialQueue> dispatchQueues =
        new HashMap<>();
    private Function<ZLinkActor, CompletionStage<Void>> disconnectedNotifier =
        ignored -> CompletableFuture.completedFuture(null);
    private Function<RoutingId, ZLinkSpot> spotResolver = ignored -> null;

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
    }

    @Override
    public CompletionStage<ZLinkActor> createAsync(String actorId, String actorType) {
        requireActorId(actorId);
        Class<? extends ZLinkActorFactory> factoryType = requireFactory(actorType);
        if (actors.containsKey(actorId)) {
            throw new ZLinkConfigurationException("duplicate actor id: " + actorId);
        }
        ZLinkBackendActorRef actorRef = spotNode.createActor(actorId);
        DefaultActorContext context = new DefaultActorContext(actorRef);
        return createFactory(factoryType)
            .createAsync(actorId, context)
            .thenApply(actor -> {
                actors.put(actorId, actor);
                actorTypes.put(actorId, actorType);
                contextsByActor.put(actor, context);
                return actor;
            });
    }

    @Override
    public CompletionStage<Optional<ZLinkActor>> findAsync(String actorId) {
        requireActorId(actorId);
        if (!actors.containsKey(actorId)) {
            spotNode.actorLookup(actorId);
        }
        return CompletableFuture.completedFuture(Optional.ofNullable(actors.get(actorId)));
    }

    @Override
    public CompletionStage<ZLinkActor> getOrCreateAsync(String actorId, String actorType) {
        requireActorId(actorId);
        ZLinkActor actor = actors.get(actorId);
        if (actor != null) {
            return CompletableFuture.completedFuture(actor);
        }
        return createAsync(actorId, actorType);
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
        return getOrCreateAsync(request.actorId(), request.actorType())
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
        return createAsync(actorId, actorType)
            .thenApply(actor -> expectedActorType.isInstance(actor)
                ? Optional.of(actor)
                : Optional.empty());
    }

    public CompletionStage<Void> submitActorDispatch(
        String actorId,
        Supplier<CompletionStage<Void>> operation) {
        systems.zlink.framework.execution.ZLinkAsyncSerialQueue queue =
            dispatchQueues.computeIfAbsent(actorId, ignored ->
                new systems.zlink.framework.execution.ZLinkAsyncSerialQueue());
        return queue.enqueue(operation);
    }

    public void setDisconnectedNotifier(
        Function<ZLinkActor, CompletionStage<Void>> disconnectedNotifier) {
        this.disconnectedNotifier = disconnectedNotifier == null
            ? ignored -> CompletableFuture.completedFuture(null)
            : disconnectedNotifier;
    }

    public void setSpotResolver(Function<RoutingId, ZLinkSpot> spotResolver) {
        this.spotResolver = spotResolver == null ? ignored -> null : spotResolver;
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
        ZLinkSpot spot) {
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
        ZLinkNativeBoundSessionRuntime boundSession = new ZLinkNativeBoundSessionRuntime(
            spotNode,
            actorRef,
            serializer,
            this,
            actor,
            defaultTimeout);
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

    private final class DefaultActorContext implements ZLinkActorContext {
        private ZLinkBackendActorRef actorRef;
        private ZLinkBoundSession boundSession;
        private long sessionBindingToken;
        private RoutingId spotRid;
        private ZLinkSpot spot;
        private boolean joined;

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
        public ZLinkSpot getSpot() {
            if (spot == null) {
                throw new ZLinkConfigurationException("actor has not joined a user Spot");
            }
            return spot;
        }

        @Override
        public <TSpot extends ZLinkSpot> TSpot getSpot(Class<TSpot> spotType) {
            if (spotType == null) {
                throw new ZLinkConfigurationException("spotType is required");
            }
            ZLinkSpot current = getSpot();
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
        public ZLinkActorJoinEntrySpotCall joinEntrySpot(RoutingId spotNodeRid) {
            if (spotNodeRid == null) {
                throw new ZLinkConfigurationException("spotNodeRid is required");
            }
            return new JoinEntrySpotCall(this, spotNodeRid, defaultTimeout);
        }

        @Override
        public ZLinkActorJoinSpotCall joinSpot(RoutingId spotRid, Object request) {
            if (spotRid == null) {
                throw new ZLinkConfigurationException("spotRid is required");
            }
            if (request == null) {
                throw new ZLinkConfigurationException("request is required");
            }
            return new JoinSpotCall(this, spotRid, request, defaultTimeout);
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
    }

    private final class JoinEntrySpotCall implements ZLinkActorJoinEntrySpotCall {
        private final DefaultActorContext context;
        private final RoutingId spotNodeRid;
        private final Duration timeout;

        JoinEntrySpotCall(
            DefaultActorContext context,
            RoutingId spotNodeRid,
            Duration timeout) {
            this.context = context;
            this.spotNodeRid = spotNodeRid;
            this.timeout = timeout;
        }

        @Override
        public ZLinkActorJoinEntrySpotCall timeout(Duration timeout) {
            if (timeout == null || timeout.isNegative() || timeout.isZero()) {
                throw new ZLinkConfigurationException("timeout must be positive");
            }
            return new JoinEntrySpotCall(context, spotNodeRid, timeout);
        }

        @Override
        public CompletionStage<ZLinkActorRef> submitAsync() {
            return spotNode.joinActorEntrySpot(
                    context.actorRef,
                    spotNodeRid,
                    timeout)
                .thenApply(result -> {
                    if (result.result() != ZLinkBackendRequestResult.OK) {
                        throw new ZLinkConfigurationException(
                            "actor entry spot join failed: " + result.result());
                    }
                    context.actorRef = result.actor();
                    context.spotRid = result.targetNodeRid();
                    context.spot = null;
                    context.joined = true;
                    return new ZLinkActorRef(
                        result.actor().nodeRid(),
                        result.actor().actorId(),
                        result.actor().epoch());
                });
        }
    }

    private final class JoinSpotCall implements ZLinkActorJoinSpotCall {
        private final DefaultActorContext context;
        private final RoutingId spotRid;
        private final Object request;
        private final Duration timeout;

        JoinSpotCall(
            DefaultActorContext context,
            RoutingId spotRid,
            Object request,
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
        public <TReply> CompletionStage<ZLinkActorJoinResult<TReply>> submitAsync(
            Class<TReply> replyType) {
            if (replyType == null) {
                throw new ZLinkConfigurationException("replyType is required");
            }
            Message requestPart = serializer.serialize(request);
            Message packetNamePart = Message.from(packetNameFor(request.getClass()));
            try {
                return spotNode.joinActor(
                        context.actorRef,
                        context.actorRef.nodeRid(),
                        spotRid,
                        List.of(packetNamePart, requestPart),
                        timeout)
                    .thenApply(result -> {
                        if (result.result() != ZLinkBackendRequestResult.OK) {
                            throw new ZLinkConfigurationException(
                                "actor spot join failed: " + result.result());
                        }
                        Message emptyReply = null;
                        try {
                            context.actorRef = result.actor();
                            context.spotRid = result.joinedSpotRid();
                            context.spot = spotResolver.apply(result.joinedSpotRid());
                            context.joined = true;
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
                packetNamePart.close();
                requestPart.close();
            }
        }
    }

    private static String packetNameFor(Class<?> messageType) {
        ZLinkPacket packet = messageType.getAnnotation(ZLinkPacket.class);
        return packet == null ? messageType.getSimpleName() : packet.value();
    }
}
