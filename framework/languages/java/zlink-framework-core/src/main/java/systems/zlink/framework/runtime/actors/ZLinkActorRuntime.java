package systems.zlink.framework.runtime.actors;

import systems.zlink.framework.runtime.internal.backend.ZLinkInternalSpotNode;

import systems.zlink.framework.runtime.backend.*;

import java.time.Duration;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.runtime.internal.metrics.ZLinkRuntimeMetrics;
import java.util.function.Function;
import java.util.function.Supplier;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.errors.ConfigResult;
import systems.zlink.contracts.errors.ZlinkConfigException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.actors.ActorRef;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorDirectory;
import systems.zlink.framework.actors.ZLinkActorFactory;
import systems.zlink.framework.actors.ZLinkActorJoinCall;
import systems.zlink.framework.actors.ZLinkActorJoinResult;
import systems.zlink.framework.actors.ZLinkActorPlacement;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.actors.ZLinkActorTransferAdapter;
import systems.zlink.framework.actors.ZLinkBoundSession;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.errors.ZLinkFrameworkErrorKind;
import systems.zlink.framework.errors.ZLinkFrameworkException;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.runtime.internal.handlers.ZLinkHandlerActivator;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerStages;
import systems.zlink.framework.runtime.locations.ZLinkLocationLifecycle;
import systems.zlink.framework.runtime.messaging.ZLinkPayloadEncoding;
import systems.zlink.framework.locations.ZLinkLocationWriteIntent;
import systems.zlink.framework.runtime.channels.ZLinkChannelRuntime;
import systems.zlink.framework.runtime.locations.ZLinkStoreLocationResolvers;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.runtime.internal.spots.SpotTransportAddressResolver;
import systems.zlink.framework.spots.SpotHandle;
import systems.zlink.framework.streams.ZLinkStreamCodec;

public final class ZLinkActorRuntime implements ZLinkActorManager, ZLinkActorDirectory {
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

    private final ZLinkInternalSpotNode spotNode;
    private final Map<String, Class<? extends ZLinkActorFactory>> factories;
    private final ZLinkActorTransferRegistry actorTransfers;
    private final Duration defaultRequestTimeout;
    private final Duration actorTransferForwardWindow;
    private final ZLinkMessageSerializer serializer;
    private final ZLinkHandlerActivator handlerFactory;
    private final ZLinkStreamCodec defaultStreamCodec;
    private final ActorRegistry actorRegistry = new ActorRegistry();
    private final ZLinkActorDispatchSerials dispatches = new ZLinkActorDispatchSerials();
    private final ZLinkActorTransferHandoff handoff = new ZLinkActorTransferHandoff();
    private final java.util.concurrent.ConcurrentMap<String, Long> transferStarts =
        new java.util.concurrent.ConcurrentHashMap<>();
    private CreatedNotifier createdNotifier =
        (ignoredNode, ignoredActor, ignoredRequest, ignoredContext) -> CompletableFuture.completedFuture(null);
    private Supplier<Object> actorCreateContextSupplier = () -> null;
    private Function<ZLinkActor, CompletionStage<Void>> disconnectedNotifier =
        ignored -> CompletableFuture.completedFuture(null);
    private SourceActorLeaver sourceActorLeaver =
        ignored -> CompletableFuture.completedFuture(null);
    private Function<RoutingId, ZLinkSpot<?>> spotResolver = ignored -> null;
    private SpotTransportAddressResolver remoteAddressResolver;
    private final ZLinkActorLocationCoordinator locations =
        new ZLinkActorLocationCoordinator(actorRegistry::actorType);
    private ZLinkChannelRuntime routedTransport;
    private Supplier<RoutingId> sourceEntrySpotRid = () -> RoutingId.from(new byte[0]);
    // Shared flow tracer (installed by the host); null = no tracing wired.
    private systems.zlink.framework.runtime.diagnostics.ZLinkMessageFlowTracer flow;
    private volatile boolean draining;

    public void beginDrain() {
        draining = true;
    }

    public boolean drainComplete() {
        for (ActorEntry entry : actorRegistry.entries()) {
            if (entry.context().moving()
                || handoff.forwardingSource(entry.actor().actorId()).isEmpty()) {
                return false;
            }
        }
        return true;
    }

    public CompletionStage<Integer> handoffActorsToEntrySpot(
        String routeChannelName,
        RoutingId targetNodeRid) {
        if (routeChannelName == null || routeChannelName.isBlank() || targetNodeRid == null) {
            return CompletableFuture.completedFuture(0);
        }
        CompletionStage<Integer> transferred = CompletableFuture.completedFuture(0);
        for (ActorEntry entry : actorRegistry.entries()) {
            transferred = transferred.thenCompose(count -> handoffActorToEntrySpot(
                    entry, routeChannelName, targetNodeRid)
                .thenApply(moved -> moved ? count + 1 : count));
        }
        return transferred;
    }

    private CompletionStage<Boolean> handoffActorToEntrySpot(
        ActorEntry entry,
        String routeChannelName,
        RoutingId targetNodeRid) {
        DefaultActorContext context = entry.context();
        CompletionStage<Void> readyToMove = context.moving()
            ? context.moveCompletion()
            : CompletableFuture.completedFuture(null);
        return readyToMove
            .thenCompose(ignored -> awaitActorDispatch(entry.actor()))
            .thenCompose(ignored -> {
            ActorEntry current = actorRegistry.byId.get(entry.actor().actorId());
            if (current != entry
                || context.actorRef() == null
                || !context.actorRef().nodeRid().equals(spotNode.routingId())) {
                return CompletableFuture.completedFuture(false);
            }
            return submitDrainHandoff(
                context,
                routeChannelName,
                targetNodeRid,
                System.nanoTime() + defaultRequestTimeout.toNanos());
        });
    }

    private CompletionStage<Void> awaitActorDispatch(ZLinkActor actor) {
        return submitActorDispatch(
            actor.actorId(),
            () -> CompletableFuture.completedFuture(null));
    }

    private CompletionStage<Boolean> submitDrainHandoff(
        DefaultActorContext context,
        String routeChannelName,
        RoutingId targetNodeRid,
        long deadlineNanos) {
        Message request = Message.from(new byte[0]);
        CompletionStage<Boolean> attempt = new ZLinkActorSpotJoinCall(
                context,
                routeChannelName,
                targetNodeRid,
                request,
                defaultRequestTimeout,
                context.spotJoinServices())
            .submit()
            .thenApply(result -> result instanceof ZLinkActorJoinResult.Accepted<?>);
        return attempt.whenComplete((moved, failure) -> request.close())
            .exceptionallyCompose(failure -> {
                Throwable cause = failure instanceof CompletionException && failure.getCause() != null
                    ? failure.getCause()
                    : failure;
                if (!(cause instanceof systems.zlink.contracts.errors.ZlinkSubmitException)
                    || System.nanoTime() >= deadlineNanos) {
                    return CompletableFuture.failedFuture(cause);
                }
                CompletableFuture<Boolean> retry = new CompletableFuture<>();
                CompletableFuture.delayedExecutor(25L, java.util.concurrent.TimeUnit.MILLISECONDS)
                    .execute(() -> submitDrainHandoff(
                            context, routeChannelName, targetNodeRid, deadlineNanos)
                        .whenComplete((moved, retryFailure) -> {
                            if (retryFailure != null) {
                                retry.completeExceptionally(retryFailure);
                            } else {
                                retry.complete(moved);
                            }
                        }));
                return retry;
            });
    }

    public void setMessageFlowTracer(
        systems.zlink.framework.runtime.diagnostics.ZLinkMessageFlowTracer flow) {
        this.flow = flow;
    }

    public ZLinkActorRuntime(
        ZLinkInternalSpotNode spotNode,
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
            ZLinkHandlerActivator.reflection(),
            ZLinkStreamCodec.JSON);
    }

    public ZLinkActorRuntime(
        ZLinkInternalSpotNode spotNode,
        Map<String, Class<? extends ZLinkActorFactory>> factories,
        Duration defaultRequestTimeout,
        ZLinkMessageSerializer serializer,
        ZLinkHandlerActivator handlerFactory) {
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
        ZLinkInternalSpotNode spotNode,
        Map<String, Class<? extends ZLinkActorFactory>> factories,
        Map<String, Class<? extends ZLinkActorTransferAdapter<?>>> transferAdapters,
        Duration defaultRequestTimeout,
        ZLinkMessageSerializer serializer,
        ZLinkHandlerActivator handlerFactory,
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
        ZLinkInternalSpotNode spotNode,
        Map<String, Class<? extends ZLinkActorFactory>> factories,
        Map<String, Class<? extends ZLinkActorTransferAdapter<?>>> transferAdapters,
        Duration defaultRequestTimeout,
        Duration actorTransferForwardWindow,
        ZLinkMessageSerializer serializer,
        ZLinkHandlerActivator handlerFactory,
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
        if (draining && intent != ZLinkLocationWriteIntent.TAKEOVER) {
            return CompletableFuture.failedFuture(new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ACTOR_CREATE_REJECTED,
                "actor creation is rejected while the node is draining"));
        }
        if (createRequest == null) {
            throw new ZLinkConfigurationException("createRequest is required");
        }
        Class<? extends ZLinkActorFactory> factoryType = requireFactory(actorType);
        ZLinkActor existing = actorRegistry.actor(actorId);
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
        if (actorRegistry.contains(actorId)) {
            throw new ZLinkConfigurationException("duplicate actor id: " + actorId);
        }
        DefaultActorContext context = new DefaultActorContext(actorRef);
        return ZLinkHandlerStages
            .fromSupplier(() -> createFactory(factoryType).create(actorId, context))
            .thenCompose(stage -> stage)
            .thenApply(actor -> {
                context.setActor(actor);
                actorRegistry.register(actorId, actorType, actor, context);
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
        ZLinkBackendActorRef reentryActorRef = detachForwardingProxyForReentry(actorId);
        if (actorRegistry.contains(actorId)) {
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
                factoryType,
                reentryActorRef)
            .thenApply(actor -> {
                actorRegistry.markTransferred(actorId);
                return actor;
            });
    }

    private synchronized ZLinkBackendActorRef detachForwardingProxyForReentry(String actorId) {
        ZLinkActorTransferHandoff.ForwardingSource forwarding =
            handoff.forwardingSource(actorId).orElse(null);
        ZLinkActor oldActor = actorRegistry.actor(actorId);
        if (forwarding == null || oldActor == null) {
            return null;
        }
        DefaultActorContext oldContext = actorRegistry.context(oldActor);
        if (oldContext == null || !forwarding.targetActorRef().equals(oldContext.actorRef())) {
            return null;
        }
        actorRegistry.remove(actorId, oldActor);
        dispatches.remove(actorId);
        return handoff.takeForwardingSource(actorId)
            .map(ZLinkActorTransferHandoff.ForwardingSource::sourceActorRef)
            .orElse(null);
    }

    public CompletionStage<Void> rollbackTransferredActor(ZLinkActor actor) {
        DefaultActorContext context = requireContext(actor);
        ZLinkBackendActorRef actorRef = context.actorRef();
        String actorType = actorRegistry.actorTypeOrDefault(actor.actorId(), "");
        context.markLeft();
        return spotNode.destroyActor(actorRef, defaultRequestTimeout)
            .exceptionally(error -> null)
            .thenCompose(ignored -> locations.releaseActor(actorType, actor.actorId()))
            .thenRun(() -> {
                removeActorSessionRouteForContext(context);
                context.clearAfterDestroy();
                actorRegistry.remove(actor.actorId(), actor);
                dispatches.remove(actor.actorId());
            });
    }

    private CompletionStage<ZLinkActor> activateTransferredActor(
        String actorId,
        String actorType,
        String adapterKey,
        ZLinkMessage transferState,
        Class<? extends ZLinkActorFactory> factoryType,
        ZLinkBackendActorRef reentryActorRef) {
        ZLinkBackendActorRef actorRef = reentryActorRef;
        if (actorRef == null) {
            Message nativeCreateRequest = Message.from(new byte[0]);
            try {
                actorRef = spotNode.createActor(actorId, nativeCreateRequest);
            } catch (RuntimeException error) {
                nativeCreateRequest.close();
                throw error;
            }
        }
        DefaultActorContext context = new DefaultActorContext(actorRef);
        context.beginMove();
        return ZLinkHandlerStages.fromStageSupplier(() -> adapterKey == null
                ? createFactory(factoryType).create(actorId, context)
                : actorTransfers.transferIn(
                    actorType,
                    actorId,
                    context,
                    transferState))
            .thenApply(actor -> {
                if (actor == null) {
                    throw new ZLinkConfigurationException(
                        "actor transfer did not materialize an actor: " + actorId);
                }
                context.setActor(actor);
                actorRegistry.register(actorId, actorType, actor, context);
                return actor;
            });
    }

    CompletionStage<ZLinkActorTransferRegistry.TransferState> transferOut(ZLinkActor actor) {
        String actorType = actorRegistry.actorType(actor.actorId());
        if (actorType == null) {
            throw new ZLinkConfigurationException(
                "actor type is not registered for transfer: " + actor.actorId());
        }
        return actorTransfers.transferOut(actorType, actor);
    }

    CompletionStage<Void> beginRemoteMove(ZLinkActor actor) {
        DefaultActorContext context = requireContext(actor);
        context.beginMove();
        handoff.begin(actor.actorId());
        if (ZLinkRuntimeMetrics.enabled()) {
            transferStarts.put(actor.actorId(), System.nanoTime());
            ZLinkRuntimeMetrics.record("zlink.actor.transfer.pending_requests.count",
                handoff.pendingCount(actor.actorId()), java.util.Map.of());
        }
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
        if (context.joinedSpotRid() == null) {
            return CompletableFuture.completedFuture(null);
        }
        return sourceActorLeaver.leave(actor)
            .thenRun(context::markLeft);
    }

    CompletionStage<Void> leaveSourceForRemoteMove(ZLinkActor actor) {
        DefaultActorContext context = requireContext(actor);
        RoutingId currentSpotRid = context.joinedSpotRid();
        return sourceActorLeaver.leave(actor)
            .thenCompose(ignored -> {
                return currentSpotRid == null || actorRegistry.isRoutedTransfer(actor.actorId())
                ? CompletableFuture.completedFuture(null)
                : spotNode.leaveActor(
                        context.actorRef(),
                        currentSpotRid,
                        defaultRequestTimeout)
                    .thenAccept(parts -> parts.forEach(Message::close));
            })
            .thenRun(context::markLeft);
    }

    void cancelRemoteMove(ZLinkActor actor) {
        failTransferBacklog(actor, new ZLinkConfigurationException("actor transfer was cancelled"));
        requireContext(actor).endMove();
    }

    public void completeRemoteMove(ZLinkActor actor) {
        requireContext(actor).endMove();
        Long started = transferStarts.remove(actor.actorId());
        ZLinkRuntimeMetrics.increment("zlink.actor.transfers", java.util.Map.of());
        if (started != null) {
            ZLinkRuntimeMetrics.record("zlink.actor.transfer.duration",
                java.time.Duration.ofNanos(System.nanoTime() - started), java.util.Map.of());
        }
    }

    void abandonSourceLocationOwnership(ZLinkActor actor) {
        locations.abandonActor(actor.actorId());
    }

    void retainForwardingSource(
        ZLinkActor actor,
        ZLinkBackendActorRef sourceActorRef,
        ZLinkBackendActorRef targetActorRef) {
        handoff.retain(
            actor.actorId(), sourceActorRef, targetActorRef, actorTransferForwardWindow,
            this::retireForwardingSource);
        // Keep the actor context as a forwarding proxy while the old native actor
        // reference can still receive packets. EntrySpot dispatch uses its rebound
        // target reference to relay those packets to the new owner.
        traceActorTransferMarker(
            "source_cleanup",
            actor.actorId(),
            Long.toUnsignedString(sourceActorRef.generation()));
    }

    int forwardingSourceCount() {
        return handoff.forwardingSourceCount();
    }

    private void retireForwardingSource(
        ZLinkActorTransferHandoff.ForwardingSource source) {
        cleanupForwardingNativeSource(source.sourceActorRef());
    }

    private void cleanupForwardingNativeSource(ZLinkBackendActorRef sourceActorRef) {
        spotNode.destroyActor(sourceActorRef, defaultRequestTimeout)
            .whenComplete((ignored, error) -> {
                if (error != null && !ZLinkActorSubmitFaults.requestNotFound(error)) {
                    ZLinkActorRetryScheduler.scheduleRoute(
                        () -> cleanupForwardingNativeSource(sourceActorRef));
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
        if (!actorRegistry.clearPendingTransfer(actor.actorId())) {
            return renewActorJoinedLocation(actor, spotRid);
        }
        String actorType = actorRegistry.actorType(actor.actorId());
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

    public CompletionStage<Void> commitEntryLocation(
        ZLinkActor actor,
        RoutingId entryNodeRid) {
        DefaultActorContext context = requireContext(actor);
        context.setEntrySpotNodeRid(entryNodeRid);
        context.markMovedToEntrySpot(context.actorRef(), entryNodeRid);
        String actorType = actorRegistry.actorType(actor.actorId());
        CompletionStage<Void> ownership = actorRegistry.clearPendingTransfer(actor.actorId())
            ? locations.claimActor(
                actorType,
                actor.actorId(),
                spotNode.routingId(),
                ZLinkLocationWriteIntent.TAKEOVER,
                () -> deactivateActorOnOwnershipLoss(actor.actorId()))
            : CompletableFuture.completedFuture(null);
        return ownership
            .thenCompose(ignored -> locations.setActorRef(
                actorType, actor.actorId(), publicRefFor(actor)))
            .thenCompose(ignored -> renewActorMovedToEntrySpotLocation(actor, entryNodeRid));
    }

    private DefaultActorContext requireContext(ZLinkActor actor) {
        DefaultActorContext context = actorRegistry.context(actor);
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
            ActorEntry removed = actorRegistry.remove(actorId);
            actor = removed == null ? null : removed.actor();
            context = removed == null ? null : removed.context();
            actorType = removed == null ? null : removed.actorType();
            dispatches.remove(actorId);
        }
        if (actor == null || context == null || context.actorRef() == null) {
            return;
        }
        context.disconnectBoundSessionForDestroy()
            .exceptionally(error -> null)
            .thenCompose(ignored -> spotNode.destroyActor(context.actorRef(), defaultRequestTimeout)
                .exceptionally(error -> null))
            .thenCompose(ignored -> locations.releaseActor(actorType, actorId)
                .exceptionally(error -> null))
            .whenComplete((ignored, failure) -> removeActorSessionRouteForContext(context));
    }

    @Override
    public CompletionStage<Optional<ActorRef>> find(String actorId) {
        requireActorId(actorId);
        ZLinkActor local = actorRegistry.actor(actorId);
        if (local != null && !actorRegistry.isPendingTransfer(actorId)) {
            return CompletableFuture.completedFuture(Optional.of(publicRefFor(local)));
        }
        if (actorRegistry.isPendingTransfer(actorId)) {
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
        ZLinkActor actor = actorRegistry.actor(actorId);
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
        DefaultActorContext context = actorRegistry.context(actor);
        if (context == null) {
            throw new ZLinkConfigurationException(
                "actor is not managed by this runtime: " + actor.actorId());
        }
        return context.actorRef();
    }

    public ZLinkBackendActorRef currentRef(ZLinkActor actor) {
        return refFor(actor);
    }

    public ZLinkSpot<?> currentSpot(ZLinkActor actor) {
        DefaultActorContext context = actorRegistry.context(actor);
        if (context == null || context.joinedSpotRid() == null) {
            return null;
        }
        return context.currentSpot();
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
        ZLinkActor current = actorRegistry.actor(actor.actorId());
        if (current == null) {
            throw new ZLinkConfigurationException(
                "actor is not managed by this runtime: " + actor.actorId());
        }
        DefaultActorContext context = actorRegistry.context(current);
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
        String actorType = actorRegistry.actorType(actor.actorId());
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
        DefaultActorContext context = actorRegistry.context(actor);
        if (context == null) {
            throw new ZLinkConfigurationException(
                "actor is not managed by this runtime: " + actor.actorId());
        }
        long bindingToken = context.bindSession(boundSession, sourceNodeRid, sourceSessionRid);
        locations.bindSessionRoute(sourceSessionRid, actor.actorId(), sourceNodeRid);
        return bindingToken;
    }

    public boolean clearSessionBinding(ZLinkActor actor, long bindingToken) {
        DefaultActorContext context = actorRegistry.context(actor);
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
                    actorRegistry.actorTypeOrDefault(actor.actorId(), request.actorType()),
                    actorRef.nodeRid(),
                    actorRef.generation());
            });
    }

    public Optional<ZLinkActor> localActor(String actorId) {
        return Optional.ofNullable(actorRegistry.actor(actorId));
    }

    public CompletionStage<ZLinkActor> getOrCreateManagedActor(
        String actorId,
        String actorType) {
        return getOrCreateManagedActor(actorId, actorType, true);
    }

    public CompletionStage<ZLinkActor> getOrCreateManagedActorWithoutLocationClaim(
        String actorId,
        String actorType) {
        ZLinkActor existing = actorRegistry.actor(actorId);
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
        ZLinkActor existing = actorRegistry.actor(actorId);
        if (existing != null) {
            return CompletableFuture.completedFuture(existing);
        }
        return createLocalActor(actorId, actorType, ZLinkMessage.empty(), false, notifyCreated);
    }

    public boolean hasBoundSession(ZLinkActor actor) {
        DefaultActorContext context = actorRegistry.context(actor);
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
        DefaultActorContext context = actorRegistry.context(actor);
        if (context == null) {
            throw new ZLinkConfigurationException(
                "actor is not managed by this runtime: " + actor.actorId());
        }
        return context.hasBoundSession(sourceNodeRid, sourceSessionRid);
    }

    public CompletionStage<Boolean> sendBoundSessionFrame(
        ZLinkActor actor,
        byte[] frameBytes) {
        DefaultActorContext context = actorRegistry.context(actor);
        if (context == null) {
            throw new ZLinkConfigurationException(
                "actor is not managed by this runtime: " + actor.actorId());
        }
        return context.sendBoundSessionFrame(frameBytes);
    }

    public Optional<RoutingId> spotRid(ZLinkActor actor) {
        DefaultActorContext context = actorRegistry.context(actor);
        if (context == null) {
            throw new ZLinkConfigurationException(
                "actor is not managed by this runtime: " + actor.actorId());
        }
        return Optional.ofNullable(context.joinedSpotRid());
    }

    public ZLinkBackendActorRef actorRef(ZLinkActor actor) {
        DefaultActorContext context = actorRegistry.context(actor);
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
        return resolveHandle(spotRid)
            .thenCompose(remoteAddressResolver::resolve)
            .thenCompose(address -> {
                var target = address.orElseThrow(() ->
                    new ZLinkConfigurationException("SPOT transport address was not found: " + spotRid));
                List<Message> parts =
                    ZLinkActorSpotRoutePackets.createActorPacketParts(
                        actorRef, header, payload, null);
                try {
                    if (header.requestSequence().isPresent()
                        || header.kind() == systems.zlink.framework.streams.ZLinkStreamMessageKind.REQUEST) {
                        return routedTransport.requestToSpotViaRouterChannel(
                                target.routerChannelId(),
                                target.targetNodeRid(),
                                target.spotRid(),
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
                            target.routerChannelId(),
                            target.targetNodeRid(),
                            target.spotRid(),
                            parts)
                        .thenApply(ignored -> Optional.<Message>empty());
                } finally {
                    parts.forEach(Message::close);
                }
            });
    }

    public boolean hasActorsInSpot(RoutingId spotRid) {
        for (DefaultActorContext context : actorRegistry.contexts()) {
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
        ZLinkActor existing = actorRegistry.actor(actorId);
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
            if (!actorRegistry.contains(actorId)) {
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

    public void setRemoteAddressResolver(SpotTransportAddressResolver remoteAddressResolver) {
        this.remoteAddressResolver = remoteAddressResolver;
    }

    private CompletionStage<SpotHandle> resolveHandle(RoutingId spotRid) {
        if (!(remoteAddressResolver instanceof systems.zlink.framework.spots.SpotHandleResolver handles)) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "SPOT transport resolver does not provide opaque handles"));
        }
        return handles.resolveSpotHandle(spotRid).thenApply(handle -> handle.orElseThrow(() ->
            new ZLinkConfigurationException("SPOT handle was not found: " + spotRid)));
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

    private void markJoinedState(
        ZLinkActor actor,
        ZLinkBackendActorRef actorRef,
        RoutingId spotRid,
        ZLinkSpot<?> spot) {
        DefaultActorContext context = actorRegistry.context(actor);
        if (context == null) {
            throw new ZLinkConfigurationException(
                "actor is not managed by this runtime: " + actor.actorId());
        }
        context.markJoined(actorRef, spotRid, spot);
    }

    public CompletionStage<Void> markJoined(
        ZLinkActor actor,
        ZLinkBackendActorRef actorRef,
        RoutingId spotRid,
        ZLinkSpot<?> spot) {
        markJoinedState(actor, actorRef, spotRid, spot);
        return renewActorJoinedLocation(actor, spotRid);
    }

    public long bindNativeSession(
        ZLinkActor actor,
        ZLinkInternalSpotNode spotNode,
        ZLinkBackendActorRef actorRef) {
        return bindNativeSession(actor, spotNode, actorRef, null, null);
    }

    public long bindNativeSession(
        ZLinkActor actor,
        ZLinkInternalSpotNode spotNode,
        ZLinkBackendActorRef actorRef,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid) {
        DefaultActorContext context = actorRegistry.context(actor);
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

    public RoutingId entrySpotNodeRid(ZLinkActor actor) {
        return requireContext(actor).entrySpotNodeRid();
    }

    public boolean isRoutedTransferActor(ZLinkActor actor) {
        requireContext(actor);
        return actorRegistry.isRoutedTransfer(actor.actorId());
    }

    public RoutingId entrySpotRid(ZLinkActor actor) {
        return requireContext(actor).entrySpotRid();
    }

    public String entryRouterChannelId(ZLinkActor actor) {
        return requireContext(actor).entryRouterChannelId();
    }

    public void setEntrySpotNodeRid(ZLinkActor actor, RoutingId entrySpotNodeRid) {
        requireContext(actor).setEntrySpotNodeRid(entrySpotNodeRid);
    }

    public void setEntrySpotRid(ZLinkActor actor, RoutingId entrySpotRid) {
        requireContext(actor).setEntrySpotRid(entrySpotRid);
    }

    public void setEntryRouterChannelId(ZLinkActor actor, String entryRouterChannelId) {
        requireContext(actor).setEntryRouterChannelId(entryRouterChannelId);
    }

    public CompletionStage<Void> joinEntrySpot(
        ZLinkActor actor,
        RoutingId entrySpotNodeRid,
        Duration timeout) {
        DefaultActorContext context = requireContext(actor);
        Message request = Message.from(new byte[0]);
        return new ZLinkActorEntrySpotJoinCall(
                context,
                entrySpotNodeRid,
                request,
                timeout,
                context.entrySpotJoinServices())
            .submit()
            .thenCompose(result -> result instanceof systems.zlink.framework.actors.ZLinkActorJoinResult.Accepted<?>
                ? CompletableFuture.<Void>completedFuture(null)
                : CompletableFuture.<Void>failedFuture(new ZLinkConfigurationException(
                    "actor Entry Spot join was rejected: " + actor.actorId())))
            .whenComplete((ignored, error) -> request.close());
    }

    private void markLeftState(ZLinkActor actor) {
        DefaultActorContext context = actorRegistry.context(actor);
        if (context == null) {
            throw new ZLinkConfigurationException(
                "actor is not managed by this runtime: " + actor.actorId());
        }
        context.markLeft();
    }

    public CompletionStage<Void> markLeft(ZLinkActor actor) {
        markLeftState(actor);
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
            ZLinkActor current = actorRegistry.actor(actor.actorId());
            if (current == null || current != actor) {
                return CompletableFuture.completedFuture(null);
            }

            context = actorRegistry.context(actor);
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

        String actorType = actorRegistry.actorTypeOrDefault(actor.actorId(), "");
        return spotNode.destroyActor(actorRef, defaultRequestTimeout)
            .thenCompose(ignored -> context.disconnectBoundSessionForDestroy()
                .exceptionally(error -> null))
            .thenCompose(ignored -> locations.releaseActor(actorType, actor.actorId()))
            .thenRun(() -> {
                synchronized (this) {
                    removeActorSessionRouteForContext(context);
                    context.clearAfterDestroy();
                    actorRegistry.remove(actor.actorId(), actor);
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
        closeAsync();
    }

    public CompletionStage<Void> closeAsync() {
        handoff.close();
        List<ActorEntry> snapshot;
        synchronized (this) {
            snapshot = actorRegistry.entries();
        }
        snapshot.forEach(this::closeActorEntry);
        return CompletableFuture.completedFuture(null);
    }

    private CompletionStage<Void> closeActorEntry(ActorEntry entry) {
        ZLinkActor actor = entry.actor();
        DefaultActorContext context = entry.context();
        locations.releaseActor(entry.actorType(), actor.actorId())
            .exceptionally(error -> null);
        synchronized (this) {
            removeActorSessionRouteForContext(context);
            context.clearAfterDestroy();
            actorRegistry.remove(actor.actorId(), actor);
            dispatches.remove(actor.actorId());
        }
        return CompletableFuture.completedFuture(null);
    }

    private final class ActorRegistry {
        private final Map<String, ActorEntry> byId = new java.util.HashMap<>();
        private final Map<ZLinkActor, ActorEntry> byActor = new IdentityHashMap<>();

        synchronized void register(
            String actorId,
            String actorType,
            ZLinkActor actor,
            DefaultActorContext context) {
            ActorEntry entry = new ActorEntry(actorType, actor, context);
            ActorEntry previous = byId.put(actorId, entry);
            if (previous != null) {
                byActor.remove(previous.actor());
            }
            byActor.put(actor, entry);
        }

        synchronized boolean contains(String actorId) {
            return byId.containsKey(actorId);
        }

        synchronized ZLinkActor actor(String actorId) {
            ActorEntry entry = byId.get(actorId);
            return entry == null ? null : entry.actor();
        }

        synchronized DefaultActorContext context(ZLinkActor actor) {
            ActorEntry entry = byActor.get(actor);
            return entry == null ? null : entry.context();
        }

        synchronized String actorType(String actorId) {
            ActorEntry entry = byId.get(actorId);
            return entry == null ? null : entry.actorType();
        }

        synchronized String actorTypeOrDefault(String actorId, String fallback) {
            String actorType = actorType(actorId);
            return actorType == null ? fallback : actorType;
        }

        synchronized void markTransferred(String actorId) {
            ActorEntry entry = byId.get(actorId);
            if (entry == null) {
                throw new ZLinkConfigurationException(
                    "actor is not managed by this runtime: " + actorId);
            }
            entry.pendingTransfer = true;
            entry.routedTransfer = true;
        }

        synchronized boolean isPendingTransfer(String actorId) {
            ActorEntry entry = byId.get(actorId);
            return entry != null && entry.pendingTransfer;
        }

        synchronized boolean clearPendingTransfer(String actorId) {
            ActorEntry entry = byId.get(actorId);
            if (entry == null || !entry.pendingTransfer) {
                return false;
            }
            entry.pendingTransfer = false;
            return true;
        }

        synchronized boolean isRoutedTransfer(String actorId) {
            ActorEntry entry = byId.get(actorId);
            return entry != null && entry.routedTransfer;
        }

        synchronized ActorEntry remove(String actorId) {
            ActorEntry removed = byId.remove(actorId);
            if (removed != null) {
                byActor.remove(removed.actor());
            }
            return removed;
        }

        synchronized boolean remove(String actorId, ZLinkActor actor) {
            ActorEntry entry = byId.get(actorId);
            if (entry == null || entry.actor() != actor) {
                return false;
            }
            byId.remove(actorId);
            byActor.remove(actor);
            return true;
        }

        synchronized List<ActorEntry> entries() {
            return List.copyOf(byId.values());
        }

        synchronized List<DefaultActorContext> contexts() {
            return byId.values().stream().map(ActorEntry::context).toList();
        }
    }

    private final class ActorEntry {
        private final String actorType;
        private final ZLinkActor actor;
        private final DefaultActorContext context;
        private boolean pendingTransfer;
        private boolean routedTransfer;

        private ActorEntry(
            String actorType,
            ZLinkActor actor,
            DefaultActorContext context) {
            this.actorType = actorType;
            this.actor = actor;
            this.context = context;
        }

        String actorType() {
            return actorType;
        }

        ZLinkActor actor() {
            return actor;
        }

        DefaultActorContext context() {
            return context;
        }
    }

    final class DefaultActorContext implements ZLinkActorContext {
        private final ZLinkActorContextState state;

        DefaultActorContext(ZLinkBackendActorRef actorRef) {
            RoutingId configuredEntrySpotRid = sourceEntrySpotRid.get();
            this.state = new ZLinkActorContextState(
                actorRef,
                configuredEntrySpotRid == null || configuredEntrySpotRid.toString().isBlank()
                    ? actorRef.nodeRid()
                    : configuredEntrySpotRid);
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

        ZLinkSpot<?> currentSpot() {
            return state.spot();
        }

        RoutingId boundSessionSourceNodeRid() {
            return state.boundSessionSourceNodeRid();
        }

        RoutingId boundSessionSourceSessionRid() {
            return state.boundSessionSourceSessionRid();
        }

        RoutingId entrySpotNodeRid() {
            return state.entrySpotNodeRid();
        }

        RoutingId entrySpotRid() {
            return state.entrySpotRid();
        }

        String entryRouterChannelId() {
            return state.entryRouterChannelId();
        }

        void setEntrySpotNodeRid(RoutingId entrySpotNodeRid) {
            state.setEntrySpotNodeRid(entrySpotNodeRid);
        }

        void setEntrySpotRid(RoutingId entrySpotRid) {
            state.setEntrySpotRid(entrySpotRid);
        }

        void setEntryRouterChannelId(String entryRouterChannelId) {
            state.setEntryRouterChannelId(entryRouterChannelId);
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
        public ZLinkBoundSession boundSession() {
            return state.requireBoundSession();
        }

        @Override
        public ZLinkActorJoinCall joinEntrySpot(RoutingId spotNodeRid, Object request) {
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

        private ZLinkActorJoinCall createJoinEntrySpotCall(
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
        public ZLinkActorJoinCall joinSpot(RoutingId spotRid, Object request) {
            if (spotRid == null) {
                throw new ZLinkConfigurationException("spotRid is required");
            }
            if (request == null) {
                throw new ZLinkConfigurationException("request is required");
            }
            return createJoinSpotCall(spotRid, messageFromRequest(request), defaultRequestTimeout);
        }

        private ZLinkActorJoinCall createJoinSpotCall(
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

        ZLinkActorSpotJoinCall.Services spotJoinServices() {
            return new ZLinkActorSpotJoinCall.Services(
                spotNode,
                spotResolver,
                remoteAddressResolver,
                routedTransport,
                actorRegistry::actorType,
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
