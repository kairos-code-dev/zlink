package systems.zlink.framework.runtime.spots;

import java.time.Duration;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.function.BiFunction;
import java.util.function.Function;
import java.util.List;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.messaging.ZLinkMessage;
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
        ZLinkSpotActorJoinResponse response,
        List<Message> handoffReplies) {
    }

    private ZLinkActorRuntime actors;
    private final ZLinkPendingActorTransfers pendingTransfers =
        new ZLinkPendingActorTransfers();

    void attach(ZLinkActorRuntime actors) {
        this.actors = actors;
    }

    void traceTransferMarker(String marker, String actorId, long arrivalIndex) {
        requireActors().traceActorTransferMarker(marker, actorId, Long.toString(arrivalIndex));
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
        Function<String, CompletionStage<ZLinkSpotActorJoinResponse>> callback,
        Function<ZLinkActor, CompletionStage<Void>> joinedCallback) {
        return admitNativeActor(request, spotRid, null, callback, joinedCallback);
    }

    CompletionStage<ZLinkSpotActorJoinResponse> admitSpotActor(
        ZLinkBackendActorJoinRequest request,
        RoutingId spotRid,
        ZLinkSpot<?> spotSurface,
        Function<String, CompletionStage<ZLinkSpotActorJoinResponse>> callback,
        Function<ZLinkActor, CompletionStage<Void>> joinedCallback) {
        return admitNativeActor(request, spotRid, spotSurface, callback, joinedCallback);
    }

    private CompletionStage<ZLinkSpotActorJoinResponse> admitNativeActor(
        ZLinkBackendActorJoinRequest request,
        RoutingId spotRid,
        ZLinkSpot<?> spotSurface,
        Function<String, CompletionStage<ZLinkSpotActorJoinResponse>> callback,
        Function<ZLinkActor, CompletionStage<Void>> joinedCallback) {
        String actorId = request.targetActor().actorId();
        return invokeAdmissionCallback(callback, actorId)
            .thenCompose(response -> {
                ZLinkSpotActorJoinResponse effective = effectiveResponse(response);
                if (!effective.accepted()) {
                    return CompletableFuture.completedFuture(effective);
                }
                ZLinkActorRuntime runtime = requireActors();
                return runtime.getOrCreateLocalActor(actorId, ZLinkActor.class)
                    .thenCompose(actor -> actor
                        .map(value -> completeLocalAdmission(
                            value,
                            request.targetActor(),
                            spotRid,
                            spotSurface,
                            effective,
                            joinedCallback))
                        .orElseGet(() -> CompletableFuture.failedFuture(
                            new ZLinkConfigurationException(
                                "Spot actor join target actor is not available: " + actorId))));
            });
    }

    CompletionStage<ZLinkSpotActorJoinResponse> prepareRoutedActor(
        ZLinkActorSpotRoutePackets.TransferRequest request,
        String routeChannelName,
        RoutingId sourcePeerRid,
        Function<String, CompletionStage<ZLinkSpotActorJoinResponse>> callback) {
        if (!request.admission()) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "actor transfer admission request has the wrong phase"));
        }
        return invokeAdmissionCallback(callback, request.actorId())
            .thenApply(ZLinkActorSpotAdmission::effectiveResponse)
            .thenApply(response -> {
                if (!response.accepted()) {
                    return response;
                }
                pendingTransfers.add(request, routeChannelName, sourcePeerRid);
                return response;
            });
    }

    CompletionStage<RoutedJoin> commitRoutedActor(
        ZLinkActorSpotRoutePackets.TransferRequest request,
        ZLinkMessage transferState,
        ZLinkBackendSpotNode primaryNode,
        RoutingId spotRid,
        ZLinkSpot<?> spotSurface,
        Function<ZLinkActor, CompletionStage<Void>> joinedCallback,
        Function<ZLinkBackendActorRef, CompletionStage<List<Message>>> backlogReplay) {
        if (!request.commit()) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "actor transfer commit request has the wrong phase"));
        }
        ZLinkPendingActorTransfers.Admission pending;
        try {
            pending = pendingTransfers.take(request);
        } catch (RuntimeException error) {
            return CompletableFuture.failedFuture(error);
        }

        ZLinkActorRuntime runtime = requireActors();
        return runtime.materializeTransferredActor(
                request.actorId(),
                request.actorType(),
                request.adapterKey(),
                transferState)
            .thenCompose(actor -> {
                ZLinkBackendActorRef actorRef = runtime.actorRef(actor);
                long bindingToken = bindRoutedTransfer(
                    runtime,
                    actor,
                    actorRef,
                    pending,
                    primaryNode);
                runtime.markJoined(actor, actorRef, spotRid, spotSurface);
                return joinedCallback.apply(actor)
                    .thenCompose(ignored -> backlogReplay.apply(actorRef))
                    .thenCompose(replies -> runtime.commitJoinedLocation(actor, spotRid)
                        .thenApply(committed -> {
                            runtime.traceActorTransferMarker(
                                "location_committed", actor.actorId(), request.transferId());
                            return replies;
                        }))
                    .thenApply(replies -> new RoutedJoin(
                        completeRemoteMove(runtime, actor),
                        ZLinkSpotActorJoinResponse.accept(), replies))
                    .whenComplete((ignored, error) -> {
                        if (error != null) {
                            clearBinding(runtime, actor, bindingToken);
                            runtime.rollbackTransferredActor(actor);
                        }
                    });
            });
    }

    private static ZLinkBackendActorRef completeRemoteMove(
        ZLinkActorRuntime runtime,
        ZLinkActor actor) {
        runtime.completeRemoteMove(actor);
        return runtime.actorRef(actor);
    }

    private static long bindRoutedTransfer(
        ZLinkActorRuntime runtime,
        ZLinkActor actor,
        ZLinkBackendActorRef actorRef,
        ZLinkPendingActorTransfers.Admission pending,
        ZLinkBackendSpotNode primaryNode) {
        ZLinkActorSpotRoutePackets.TransferRequest request = pending.request();
        if (request.hasSourceSessionRoute()) {
            RoutingId sourceNodeRid = pending.sourcePeerRid() == null
                ? request.sourceNodeRid()
                : pending.sourcePeerRid();
            return runtime.bindNativeSession(
                actor,
                primaryNode,
                actorRef,
                sourceNodeRid,
                request.sourceSessionRid());
        }
        if (pending.routeChannelName() != null || pending.sourcePeerRid() != null) {
            return runtime.bindRoutedSession(
                actor,
                pending.routeChannelName(),
                pending.sourcePeerRid() == null
                    ? request.actorRef().nodeRid()
                    : pending.sourcePeerRid(),
                request.sourceEntrySpotRid(),
                request.actorRef());
        }
        return runtime.bindNativeSession(actor, primaryNode, actorRef);
    }

    private static CompletionStage<ZLinkSpotActorJoinResponse> invokeAdmissionCallback(
        Function<String, CompletionStage<ZLinkSpotActorJoinResponse>> callback,
        String actorId) {
        try {
            return callback.apply(actorId);
        } catch (RuntimeException error) {
            return CompletableFuture.failedFuture(error);
        }
    }

    private CompletionStage<ZLinkSpotActorJoinResponse> completeLocalAdmission(
        ZLinkActor actor,
        ZLinkBackendActorRef actorRef,
        RoutingId spotRid,
        ZLinkSpot<?> spotSurface,
        ZLinkSpotActorJoinResponse response,
        Function<ZLinkActor, CompletionStage<Void>> joinedCallback) {
        ZLinkActorRuntime runtime = requireActors();
        return runtime.leaveSourceForLocalMove(actor)
            .thenCompose(ignored -> {
                runtime.markJoined(actor, actorRef, spotRid, spotSurface);
                return joinedCallback.apply(actor);
            })
            .thenCompose(ignored -> runtime.commitJoinedLocation(actor, spotRid))
            .thenRun(() -> runtime.completeRemoteMove(actor))
            .thenApply(ignored -> response)
            .whenComplete((ignored, error) -> {
                if (error != null) {
                    runtime.failRemoteMove(actor, error);
                }
            });
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

    private ZLinkActorRuntime requireActors() {
        if (actors == null) {
            throw new ZLinkConfigurationException(
                "actor runtime is required for Spot actor admission");
        }
        return actors;
    }
}
