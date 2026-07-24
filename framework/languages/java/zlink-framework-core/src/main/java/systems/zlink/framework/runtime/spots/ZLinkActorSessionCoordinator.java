package systems.zlink.framework.runtime.spots;

import java.util.List;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.function.Function;
import java.util.function.Predicate;
import java.util.function.Supplier;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.runtime.actors.ZLinkActorRuntime;
import systems.zlink.framework.runtime.actors.ZLinkActorReplyRoute;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorRef;
import systems.zlink.framework.runtime.internal.backend.ZLinkInternalSpotNode;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorReceived;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeader;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.spots.ZLinkSpot;

final class ZLinkActorSessionCoordinator {
    record ActorRoute(
        Optional<String> joinedSpotId,
        ZLinkBackendActorRef actorRef,
        boolean remoteJoinedSpot) {
    }

    record LocalDispatch(
        ZLinkActor actor,
        Optional<String> joinedSpotId) {
    }

    private ZLinkActorRuntime actors;

    void attach(
        ZLinkActorRuntime actors,
        ZLinkActorRuntime.CreatedNotifier createdNotifier,
        Supplier<Object> actorCreateContextSupplier,
        Function<ZLinkActor, CompletionStage<Void>> disconnectedNotifier,
        ZLinkActorRuntime.SourceActorLeaver sourceActorLeaver,
        Function<String, ZLinkSpot<?>> spotResolver,
        Function<String, String> spotMeshResolver) {
        this.actors = actors;
        actors.setCreatedNotifier(createdNotifier);
        actors.setActorCreateContextSupplier(actorCreateContextSupplier);
        actors.setDisconnectedNotifier(disconnectedNotifier);
        actors.setSourceActorLeaver(sourceActorLeaver);
        actors.setSpotResolver(spotResolver);
        actors.setSpotMeshResolver(spotMeshResolver);
    }

    boolean available() {
        return actors != null;
    }

    boolean hasActorsInSpot(String spotId) {
        return actors != null && actors.hasActorsInSpot(spotId);
    }

    List<String> actorIdsInSpot(String spotId) {
        return actors == null
            ? List.of()
            : actors.actorIdsInSpot(spotId);
    }

    Optional<systems.zlink.framework.execution.ZLinkAsyncSerialQueue.RelocationSeal>
        trySealActorRelocation(String actorId) {
        return requireActors().trySealActorRelocation(actorId);
    }

    boolean abortActorRelocation(
        String actorId,
        systems.zlink.framework.execution.ZLinkAsyncSerialQueue.RelocationSeal
            seal) {
        return requireActors().abortActorRelocation(actorId, seal);
    }

    Optional<List<systems.zlink.framework.execution.ZLinkAsyncSerialQueue.QueuedRecord>>
        commitActorRelocation(
            String actorId,
            systems.zlink.framework.execution.ZLinkAsyncSerialQueue.RelocationSeal
                seal) {
        return requireActors().commitActorRelocation(actorId, seal);
    }

    Optional<ZLinkActor> localActor(String actorId) {
        return actors == null ? Optional.empty() : actors.localActor(actorId);
    }

    boolean isActorMember(String spotId, String actorId) {
        if (actors == null) {
            return false;
        }
        return actors.localActor(actorId)
            .flatMap(actors::spotId)
            .filter(spotId::equals)
            .isPresent();
    }

    CompletionStage<Void> dispatch(
        ZLinkActor actor,
        Supplier<CompletionStage<Void>> operation) {
        return requireActors().submitActorDispatch(actor.actorId(), operation);
    }

    CompletionStage<Optional<Message>> captureMoving(
        ZLinkActor actor,
        ZLinkStreamHeader header,
        Message payload,
        ZLinkActorReplyRoute replyRoute) {
        return requireActors().captureMovingPacket(actor, header, payload, replyRoute);
    }

    boolean isMoving(ZLinkActor actor) {
        return requireActors().isMoving(actor);
    }

    CompletionStage<Optional<Message>> dispatchLocalSession(
        ZLinkBackendActorRef actorRef,
        ZLinkStreamHeader header,
        Message payload,
        Predicate<String> isLocalSpot,
        Function<LocalDispatch, CompletionStage<Optional<Message>>> localDispatch) {
        return dispatchLocalSession(
            actorRef, header, payload, isLocalSpot, localDispatch, true);
    }

    CompletionStage<Optional<Message>> dispatchTransferBacklog(
        ZLinkBackendActorRef actorRef,
        ZLinkStreamHeader header,
        Message payload,
        Predicate<String> isLocalSpot,
        Function<LocalDispatch, CompletionStage<Optional<Message>>> localDispatch) {
        return dispatchLocalSession(
            actorRef, header, payload, isLocalSpot, localDispatch, false);
    }

    private CompletionStage<Optional<Message>> dispatchLocalSession(
        ZLinkBackendActorRef actorRef,
        ZLinkStreamHeader header,
        Message payload,
        Predicate<String> isLocalSpot,
        Function<LocalDispatch, CompletionStage<Optional<Message>>> localDispatch,
        boolean captureMovingPacket) {
        ZLinkActorRuntime runtime = requireActors();
        Optional<ZLinkActor> localActor = runtime.localActor(actorRef.actorId());
        if (localActor.isEmpty()) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "local actor is not available: " + actorRef.actorId()));
        }
        ZLinkActor actor = localActor.get();
        if (captureMovingPacket && runtime.isMoving(actor)) {
            CompletionStage<Optional<Message>> captured =
                runtime.captureMovingPacket(actor, header, payload);
            if (captured != null) {
                return captured;
            }
            return runtime.awaitMoveCompletion(actor)
                .thenCompose(ignored -> dispatchLocalSession(
                    actorRef,
                    header,
                    payload,
                    isLocalSpot,
                    localDispatch));
        }
        Optional<String> joinedSpotId = runtime.spotId(actor);
        if (joinedSpotId.isPresent()
            && currentSpotSurface(actor) == null
            && !isLocalSpot.test(joinedSpotId.get())
            && runtime.canRouteRemoteJoinedSpot(joinedSpotId.get())) {
            return runtime.dispatchRemoteJoinedActor(
                runtime.currentRef(actor),
                joinedSpotId.get(),
                header,
                payload);
        }
        if (!captureMovingPacket) {
            // Transfer commit already owns the actor's serialized turn. Queuing
            // replay behind that turn would wait on the commit that is waiting
            // for this replay to finish.
            return localDispatch.apply(new LocalDispatch(actor, joinedSpotId));
        }
        CompletableFuture<Optional<Message>> result = new CompletableFuture<>();
        return runtime.submitActorDispatch(
                actor.actorId(),
                ZLinkActorAcceptedJournal.encode(actor.actorId(), header, payload),
                () -> invokeLocalDispatch(
                    localDispatch,
                    new LocalDispatch(actor, joinedSpotId),
                    result))
            .thenCompose(ignored -> result);
    }

    CompletionStage<Optional<Message>> runPacketTurn(
        ZLinkActor actor,
        boolean request,
        boolean noBindRequest,
        ZLinkBackendActorReceived headerPart,
        ZLinkInternalSpotNode primaryNode,
        Supplier<CompletionStage<Optional<Message>>> operation) {
        ZLinkActorRuntime runtime = requireActors();
        if (request
            && !noBindRequest
            && !runtime.hasBoundSession(
                actor,
                headerPart.sourceNodeRid(),
                headerPart.sourceSessionRid())) {
            runtime.bindNativeSession(
                actor,
                primaryNode,
                headerPart.actor(),
                headerPart.sourceNodeRid(),
                headerPart.sourceSessionRid());
        }
        return runtime.runActorDispatchTurn(actor.actorId(), operation);
    }

    boolean hasBoundSession(ZLinkActor actor) {
        return requireActors().hasBoundSession(actor);
    }

    void bindNativeSession(
        ZLinkActor actor,
        ZLinkInternalSpotNode node,
        ZLinkBackendActorRef actorRef,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid) {
        requireActors().bindNativeSession(
            actor,
            node,
            actorRef,
            sourceNodeRid,
            sourceSessionRid);
    }

    boolean isJoinedTo(
        ZLinkActor actor,
        ZLinkBackendActorRef actorRef,
        String spotId) {
        if (actors == null || actor == null || actorRef == null || spotId == null) {
            return false;
        }
        return actors.spotId(actor).filter(spotId::equals).isPresent()
            && actorRef.equals(actors.currentRef(actor));
    }

    boolean isJoinedToDifferentSpot(ZLinkActor actor, String spotId) {
        if (actors == null || actor == null || spotId == null) {
            return false;
        }
        return actors.spotId(actor)
            .map(current -> !spotId.equals(current))
            .orElse(false);
    }

    Object spotSurface(
        ZLinkActor actor,
        Function<String, Object> resolver,
        Supplier<Object> fallback) {
        Object current = currentSpotSurface(actor);
        if (current != null) {
            return current;
        }
        if (actors != null) {
            Object resolved = actors.spotId(actor).map(resolver).orElse(null);
            if (resolved != null) {
                return resolved;
            }
        }
        return fallback.get();
    }

    ActorRoute routeFor(
        ZLinkActor actor,
        RoutingId localNodeRid,
        Predicate<String> isLocalSpot) {
        ZLinkActorRuntime runtime = requireActors();
        Optional<String> joinedSpotId = runtime.spotId(actor);
        ZLinkBackendActorRef actorRef = runtime.currentRef(actor);
        boolean remote = joinedSpotId.isPresent()
            && !actorRef.nodeRid().equals(localNodeRid)
            && !isLocalSpot.test(joinedSpotId.get())
            && runtime.canRouteRemoteJoinedSpot(joinedSpotId.get());
        return new ActorRoute(joinedSpotId, actorRef, remote);
    }

    CompletionStage<Void> sendBoundSession(
        ZLinkBackendActorRef actorRef,
        byte[] frameBytes,
        Supplier<CompletionStage<Void>> fallback) {
        ZLinkActorRuntime runtime = requireActors();
        return runtime.localActor(actorRef.actorId())
            .map(actor -> runtime.sendBoundSessionFrame(actor, frameBytes)
                .thenCompose(sent -> sent
                    ? CompletableFuture.completedFuture(null)
                    : fallback.get()))
            .orElseGet(fallback);
    }

    private static CompletionStage<Void> invokeLocalDispatch(
        Function<LocalDispatch, CompletionStage<Optional<Message>>> dispatch,
        LocalDispatch local,
        CompletableFuture<Optional<Message>> result) {
        try {
            return dispatch.apply(local)
                .whenComplete((reply, error) -> {
                    if (error != null) {
                        result.completeExceptionally(error);
                    } else {
                        result.complete(reply);
                    }
                })
                .thenApply(ignored -> null);
        } catch (RuntimeException error) {
            result.completeExceptionally(error);
            return CompletableFuture.failedFuture(error);
        }
    }

    private ZLinkActorRuntime requireActors() {
        if (actors == null) {
            throw new ZLinkConfigurationException("actor runtime is required for Spot actor operation");
        }
        return actors;
    }

    private Object currentSpotSurface(ZLinkActor actor) {
        return requireActors().currentSpot(actor);
    }
}
