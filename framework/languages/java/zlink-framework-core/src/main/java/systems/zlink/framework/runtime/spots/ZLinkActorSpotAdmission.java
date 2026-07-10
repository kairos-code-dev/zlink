package systems.zlink.framework.runtime.spots;

import java.time.Duration;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.function.BiFunction;
import java.util.function.Function;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.runtime.actors.ZLinkActorRuntime;
import systems.zlink.framework.runtime.actors.ZLinkActorSpotRoutePackets;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorJoinRequest;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorRef;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpotNode;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse;

final class ZLinkActorSpotAdmission {
    record RoutedJoin(
        ZLinkBackendActorRef actorRef,
        ZLinkSpotActorJoinResponse response) {
    }

    private ZLinkActorRuntime actors;

    void attach(ZLinkActorRuntime actors) {
        this.actors = actors;
    }

    CompletionStage<Void> destroyFromEntry(RoutingId nodeRid, ZLinkActor actor) {
        return requireActors().destroyFromEntrySpot(nodeRid, actor);
    }

    CompletionStage<Void> leaveSpot(
        ZLinkBackendSpotNode node,
        ZLinkActor actor,
        RoutingId fallbackSpotRid,
        Duration timeout,
        BiFunction<ZLinkActor, RoutingId, CompletionStage<Void>> leftCallback) {
        ZLinkActorRuntime runtime = requireActors();
        RoutingId currentSpotRid = runtime.spotRid(actor).orElse(fallbackSpotRid);
        ZLinkBackendActorRef actorRef = runtime.actorRef(actor);
        node.leaveActor(actorRef, currentSpotRid, timeout)
            .whenComplete((replyParts, error) -> {
                if (replyParts != null) {
                    replyParts.forEach(Message::close);
                }
            });
        return runtime.submitActorDispatch(
            actor.actorId(),
            () -> runtime.markLeftAsync(actor)
                .thenCompose(ignored -> leftCallback.apply(actor, currentSpotRid)));
    }

    CompletionStage<Void> markLeft(ZLinkActor actor) {
        return requireActors().markLeftAsync(actor);
    }

    CompletionStage<Void> markJoined(
        ZLinkActor actor,
        ZLinkBackendActorRef actorRef,
        RoutingId spotRid,
        ZLinkSpot<?> spot) {
        return requireActors().markJoinedAsync(actor, actorRef, spotRid, spot);
    }

    CompletionStage<ZLinkSpotActorJoinResponse> admitEntryActor(
        ZLinkBackendActorJoinRequest request,
        RoutingId spotRid,
        Function<ZLinkActor, CompletionStage<ZLinkSpotActorJoinResponse>> callback,
        Function<ZLinkActor, CompletionStage<Void>> joinedCallback) {
        ZLinkActorRuntime runtime = requireActors();
        return runtime.getOrCreateLocalActor(request.targetActor().actorId(), ZLinkActor.class)
            .thenCompose(actor -> actor
                .map(value -> admitLocalActor(
                    value,
                    request.targetActor(),
                    spotRid,
                    null,
                    callback,
                    joinedCallback))
                .orElseGet(() -> CompletableFuture.failedFuture(
                    new ZLinkConfigurationException(
                        "Entry Spot actor join target actor is not available: "
                            + request.targetActor().actorId()))));
    }

    CompletionStage<ZLinkSpotActorJoinResponse> admitSpotActor(
        ZLinkBackendActorJoinRequest request,
        RoutingId spotRid,
        ZLinkSpot<?> spotSurface,
        Function<ZLinkActor, CompletionStage<ZLinkSpotActorJoinResponse>> callback,
        Function<ZLinkActor, CompletionStage<Void>> joinedCallback) {
        ZLinkActorRuntime runtime = requireActors();
        return runtime.getOrCreateLocalActor(request.targetActor().actorId(), ZLinkActor.class)
            .thenCompose(actor -> actor
                .map(value -> admitLocalActor(
                    value,
                    request.targetActor(),
                    spotRid,
                    spotSurface,
                    callback,
                    joinedCallback))
                .orElseGet(() -> CompletableFuture.failedFuture(
                    new ZLinkConfigurationException(
                        "Spot actor join callback target actor is not available: "
                            + request.targetActor().actorId()))));
    }

    CompletionStage<RoutedJoin> admitRoutedActor(
        ZLinkActorSpotRoutePackets.JoinRequest request,
        String routeChannelName,
        RoutingId sourcePeerRid,
        ZLinkBackendSpotNode primaryNode,
        RoutingId spotRid,
        ZLinkSpot<?> spotSurface,
        Function<ZLinkActor, CompletionStage<ZLinkSpotActorJoinResponse>> callback,
        Function<ZLinkActor, CompletionStage<Void>> joinedCallback) {
        ZLinkActorRuntime runtime = requireActors();
        return runtime.getOrCreateManagedActorWithoutLocationClaim(
                request.actorId(),
                request.actorType())
            .thenCompose(actor -> {
                ZLinkBackendActorRef actorRef = runtime.actorRef(actor);
                long bindingToken = bindRoutedJoin(
                    runtime,
                    actor,
                    actorRef,
                    request,
                    routeChannelName,
                    sourcePeerRid,
                    primaryNode);
                return invokeCallback(callback, actor)
                    .thenCompose(response -> completeRoutedAdmission(
                        runtime,
                        actor,
                        actorRef,
                        spotRid,
                        spotSurface,
                        bindingToken,
                        response,
                        joinedCallback))
                    .whenComplete((ignored, error) -> {
                        if (error != null) {
                            clearBinding(runtime, actor, bindingToken);
                        }
                    })
                    .thenApply(response -> new RoutedJoin(actorRef, response));
            });
    }

    private CompletionStage<ZLinkSpotActorJoinResponse> admitLocalActor(
        ZLinkActor actor,
        ZLinkBackendActorRef actorRef,
        RoutingId spotRid,
        ZLinkSpot<?> spotSurface,
        Function<ZLinkActor, CompletionStage<ZLinkSpotActorJoinResponse>> callback,
        Function<ZLinkActor, CompletionStage<Void>> joinedCallback) {
        ZLinkActorRuntime runtime = requireActors();
        return invokeCallback(callback, actor)
            .thenCompose(response -> {
                ZLinkSpotActorJoinResponse effective = effectiveResponse(response);
                if (!effective.accepted()) {
                    return CompletableFuture.completedFuture(effective);
                }
                return runtime.markJoinedAsync(actor, actorRef, spotRid, spotSurface)
                    .thenCompose(ignored -> joinedCallback.apply(actor))
                    .thenApply(ignored -> effective)
                    .whenComplete((ignored, error) -> {
                        if (error != null) {
                            runtime.markLeftAsync(actor);
                        }
                    });
            });
    }

    private CompletionStage<ZLinkSpotActorJoinResponse> completeRoutedAdmission(
        ZLinkActorRuntime runtime,
        ZLinkActor actor,
        ZLinkBackendActorRef actorRef,
        RoutingId spotRid,
        ZLinkSpot<?> spotSurface,
        long bindingToken,
        ZLinkSpotActorJoinResponse response,
        Function<ZLinkActor, CompletionStage<Void>> joinedCallback) {
        ZLinkSpotActorJoinResponse effective = effectiveResponse(response);
        if (!effective.accepted()) {
            clearBinding(runtime, actor, bindingToken);
            return CompletableFuture.completedFuture(effective);
        }
        return runtime.markJoinedAsync(actor, actorRef, spotRid, spotSurface)
            .thenCompose(ignored -> joinedCallback.apply(actor))
            .thenApply(ignored -> effective)
            .whenComplete((ignored, error) -> {
                if (error != null) {
                    runtime.markLeftAsync(actor);
                    clearBinding(runtime, actor, bindingToken);
                }
            });
    }

    private static long bindRoutedJoin(
        ZLinkActorRuntime runtime,
        ZLinkActor actor,
        ZLinkBackendActorRef actorRef,
        ZLinkActorSpotRoutePackets.JoinRequest request,
        String routeChannelName,
        RoutingId sourcePeerRid,
        ZLinkBackendSpotNode primaryNode) {
        if (request.hasSourceSessionRoute()) {
            RoutingId sourceNodeRid = sourcePeerRid == null
                ? request.sourceNodeRid()
                : sourcePeerRid;
            return runtime.bindNativeSession(
                actor,
                primaryNode,
                actorRef,
                sourceNodeRid,
                request.sourceSessionRid());
        }
        if (routeChannelName != null || sourcePeerRid != null) {
            return runtime.bindRoutedSession(
                actor,
                routeChannelName,
                sourcePeerRid == null ? request.actorRef().nodeRid() : sourcePeerRid,
                request.sourceEntrySpotRid(),
                request.actorRef());
        }
        return runtime.bindNativeSession(actor, primaryNode, actorRef);
    }

    private static void clearBinding(
        ZLinkActorRuntime runtime,
        ZLinkActor actor,
        long bindingToken) {
        if (bindingToken >= 0) {
            runtime.clearSessionBinding(actor, bindingToken);
        }
    }

    private static ZLinkSpotActorJoinResponse effectiveResponse(
        ZLinkSpotActorJoinResponse response) {
        return response == null ? ZLinkSpotActorJoinResponse.reject() : response;
    }

    private static CompletionStage<ZLinkSpotActorJoinResponse> invokeCallback(
        Function<ZLinkActor, CompletionStage<ZLinkSpotActorJoinResponse>> callback,
        ZLinkActor actor) {
        try {
            return callback.apply(actor);
        } catch (RuntimeException error) {
            return CompletableFuture.failedFuture(error);
        }
    }

    private ZLinkActorRuntime requireActors() {
        if (actors == null) {
            throw new ZLinkConfigurationException(
                "actor runtime is required for Spot actor admission");
        }
        return actors;
    }
}
