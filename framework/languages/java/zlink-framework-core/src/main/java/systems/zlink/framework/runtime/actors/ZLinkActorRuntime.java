package systems.zlink.framework.runtime.actors;

import systems.zlink.framework.runtime.backend.*;

import java.time.Duration;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CompletionStage;
import java.util.function.Function;
import java.util.function.Supplier;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.errors.ConfigResult;
import systems.zlink.contracts.errors.ZlinkConfigException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.ZLinkAwait;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.actors.ActorRef;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorDirectory;
import systems.zlink.framework.actors.ZLinkActorFactory;
import systems.zlink.framework.actors.ZLinkActorJoinEntrySpotCall;
import systems.zlink.framework.actors.ZLinkActorJoinResult;
import systems.zlink.framework.actors.ZLinkActorJoinSpotCall;
import systems.zlink.framework.actors.ZLinkActorPlacement;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.actors.ZLinkActorTransferAdapter;
import systems.zlink.framework.actors.ZLinkBoundSession;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.errors.ZLinkFrameworkErrorKind;
import systems.zlink.framework.errors.ZLinkFrameworkException;
import systems.zlink.framework.execution.ZLinkFrameworkTurns;
import systems.zlink.framework.execution.ZLinkYieldTurn;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerFactory;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerStages;
import systems.zlink.framework.runtime.locations.ZLinkLocationLifecycle;
import systems.zlink.framework.runtime.messaging.ZLinkPayloadEncoding;
import systems.zlink.framework.locations.ZLinkLocationWriteIntent;
import systems.zlink.framework.runtime.channels.ZLinkChannelRuntime;
import systems.zlink.framework.runtime.locations.ZLinkStoreLocationResolvers;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.SpotRemoteRefResolver;
import systems.zlink.framework.streams.ZLinkStreamCodec;

public final class ZLinkActorRuntime implements ZLinkActorManager, ZLinkActorDirectory {
    private static final CancellationToken NONE_CANCELLATION = () -> false;
    @FunctionalInterface
    public interface CreatedNotifier {
        CompletionStage<Void> notify(
            RoutingId nodeRid,
            ZLinkActor actor,
            ZLinkMessage createRequest,
            Object createContext);
    }

    @FunctionalInterface
    public interface SourceActorLeaver {
        CompletionStage<Void> leave(ZLinkActor actor);
    }

    private final ZLinkBackendSpotNode spotNode;
    private final Map<String, Class<? extends ZLinkActorFactory>> factories;
    private final ZLinkActorTransferRegistry actorTransfers;
    private final Duration defaultRequestTimeout;
    private final Duration actorTransferForwardWindow;
    private final ZLinkMessageSerializer serializer;
    private final ZLinkHandlerFactory handlerFactory;
    private final ZLinkStreamCodec defaultStreamCodec;
    private final Map<String, ZLinkActor> actors = new HashMap<>();
    private final Map<String, String> actorTypes = new HashMap<>();
    private final Map<ZLinkActor, DefaultActorContext> contextsByActor = new HashMap<>();
    private final Set<String> pendingTransferredActors = ConcurrentHashMap.newKeySet();
    private final Set<String> routedTransferredActors = ConcurrentHashMap.newKeySet();
    private final ZLinkActorDispatchSerials dispatches = new ZLinkActorDispatchSerials();
    private final ZLinkActorTransferHandoff handoff = new ZLinkActorTransferHandoff();
    private CreatedNotifier createdNotifier =
        (ignoredNode, ignoredActor, ignoredRequest, ignoredContext) -> CompletableFuture.completedFuture(null);
    private Supplier<Object> actorCreateContextSupplier = () -> null;
    private Function<ZLinkActor, CompletionStage<Void>> disconnectedNotifier =
        ignored -> CompletableFuture.completedFuture(null);
    private SourceActorLeaver sourceActorLeaver =
        ignored -> CompletableFuture.completedFuture(null);
    private Function<RoutingId, ZLinkSpot<?>> spotResolver = ignored -> null;
    private SpotRemoteRefResolver remoteAddressResolver;
    private final ZLinkActorLocationCoordinator locations =
        new ZLinkActorLocationCoordinator(actorTypes::get);
    private ZLinkChannelRuntime routedTransport;
    private Supplier<RoutingId> sourceEntrySpotRid = () -> RoutingId.from(new byte[0]);
    // Shared flow tracer (installed by the host); null = no tracing wired.
    private systems.zlink.framework.runtime.diagnostics.ZLinkMessageFlowTracer flow;

    public void setMessageFlowTracer(
        systems.zlink.framework.runtime.diagnostics.ZLinkMessageFlowTracer flow) {
        this.flow = flow;
    }

    public ZLinkActorRuntime(
        ZLinkBackendSpotNode spotNode,
        Map<String, Class<? extends ZLinkActorFactory>> factories,
        Duration defaultRequestTimeout,
        ZLinkMessageSerializer serializer) {
        this(
            spotNode,
            factories,
            Map.of(),
            defaultRequestTimeout,
            Duration.ofSeconds(5),
            serializer,
            ZLinkHandlerFactory.reflection(),
            ZLinkStreamCodec.JSON);
    }

    public ZLinkActorRuntime(
        ZLinkBackendSpotNode spotNode,
        Map<String, Class<? extends ZLinkActorFactory>> factories,
        Duration defaultRequestTimeout,
        ZLinkMessageSerializer serializer,
        ZLinkHandlerFactory handlerFactory) {
        this(
            spotNode,
            factories,
            Map.of(),
            defaultRequestTimeout,
            Duration.ofSeconds(5),
            serializer,
            handlerFactory,
            ZLinkStreamCodec.JSON);
    }

    public ZLinkActorRuntime(
        ZLinkBackendSpotNode spotNode,
        Map<String, Class<? extends ZLinkActorFactory>> factories,
        Map<String, Class<? extends ZLinkActorTransferAdapter<?>>> transferAdapters,
        Duration defaultRequestTimeout,
        ZLinkMessageSerializer serializer,
        ZLinkHandlerFactory handlerFactory,
        ZLinkStreamCodec defaultStreamCodec) {
        this(
            spotNode,
            factories,
            transferAdapters,
            defaultRequestTimeout,
            Duration.ofSeconds(5),
            serializer,
            handlerFactory,
            defaultStreamCodec);
    }

    public ZLinkActorRuntime(
        ZLinkBackendSpotNode spotNode,
        Map<String, Class<? extends ZLinkActorFactory>> factories,
        Map<String, Class<? extends ZLinkActorTransferAdapter<?>>> transferAdapters,
        Duration defaultRequestTimeout,
        Duration actorTransferForwardWindow,
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
        this.defaultRequestTimeout = defaultRequestTimeout;
        this.actorTransferForwardWindow = actorTransferForwardWindow == null
            ? Duration.ofSeconds(5)
            : actorTransferForwardWindow;
        this.serializer = serializer;
        this.handlerFactory = handlerFactory;
        this.actorTransfers = new ZLinkActorTransferRegistry(
            transferAdapters,
            handlerFactory);
        this.defaultStreamCodec =
            defaultStreamCodec == null ? ZLinkStreamCodec.JSON : defaultStreamCodec;
    }

    @Override
    public CompletionStage<ActorRef> create(String actorId, String actorType) {
        return create(actorId, actorType, ZLinkMessage.empty());
    }

    @Override
    public CompletionStage<ActorRef> create(
        String actorId,
        String actorType,
        ZLinkMessage createRequest) {
        return createLocalActor(actorId, actorType, createRequest, true)
            .thenApply(this::publicRefFor);
    }

    private CompletionStage<ZLinkActor> createLocalActor(
        String actorId,
        String actorType,
        ZLinkMessage createRequest,
        boolean failIfExists) {
        return createLocalActor(actorId, actorType, createRequest, failIfExists, true);
    }

    private CompletionStage<ZLinkActor> createLocalActor(
        String actorId,
        String actorType,
        ZLinkMessage createRequest,
        boolean failIfExists,
        boolean notifyCreated) {
        return createLocalActor(
            actorId,
            actorType,
            createRequest,
            failIfExists,
            notifyCreated,
            ZLinkLocationWriteIntent.NEW_CLAIM);
    }

    private CompletionStage<ZLinkActor> createLocalActor(
        String actorId,
        String actorType,
        ZLinkMessage createRequest,
        boolean failIfExists,
        boolean notifyCreated,
        ZLinkLocationWriteIntent intent) {
        requireActorId(actorId);
        if (createRequest == null) {
            throw new ZLinkConfigurationException("createRequest is required");
        }
        Class<? extends ZLinkActorFactory> factoryType = requireFactory(actorType);
        ZLinkActor existing = actors.get(actorId);
        if (existing != null) {
            if (failIfExists) {
                throw new ZLinkConfigurationException("duplicate actor id: " + actorId);
            }
            return CompletableFuture.completedFuture(existing);
        }
        Object createContext = actorCreateContextSupplier.get();
        if (locations.claimsActors(intent)) {
            return locations
                .claimActor(
                    actorType,
                    actorId,
                    spotNode.routingId(),
                    intent,
                    () -> deactivateActorOnOwnershipLoss(actorId))
                .thenCompose(ignored -> activateLocalActor(
                        actorId,
                        actorType,
                        createRequest,
                        factoryType,
                        notifyCreated,
                        createContext)
                    .thenCompose(actor -> locations.setActorRef(
                            actorType,
                            actorId,
                            new ActorRef(
                                refFor(actor).nodeRid(),
                                refFor(actor).actorId(),
                                refFor(actor).generation()))
                        .thenApply(ignoredSet -> actor))
                    .whenComplete((actor, error) -> {
                        if (error != null) {
                            locations.releaseActor(actorType, actorId);
                        }
                    }));
        }
        return activateLocalActor(
            actorId,
            actorType,
            createRequest,
            factoryType,
            notifyCreated,
            createContext);
    }

    private CompletionStage<ZLinkActor> activateLocalActor(
        String actorId,
        String actorType,
        ZLinkMessage createRequest,
        Class<? extends ZLinkActorFactory> factoryType,
        boolean notifyCreated,
        Object createContext) {
        Message nativeCreateRequest = messageFromRequest(createRequest);
        ZLinkBackendActorRef actorRef;
        try {
            actorRef = spotNode.createActor(actorId, nativeCreateRequest);
        } catch (RuntimeException ex) {
            nativeCreateRequest.close();
            throw ex;
        }
        if (actors.containsKey(actorId)) {
            throw new ZLinkConfigurationException("duplicate actor id: " + actorId);
        }
        DefaultActorContext context = new DefaultActorContext(actorRef);
        return ZLinkHandlerStages
            .fromSupplier(() -> createFactory(factoryType).create(actorId, context))
            .thenApply(actor -> {
                context.setActor(actor);
                actors.put(actorId, actor);
                actorTypes.put(actorId, actorType);
                contextsByActor.put(actor, context);
                return actor;
            })
            .thenCompose(actor -> notifyCreated
                ? submitActorDispatch(
                    actor.actorId(),
                    () -> createdNotifier.notify(actorRef.nodeRid(), actor, createRequest, createContext))
                    .thenApply(ignored -> actor)
                : CompletableFuture.completedFuture(actor));
    }

    public CompletionStage<ZLinkActor> materializeTransferredActor(
        String actorId,
        String actorType,
        String adapterKey,
        ZLinkMessage transferState) {
        requireActorId(actorId);
        Class<? extends ZLinkActorFactory> factoryType = requireFactory(actorType);
        detachForwardingProxyForReentry(actorId);
        if (actors.containsKey(actorId)) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "target runtime already owns actor: " + actorId));
        }
        if (adapterKey != null && !adapterKey.equals(actorType)) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "actor transfer adapter key does not match actor type: " + adapterKey));
        }
        return activateTransferredActor(
                actorId,
                actorType,
                adapterKey,
                transferState,
                factoryType)
            .thenApply(actor -> {
                pendingTransferredActors.add(actorId);
                routedTransferredActors.add(actorId);
                return actor;
            });
    }

    private synchronized void detachForwardingProxyForReentry(String actorId) {
        ZLinkActorTransferHandoff.ForwardingSource forwarding =
            handoff.forwardingSource(actorId).orElse(null);
        ZLinkActor oldActor = actors.get(actorId);
        if (forwarding == null || oldActor == null) {
            return;
        }
        DefaultActorContext oldContext = contextsByActor.get(oldActor);
        if (oldContext == null || !forwarding.targetActorRef().equals(oldContext.actorRef())) {
            return;
        }
        actors.remove(actorId, oldActor);
        actorTypes.remove(actorId);
        contextsByActor.remove(oldActor);
        dispatches.remove(actorId);
    }

    public CompletionStage<Void> rollbackTransferredActor(ZLinkActor actor) {
        DefaultActorContext context = requireContext(actor);
        ZLinkBackendActorRef actorRef = context.actorRef();
        String actorType = actorTypes.getOrDefault(actor.actorId(), "");
        context.markLeft();
        return spotNode.destroyActor(actorRef, defaultRequestTimeout)
            .exceptionally(error -> null)
            .thenCompose(ignored -> locations.releaseActor(actorType, actor.actorId()))
            .thenRun(() -> {
                removeActorSessionRouteForContext(context);
                context.clearAfterDestroy();
                actors.remove(actor.actorId(), actor);
                pendingTransferredActors.remove(actor.actorId());
                routedTransferredActors.remove(actor.actorId());
                actorTypes.remove(actor.actorId());
                contextsByActor.remove(actor);
                dispatches.remove(actor.actorId());
            });
    }

    private CompletionStage<ZLinkActor> activateTransferredActor(
        String actorId,
        String actorType,
        String adapterKey,
        ZLinkMessage transferState,
        Class<? extends ZLinkActorFactory> factoryType) {
        Message nativeCreateRequest = Message.from(new byte[0]);
        ZLinkBackendActorRef actorRef;
        try {
            actorRef = spotNode.createActor(actorId, nativeCreateRequest);
        } catch (RuntimeException error) {
            nativeCreateRequest.close();
            throw error;
        }
        DefaultActorContext context = new DefaultActorContext(actorRef);
        context.beginMove();
        return ZLinkHandlerStages.fromSupplier(() -> {
                ZLinkActor actor = adapterKey == null
                    ? createFactory(factoryType).create(actorId, context)
                    : actorTransfers.transferIn(
                        actorType,
                        actorId,
                        context,
                        transferState,
                        NONE_CANCELLATION);
                if (actor == null) {
                    throw new ZLinkConfigurationException(
                        "actor transfer did not materialize an actor: " + actorId);
                }
                return actor;
            })
            .thenApply(actor -> {
                context.setActor(actor);
                actors.put(actorId, actor);
                actorTypes.put(actorId, actorType);
                contextsByActor.put(actor, context);
                return actor;
            });
    }

    ZLinkActorTransferRegistry.TransferState transferOut(
        ZLinkActor actor,
        CancellationToken cancellationToken) {
        String actorType = actorTypes.get(actor.actorId());
        if (actorType == null) {
            throw new ZLinkConfigurationException(
                "actor type is not registered for transfer: " + actor.actorId());
        }
        return actorTransfers.transferOut(actorType, actor, cancellationToken);
    }

    CompletionStage<Void> beginRemoteMove(ZLinkActor actor) {
        DefaultActorContext context = requireContext(actor);
        context.beginMove();
        handoff.begin(actor.actorId());
        return CompletableFuture.completedFuture(null);
    }

    public CompletionStage<Optional<Message>> captureMovingPacket(
        ZLinkActor actor,
        systems.zlink.framework.runtime.streams.ZLinkStreamHeader header,
        Message payload) {
        return captureMovingPacket(actor, header, payload, null);
    }

    public CompletionStage<Optional<Message>> captureMovingPacket(
        ZLinkActor actor,
        systems.zlink.framework.runtime.streams.ZLinkStreamHeader header,
        Message payload,
        ZLinkActorReplyRoute replyRoute) {
        ZLinkActorHandoffPacket packet =
            handoff.capture(actor.actorId(), header, payload, replyRoute);
        if (packet == null) {
            return null;
        }
        traceActorTransferMarker("handoff_backlog", actor.actorId(),
            Long.toString(packet.arrivalIndex()));
        if (replyRoute != null) {
            traceActorTransferMarker(
                "handoff_request_frame",
                actor.actorId(),
                Long.toUnsignedString(replyRoute.requestId())
                    + ":" + Integer.toUnsignedString(replyRoute.flags()));
        }
        return packet.reply();
    }

    List<ZLinkActorHandoffPacket> takeRemoteMoveBacklog(ZLinkActor actor) {
        return handoff.take(actor.actorId());
    }

    List<ZLinkActorHandoffPacket> finishRemoteMoveBacklog(ZLinkActor actor) {
        return handoff.finish(actor.actorId());
    }

    public CompletionStage<Void> leaveSourceForLocalMove(ZLinkActor actor) {
        DefaultActorContext context = requireContext(actor);
        context.beginMove();
        return sourceActorLeaver.leave(actor)
            .thenRun(context::markLeft);
    }

    CompletionStage<Void> leaveSourceForRemoteMove(ZLinkActor actor) {
        DefaultActorContext context = requireContext(actor);
        RoutingId currentSpotRid = context.joinedSpotRid();
        return sourceActorLeaver.leave(actor)
            .thenCompose(ignored -> currentSpotRid == null
                || routedTransferredActors.contains(actor.actorId())
                ? CompletableFuture.completedFuture(null)
                : spotNode.leaveActor(
                        context.actorRef(),
                        currentSpotRid,
                        defaultRequestTimeout)
                    .thenAccept(parts -> parts.forEach(Message::close)))
            .thenRun(context::markLeft);
    }

    void cancelRemoteMove(ZLinkActor actor) {
        failTransferBacklog(actor, new ZLinkConfigurationException("actor transfer was cancelled"));
        requireContext(actor).endMove();
    }

    public void completeRemoteMove(ZLinkActor actor) {
        requireContext(actor).endMove();
    }

    void retainForwardingSource(
        ZLinkActor actor,
        ZLinkBackendActorRef sourceActorRef,
        ZLinkBackendActorRef targetActorRef) {
        handoff.retain(
            actor.actorId(), sourceActorRef, targetActorRef, actorTransferForwardWindow,
            source -> retireForwardingSource(actor, source));
    }

    int forwardingSourceCount() {
        return handoff.forwardingSourceCount();
    }

    private void retireForwardingSource(
        ZLinkActor actor,
        ZLinkActorTransferHandoff.ForwardingSource source) {
        traceActorTransferMarker("mapping_evicted", actor.actorId(), null);
        retryForwardingSourceCleanup(actor, source);
    }

    private void retryForwardingSourceCleanup(
        ZLinkActor actor,
        ZLinkActorTransferHandoff.ForwardingSource source) {
        synchronized (this) {
            DefaultActorContext context = contextsByActor.get(actor);
            if (context == null || !source.targetActorRef().equals(context.actorRef())) {
                return;
            }
        }
        spotNode.destroyActor(source.sourceActorRef(), defaultRequestTimeout)
            .whenComplete((ignored, error) -> {
                if (error != null && !ZLinkActorSubmitFaults.requestNotFound(error)) {
                    ZLinkActorRetryScheduler.scheduleRoute(
                        () -> retryForwardingSourceCleanup(actor, source));
                    return;
                }
                synchronized (this) {
                    DefaultActorContext context = contextsByActor.get(actor);
                    if (context == null || !source.targetActorRef().equals(context.actorRef())) {
                        return;
                    }
                    traceActorTransferMarker(
                        "source_cleanup",
                        actor.actorId(),
                        Long.toUnsignedString(source.sourceActorRef().generation()));
                    actors.remove(actor.actorId(), actor);
                    routedTransferredActors.remove(actor.actorId());
                    actorTypes.remove(actor.actorId());
                    contextsByActor.remove(actor);
                    dispatches.remove(actor.actorId());
                }
            });
    }

    public void traceActorTransferMarker(String marker, String actorId, String correlationId) {
        if (flow == null
            || !flow.enabled(systems.zlink.framework.configuration.ZLinkMessageFlowOutcome.DISPATCHED)) {
            return;
        }
        flow.trace(new systems.zlink.framework.configuration.ZLinkMessageFlowEvent(
            systems.zlink.framework.configuration.ZLinkMessageFlowOutcome.DISPATCHED,
            systems.zlink.framework.configuration.ZLinkDispatchErrorSurface.SPOT_ACTOR,
            systems.zlink.framework.configuration.ZLinkDispatchMessageKind.ACTOR_SEND,
            marker,
            null,
            null,
            correlationId,
            null,
            null,
            actorId,
            null));
    }

    public void failRemoteMove(ZLinkActor actor, Throwable error) {
        failTransferBacklog(actor, error);
        requireContext(actor).failMove(error);
    }

    private void failTransferBacklog(ZLinkActor actor, Throwable error) {
        handoff.fail(actor.actorId(), error);
    }

    public boolean isMoving(ZLinkActor actor) {
        return requireContext(actor).moving();
    }

    public CompletionStage<Void> awaitMoveCompletion(ZLinkActor actor) {
        return requireContext(actor).moveCompletion();
    }

    public CompletionStage<Void> commitJoinedLocation(
        ZLinkActor actor,
        RoutingId spotRid) {
        if (!pendingTransferredActors.remove(actor.actorId())) {
            return renewActorJoinedLocation(actor, spotRid);
        }
        String actorType = actorTypes.get(actor.actorId());
        return locations.claimActor(
                actorType,
                actor.actorId(),
                spotNode.routingId(),
                ZLinkLocationWriteIntent.TAKEOVER,
                () -> deactivateActorOnOwnershipLoss(actor.actorId()))
            .thenCompose(ignored -> locations.setActorRef(
                actorType,
                actor.actorId(),
                publicRefFor(actor)))
            .thenCompose(ignored -> renewActorJoinedLocation(actor, spotRid));
    }

    private DefaultActorContext requireContext(ZLinkActor actor) {
        DefaultActorContext context = contextsByActor.get(actor);
        if (context == null) {
            throw new ZLinkConfigurationException(
                "actor is not managed by this runtime: " + actor.actorId());
        }
        return context;
    }

    static ActorRef toPublicActorRef(ZLinkBackendActorRef actorRef) {
        return new ActorRef(actorRef.nodeRid(), actorRef.actorId(), actorRef.generation());
    }

    private CompletionStage<Void> renewActorJoinedLocation(
        ZLinkActor actor,
        RoutingId spotRid) {
        return locations.actorJoinedSpot(actor, spotRid);
    }

    private CompletionStage<Void> renewActorLeftLocation(ZLinkActor actor) {
        return locations.actorLeftSpot(actor);
    }

    private CompletionStage<Void> renewActorMovedToEntrySpotLocation(
        ZLinkActor actor,
        RoutingId nodeRid) {
        return locations.actorMovedToEntrySpot(actor, nodeRid);
    }

    private void removeActorSessionRouteForContext(DefaultActorContext context) {
        locations.removeSessionRoute(context.boundSessionSourceSessionRid());
    }

    private void deactivateActorOnOwnershipLoss(String actorId) {
        ZLinkActor actor;
        DefaultActorContext context;
        String actorType;
        synchronized (this) {
            actor = actors.remove(actorId);
            context = actor == null ? null : contextsByActor.remove(actor);
            actorType = actorTypes.remove(actorId);
            dispatches.remove(actorId);
        }
        if (actor == null || context == null || context.actorRef() == null) {
            return;
        }
        try {
            context.disconnectBoundSessionForDestroy().toCompletableFuture().join();
        } catch (RuntimeException ignored) {
        }
        try {
            spotNode.destroyActor(context.actorRef(), defaultRequestTimeout).toCompletableFuture().join();
        } catch (RuntimeException ignored) {
        }
        removeActorSessionRouteForContext(context);
        locations.releaseActor(actorType, actorId);
    }

    @Override
    public CompletionStage<Optional<ActorRef>> find(String actorId) {
        requireActorId(actorId);
        ZLinkActor local = actors.get(actorId);
        if (local != null && !pendingTransferredActors.contains(actorId)) {
            return CompletableFuture.completedFuture(Optional.of(publicRefFor(local)));
        }
        if (pendingTransferredActors.contains(actorId)) {
            return locations.findStoredActorRef(actorId);
        }
        try {
            return CompletableFuture.completedFuture(Optional.of(toPublicActorRef(spotNode.actorLookup(actorId))));
        } catch (ZlinkConfigException ex) {
            if (ex.getResult() != ConfigResult.NOT_FOUND) {
                throw ex;
            }
        }
        return locations.findStoredActorRef(actorId);
    }

    @Override
    public CompletionStage<ActorRef> ensure(
        String actorId,
        ZLinkMessage createRequest,
        ZLinkActorPlacement placement) {
        requireActorId(actorId);
        if (createRequest == null) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "createRequest is required"));
        }
        CompletionStage<Optional<ActorRef>> existing = find(actorId);
        ZLinkActorPlacement safePlacement =
            placement == null ? ZLinkActorPlacement.any() : placement;
        return existing.thenCompose(found -> {
            if (found.isPresent()) {
                return CompletableFuture.completedFuture(found.get());
            }
            if (safePlacement.preferredNodeRid() != null
                && !safePlacement.preferredNodeRid().equals(spotNode.routingId())) {
                return CompletableFuture.failedFuture(new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ROUTE_NOT_CONNECTED,
                    "preferred actor node is not connected to this actor directory"));
            }
            return createLocalActor(actorId, resolveSingleActorType(), createRequest, false)
                .thenApply(this::publicRefFor)
                .exceptionallyCompose(error -> {
                    Throwable cause = unwrap(error);
                    if (cause instanceof ZLinkFrameworkException frameworkError
                        && frameworkError.kind() == ZLinkFrameworkErrorKind.ACTOR_CREATE_FAILED) {
                        return find(actorId).thenCompose(raced -> raced
                            .<CompletionStage<ActorRef>>map(CompletableFuture::completedFuture)
                            .orElseGet(() -> CompletableFuture.failedFuture(
                                new ZLinkFrameworkException(
                                    ZLinkFrameworkErrorKind.ACTOR_CREATE_REJECTED,
                                    frameworkError.getMessage(),
                                    frameworkError))));
                    }
                    return CompletableFuture.failedFuture(cause);
                });
        });
    }

    private static Throwable unwrap(Throwable error) {
        if (error instanceof CompletionException && error.getCause() != null) {
            return error.getCause();
        }
        return error;
    }

    private String resolveSingleActorType() {
        if (factories.size() == 1) {
            return factories.keySet().iterator().next();
        }
        String message = factories.isEmpty()
            ? "actor directory requires one registered actor factory"
            : "actor directory cannot choose an actor type when multiple actor factories are registered";
        throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.ACTOR_CREATE_FAILED,
            message);
    }

    @Override
    public CompletionStage<ActorRef> getOrCreate(String actorId, String actorType) {
        return getOrCreate(actorId, actorType, ZLinkMessage.empty());
    }

    @Override
    public CompletionStage<ActorRef> getOrCreate(
        String actorId,
        String actorType,
        ZLinkMessage createRequest) {
        requireActorId(actorId);
        ZLinkActor actor = actors.get(actorId);
        if (actor != null) {
            return CompletableFuture.completedFuture(publicRefFor(actor));
        }
        return createLocalActor(actorId, actorType, createRequest, false)
            .thenApply(this::publicRefFor);
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

    private Message messageFromRequest(ZLinkMessage request) {
        if (request == null) {
            throw new ZLinkConfigurationException("request is required");
        }
        return Message.from(request.toEncodedPayload(serializer).bytes());
    }

    ZLinkBackendActorRef refFor(ZLinkActor actor) {
        DefaultActorContext context = contextsByActor.get(actor);
        if (context == null) {
            throw new ZLinkConfigurationException(
                "actor is not managed by this runtime: " + actor.actorId());
        }
        return context.actorRef();
    }

    public ZLinkBackendActorRef currentRef(ZLinkActor actor) {
        return refFor(actor);
    }

    private ActorRef publicRefFor(ZLinkActor actor) {
        ZLinkBackendActorRef actorRef = refFor(actor);
        return new ActorRef(
            actorRef.nodeRid(),
            actorRef.actorId(),
            actorRef.generation());
    }

    private DefaultActorContext contextFor(ActorRef actor) {
        if (actor == null) {
            throw new ZLinkConfigurationException("actor is required");
        }
        ZLinkActor current = actors.get(actor.actorId());
        if (current == null) {
            throw new ZLinkConfigurationException(
                "actor is not managed by this runtime: " + actor.actorId());
        }
        DefaultActorContext context = contextsByActor.get(current);
        if (context == null || context.actorRef() == null) {
            throw new ZLinkConfigurationException(
                "actor does not have a native Actor ref: " + actor.actorId());
        }
        ZLinkBackendActorRef currentRef = context.actorRef();
        if (!currentRef.actorId().equals(actor.actorId())
            || !currentRef.nodeRid().equals(actor.nodeRid())
            || currentRef.generation() != actor.generation()) {
            throw new ZLinkConfigurationException(
                "actor ref is not current for this runtime: " + actor.actorId());
        }
        return context;
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
        return bindSession(actor, boundSession, null, null);
    }

    long bindSession(
        ZLinkActor actor,
        ZLinkBoundSession boundSession,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid) {
        DefaultActorContext context = contextsByActor.get(actor);
        if (context == null) {
            throw new ZLinkConfigurationException(
                "actor is not managed by this runtime: " + actor.actorId());
        }
        long bindingToken = context.bindSession(boundSession, sourceNodeRid, sourceSessionRid);
        locations.bindSessionRoute(sourceSessionRid, actor.actorId(), sourceNodeRid);
        return bindingToken;
    }

    public boolean clearSessionBinding(ZLinkActor actor, long bindingToken) {
        DefaultActorContext context = contextsByActor.get(actor);
        if (context == null) {
            throw new ZLinkConfigurationException(
                "actor is not managed by this runtime: " + actor.actorId());
        }
        RoutingId routeSessionRid = context.boundSessionSourceSessionRid(bindingToken);
        boolean cleared = context.clearBoundSession(bindingToken);
        if (cleared) {
            locations.removeSessionRoute(routeSessionRid);
        }
        return cleared;
    }

    public CompletionStage<Message> handleEntrySpotRouteJoin(
        RoutingId sourceRoutingId,
        Message payload) {
        ZLinkActorEntrySpotRoutePackets.JoinRequest request =
            ZLinkActorEntrySpotRoutePackets.decodeJoinRequest(payload);
        return createLocalActor(
                request.actorId(),
                request.actorType(),
                ZLinkMessage.empty(),
                false)
            .thenApply(actor -> {
                ZLinkBackendActorRef actorRef = refFor(actor);
                return ZLinkActorEntrySpotRoutePackets.encodeJoinReply(
                    actor.actorId(),
                    actorTypes.getOrDefault(actor.actorId(), request.actorType()),
                    actorRef.nodeRid(),
                    actorRef.generation());
            });
    }

    public Optional<ZLinkActor> localActor(String actorId) {
        return Optional.ofNullable(actors.get(actorId));
    }

    public CompletionStage<ZLinkActor> getOrCreateManagedActor(
        String actorId,
        String actorType) {
        return getOrCreateManagedActor(actorId, actorType, true);
    }

    public CompletionStage<ZLinkActor> getOrCreateManagedActorWithoutLocationClaim(
        String actorId,
        String actorType) {
        ZLinkActor existing = actors.get(actorId);
        if (existing != null) {
            return CompletableFuture.completedFuture(existing);
        }
        return createLocalActor(
            actorId,
            actorType,
            ZLinkMessage.empty(),
            false,
            false,
            null);
    }

    private CompletionStage<ZLinkActor> getOrCreateManagedActor(
        String actorId,
        String actorType,
        boolean notifyCreated) {
        ZLinkActor existing = actors.get(actorId);
        if (existing != null) {
            return CompletableFuture.completedFuture(existing);
        }
        return createLocalActor(actorId, actorType, ZLinkMessage.empty(), false, notifyCreated);
    }

    public boolean hasBoundSession(ZLinkActor actor) {
        DefaultActorContext context = contextsByActor.get(actor);
        if (context == null) {
            throw new ZLinkConfigurationException(
                "actor is not managed by this runtime: " + actor.actorId());
        }
        return context.hasBoundSession();
    }

    public boolean hasBoundSession(
        ZLinkActor actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid) {
        DefaultActorContext context = contextsByActor.get(actor);
        if (context == null) {
            throw new ZLinkConfigurationException(
                "actor is not managed by this runtime: " + actor.actorId());
        }
        return context.hasBoundSession(sourceNodeRid, sourceSessionRid);
    }

    public CompletionStage<Boolean> sendBoundSessionFrame(
        ZLinkActor actor,
        byte[] frameBytes) {
        DefaultActorContext context = contextsByActor.get(actor);
        if (context == null) {
            throw new ZLinkConfigurationException(
                "actor is not managed by this runtime: " + actor.actorId());
        }
        return context.sendBoundSessionFrame(frameBytes);
    }

    public Optional<RoutingId> spotRid(ZLinkActor actor) {
        DefaultActorContext context = contextsByActor.get(actor);
        if (context == null) {
            throw new ZLinkConfigurationException(
                "actor is not managed by this runtime: " + actor.actorId());
        }
        return Optional.ofNullable(context.joinedSpotRid());
    }

    public ZLinkBackendActorRef actorRef(ZLinkActor actor) {
        DefaultActorContext context = contextsByActor.get(actor);
        if (context == null) {
            throw new ZLinkConfigurationException(
                "actor is not managed by this runtime: " + actor.actorId());
        }
        return context.actorRef();
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
        systems.zlink.framework.runtime.streams.ZLinkStreamHeader header,
        Message payload) {
        if (!canRouteRemoteJoinedSpot(spotRid)) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "actor Spot is not routable: " + spotRid));
        }
        return remoteAddressResolver.resolveSpotRemoteRefAsync(spotRid)
            .thenCompose(address -> {
                List<Message> parts =
                    ZLinkActorSpotRoutePackets.createActorPacketParts(
                        actorRef, header, payload, null);
                try {
                    if (header.requestSequence().isPresent()
                        || header.kind() == systems.zlink.framework.streams.ZLinkStreamMessageKind.REQUEST) {
                        return routedTransport.requestToSpotViaRouterChannel(
                                address.routerChannelId(),
                                address.targetNodeRid(),
                                address.spotRid(),
                                parts,
                                defaultRequestTimeout)
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
                    return routedTransport.sendToSpotViaRouterChannel(
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
            if (spotRid.equals(context.joinedSpotRid())) {
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
        return createLocalActor(actorId, actorType, ZLinkMessage.empty(), false)
            .thenApply(actor -> expectedActorType.isInstance(actor)
                ? Optional.of(actor)
                : Optional.empty());
    }

    public CompletionStage<Void> submitActorDispatch(
        String actorId,
        Supplier<CompletionStage<Void>> operation) {
        if (dispatches.isCurrent(actorId)) {
            try {
                return operation.get();
            } catch (RuntimeException ex) {
                return CompletableFuture.failedFuture(ex);
            }
        }
        ZLinkActorDispatchSerials.QueuedTurn turn;
        synchronized (this) {
            if (!actors.containsKey(actorId)) {
                return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                    "actor is not managed by this runtime: " + actorId));
            }
            turn = dispatches.prepare(actorId);
        }
        return dispatches.enqueue(turn, operation);
    }

    public <T> CompletionStage<T> runActorDispatchTurn(
        String actorId,
        Supplier<CompletionStage<T>> operation) {
        return dispatches.runTurn(actorId, operation);
    }

    boolean isActorDispatchActive(ZLinkActor actor) {
        return dispatches.isActive(actor.actorId());
    }

    void continueAfterActorDispatch(
        ZLinkActor actor,
        Supplier<CompletionStage<Void>> operation) {
        ZLinkActorDispatchSerials.QueuedTurn turn;
        synchronized (this) {
            turn = dispatches.prepare(actor.actorId());
        }
        dispatches.enqueue(turn, operation)
            .whenComplete((ignored, error) -> {
                if (error != null) {
                    failRemoteMove(actor, error);
                }
            });
    }

    public void setDisconnectedNotifier(
        Function<ZLinkActor, CompletionStage<Void>> disconnectedNotifier) {
        this.disconnectedNotifier = disconnectedNotifier == null
            ? ignored -> CompletableFuture.completedFuture(null)
            : disconnectedNotifier;
    }

    public void setSourceActorLeaver(SourceActorLeaver sourceActorLeaver) {
        this.sourceActorLeaver = sourceActorLeaver == null
            ? ignored -> CompletableFuture.completedFuture(null)
            : sourceActorLeaver;
    }

    public void setCreatedNotifier(CreatedNotifier createdNotifier) {
        this.createdNotifier = createdNotifier == null
            ? (ignoredNode, ignoredActor, ignoredRequest, ignoredContext) -> CompletableFuture.completedFuture(null)
            : createdNotifier;
    }

    public void setActorCreateContextSupplier(Supplier<Object> actorCreateContextSupplier) {
        this.actorCreateContextSupplier = actorCreateContextSupplier == null
            ? () -> null
            : actorCreateContextSupplier;
    }

    public void setSpotResolver(Function<RoutingId, ZLinkSpot<?>> spotResolver) {
        this.spotResolver = spotResolver == null ? ignored -> null : spotResolver;
    }

    public void setSpotMeshResolver(Function<RoutingId, String> spotMeshResolver) {
        locations.setSpotMeshResolver(spotMeshResolver);
    }

    public void setRemoteAddressResolver(SpotRemoteRefResolver remoteAddressResolver) {
        this.remoteAddressResolver = remoteAddressResolver;
    }

    public void setLocationLifecycle(ZLinkLocationLifecycle lifecycle) {
        locations.setLifecycle(lifecycle);
    }

    public void setStoreLocationResolvers(ZLinkStoreLocationResolvers resolvers) {
        locations.setResolvers(resolvers);
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
        RoutingId spotRid,
        ZLinkSpot<?> spot) {
        DefaultActorContext context = contextsByActor.get(actor);
        if (context == null) {
            throw new ZLinkConfigurationException(
                "actor is not managed by this runtime: " + actor.actorId());
        }
        context.markJoined(actorRef, spotRid, spot);
    }

    public CompletionStage<Void> markJoinedAsync(
        ZLinkActor actor,
        ZLinkBackendActorRef actorRef,
        RoutingId spotRid,
        ZLinkSpot<?> spot) {
        markJoined(actor, actorRef, spotRid, spot);
        return renewActorJoinedLocation(actor, spotRid);
    }

    public long bindNativeSession(
        ZLinkActor actor,
        ZLinkBackendSpotNode spotNode,
        ZLinkBackendActorRef actorRef) {
        return bindNativeSession(actor, spotNode, actorRef, null, null);
    }

    public long bindNativeSession(
        ZLinkActor actor,
        ZLinkBackendSpotNode spotNode,
        ZLinkBackendActorRef actorRef,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid) {
        DefaultActorContext context = contextsByActor.get(actor);
        if (context == null) {
            throw new ZLinkConfigurationException(
                "actor is not managed by this runtime: " + actor.actorId());
        }
        context.markNativeActorRef(actorRef, sourceNodeRid, sourceSessionRid);
        if (sourceNodeRid != null
            && sourceSessionRid != null
            && !sourceNodeRid.equals(actorRef.nodeRid())) {
            spotNode.bindRemoteActorBoundSession(actorRef, sourceNodeRid, sourceSessionRid);
        }
        ZLinkNativeBoundSessionRuntime boundSession = new ZLinkNativeBoundSessionRuntime(
            spotNode,
            actorRef,
            serializer,
            this,
            actor,
            sourceNodeRid,
            sourceSessionRid,
            defaultRequestTimeout,
            defaultStreamCodec);
        long bindingToken = bindSession(actor, boundSession, sourceNodeRid, sourceSessionRid);
        boundSession.setBindingToken(bindingToken);
        return bindingToken;
    }

    public long bindRoutedSession(
        ZLinkActor actor,
        String routeChannelName,
        RoutingId targetRoutePeerRid,
        RoutingId targetEntrySpotRid,
        ZLinkBackendActorRef actorRef) {
        ZLinkRoutedBoundSessionRuntime boundSession = new ZLinkRoutedBoundSessionRuntime(
            spotNode.entrySpot(),
            routedTransport,
            routeChannelName,
            targetRoutePeerRid,
            targetEntrySpotRid,
            actorRef,
            serializer,
            this,
            actor,
            defaultRequestTimeout,
            defaultStreamCodec);
        long bindingToken = bindSession(actor, boundSession);
        boundSession.setBindingToken(bindingToken);
        return bindingToken;
    }

    public void markLeft(ZLinkActor actor) {
        DefaultActorContext context = contextsByActor.get(actor);
        if (context == null) {
            throw new ZLinkConfigurationException(
                "actor is not managed by this runtime: " + actor.actorId());
        }
        context.markLeft();
    }

    public CompletionStage<Void> markLeftAsync(ZLinkActor actor) {
        markLeft(actor);
        return renewActorLeftLocation(actor);
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

        String actorType = actorTypes.getOrDefault(actor.actorId(), "");
        return spotNode.destroyActor(actorRef, defaultRequestTimeout)
            .thenCompose(ignored -> context.disconnectBoundSessionForDestroy()
                .exceptionally(error -> null))
            .thenCompose(ignored -> locations.releaseActor(actorType, actor.actorId()))
            .thenRun(() -> {
                synchronized (this) {
                    removeActorSessionRouteForContext(context);
                    context.clearAfterDestroy();
                    actors.remove(actor.actorId(), actor);
                    actorTypes.remove(actor.actorId());
                    contextsByActor.remove(actor);
                    dispatches.remove(actor.actorId());
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
            ZLinkBackendActorRef actorRef = context.actorRef();
            if (actorRef == null) {
                continue;
            }

            try {
                context.disconnectBoundSessionForDestroy().toCompletableFuture().join();
            } catch (RuntimeException ignored) {
                // Runtime shutdown is best-effort; native context close will release remaining handles.
            }

            RoutingId joinedSpotRid = context.joinedSpotRid();
            if (joinedSpotRid != null) {
                try {
                    List<Message> replyParts = spotNode
                        .leaveActor(actorRef, joinedSpotRid, defaultRequestTimeout)
                        .toCompletableFuture()
                        .join();
                    replyParts.forEach(Message::close);
                } catch (RuntimeException ignored) {
                    // Shutdown still proceeds to actor destroy and map cleanup below.
                }
            }

            try {
                spotNode.destroyActor(actorRef, defaultRequestTimeout).toCompletableFuture().join();
            } catch (RuntimeException ignored) {
                // Actors outside the Entry Spot may reject destroy. Shutdown must still release maps.
            }

            try {
                locations.releaseActor(actorTypes.getOrDefault(actor.actorId(), ""), actor.actorId())
                    .toCompletableFuture()
                    .join();
            } catch (RuntimeException ignored) {
                // Location runtime stop will bulk-remove remaining rows owned by this process.
            }

            synchronized (this) {
                removeActorSessionRouteForContext(context);
                context.clearAfterDestroy();
                actors.remove(actor.actorId(), actor);
                pendingTransferredActors.remove(actor.actorId());
                routedTransferredActors.remove(actor.actorId());
                actorTypes.remove(actor.actorId());
                contextsByActor.remove(actor);
                dispatches.remove(actor.actorId());
            }
        }
    }

    final class DefaultActorContext implements ZLinkActorContext {
        private final ZLinkActorContextState state;

        DefaultActorContext(ZLinkBackendActorRef actorRef) {
            this.state = new ZLinkActorContextState(actorRef);
        }

        ZLinkActor actor() {
            return state.actor();
        }

        void setActor(ZLinkActor actor) {
            state.setActor(actor);
        }

        void beginMove() {
            state.beginMove();
        }

        void endMove() {
            state.endMove();
        }

        void failMove(Throwable error) {
            state.failMove(error);
        }

        boolean moving() {
            return state.moving();
        }

        CompletionStage<Void> moveCompletion() {
            return state.moveCompletion();
        }

        ZLinkBackendActorRef actorRef() {
            return state.actorRef();
        }

        RoutingId joinedSpotRid() {
            return state.spotRid();
        }

        RoutingId boundSessionSourceNodeRid() {
            return state.boundSessionSourceNodeRid();
        }

        RoutingId boundSessionSourceSessionRid() {
            return state.boundSessionSourceSessionRid();
        }

        void markJoined(
            ZLinkBackendActorRef actorRef,
            RoutingId spotRid,
            ZLinkSpot<?> spot) {
            state.markJoined(actorRef, spotRid, spot);
        }

        void markMovedToEntrySpot(
            ZLinkBackendActorRef actorRef,
            RoutingId entrySpotNodeRid) {
            state.markMovedToEntrySpot(actorRef, entrySpotNodeRid);
        }

        void markLeft() {
            state.markLeft();
        }

        void markNativeActorRef(
            ZLinkBackendActorRef actorRef,
            RoutingId sourceNodeRid,
            RoutingId sourceSessionRid) {
            state.markNativeActorRef(actorRef, sourceNodeRid, sourceSessionRid);
        }

        @Override
        public Optional<RoutingId> spotRid() {
            return Optional.ofNullable(state.spotRid());
        }

        @Override
        public boolean isJoined() {
            return state.joined();
        }

        @Override
        public ZLinkBoundSession boundSession() {
            return state.requireBoundSession();
        }

        @Override
        public ZLinkSpot<?> getSpot() {
            return state.requireSpot();
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
        public ZLinkActorJoinEntrySpotCall joinEntrySpot(RoutingId spotNodeRid) {
            if (spotNodeRid == null) {
                throw new ZLinkConfigurationException("spotNodeRid is required");
            }
            return createJoinEntrySpotCall(spotNodeRid, Message.from(new byte[0]), defaultRequestTimeout);
        }

        @Override
        public ZLinkActorJoinEntrySpotCall joinEntrySpot(RoutingId spotNodeRid, Object request) {
            if (spotNodeRid == null) {
                throw new ZLinkConfigurationException("spotNodeRid is required");
            }
            if (request == null) {
                throw new ZLinkConfigurationException("request is required");
            }
            return createJoinEntrySpotCall(
                spotNodeRid,
                messageFromRequest(request),
                defaultRequestTimeout);
        }

        private ZLinkActorJoinEntrySpotCall createJoinEntrySpotCall(
            RoutingId spotNodeRid,
            Message request,
            Duration timeout) {
            return new ZLinkActorEntrySpotJoinCall(
                this,
                spotNodeRid,
                request,
                timeout,
                entrySpotJoinServices());
        }

        private ZLinkActorEntrySpotJoinCall.Services entrySpotJoinServices() {
            return new ZLinkActorEntrySpotJoinCall.Services(
                spotNode,
                serializer,
                ZLinkActorRuntime.this::renewActorMovedToEntrySpotLocation);
        }

        @Override
        public ZLinkActorJoinSpotCall joinSpot(RoutingId spotRid) {
            if (spotRid == null) {
                throw new ZLinkConfigurationException("spotRid is required");
            }
            return createJoinSpotCall(spotRid, Message.from(new byte[0]), defaultRequestTimeout);
        }

        @Override
        public ZLinkActorJoinSpotCall joinSpot(RoutingId spotRid, Object request) {
            if (spotRid == null) {
                throw new ZLinkConfigurationException("spotRid is required");
            }
            if (request == null) {
                throw new ZLinkConfigurationException("request is required");
            }
            return createJoinSpotCall(spotRid, messageFromRequest(request), defaultRequestTimeout);
        }

        private ZLinkActorJoinSpotCall createJoinSpotCall(
            RoutingId spotRid,
            Message request,
            Duration timeout) {
            return new ZLinkActorSpotJoinCall(
                this,
                spotRid,
                request,
                timeout,
                spotJoinServices());
        }

        private ZLinkActorSpotJoinCall.Services spotJoinServices() {
            return new ZLinkActorSpotJoinCall.Services(
                spotNode,
                spotResolver,
                remoteAddressResolver,
                routedTransport,
                sourceEntrySpotRid,
                actorTypes::get,
                serializer,
                flow,
                ZLinkActorRuntime.this,
                ZLinkActorRuntime.this::renewActorJoinedLocation);
        }

        private Message messageFromRequest(Object request) {
            if (request instanceof ZLinkMessage message) {
                return Message.from(message.toEncodedPayload(serializer).bytes());
            }
            ZLinkPayloadEncoding.EncodedPayload encoded =
                ZLinkPayloadEncoding.encode(serializer, request);
            return encoded.payload();
        }

        long bindSession(
            ZLinkBoundSession boundSession,
            RoutingId sourceNodeRid,
            RoutingId sourceSessionRid) {
            return state.bindSession(boundSession, sourceNodeRid, sourceSessionRid);
        }

        RoutingId boundSessionSourceSessionRid(long bindingToken) {
            return state.boundSessionSourceSessionRid(bindingToken);
        }

        boolean clearBoundSession(long bindingToken) {
            return state.clearBoundSession(bindingToken);
        }

        CompletionStage<Void> rebindNativeActor(
            ZLinkBackendActorRef targetActor,
            Duration timeout) {
            return state.rebindNativeActor(targetActor, timeout);
        }

        void updateNativeBoundSessionActorRef(ZLinkBackendActorRef targetActor) {
            state.updateNativeBoundSessionActorRef(targetActor);
        }

        boolean hasBoundSession() {
            return state.hasBoundSession();
        }

        boolean hasBoundSession(
            RoutingId sourceNodeRid,
            RoutingId sourceSessionRid) {
            return state.hasBoundSession(sourceNodeRid, sourceSessionRid);
        }

        CompletionStage<Boolean> sendBoundSessionFrame(byte[] frameBytes) {
            return state.sendBoundSessionFrame(frameBytes);
        }

        CompletionStage<Void> disconnectBoundSessionForDestroy() {
            return state.disconnectBoundSessionForDestroy();
        }

        void clearAfterDestroy() {
            state.clearAfterDestroy();
        }

        ZLinkBackendActorRef beginDestroy(RoutingId entryNodeRid, String actorId) {
            return state.beginDestroy(entryNodeRid, actorId);
        }

        void resetDestroying() {
            state.resetDestroying();
        }
    }

}
