package systems.zlink.framework.runtime.spots;

import java.time.Duration;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
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
import systems.zlink.framework.runtime.internal.backend.ZLinkInternalSpotNode;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse;

final class ZLinkActorSpotAdmission {
    record RoutedJoin(
        ZLinkBackendActorRef actorRef,
        ZLinkSpotActorJoinResponse response,
        List<Message> handoffReplies) {
    }

    private ZLinkActorRuntime actors;
    private java.util.function.BooleanSupplier draining = () -> false;
    private final ZLinkPendingActorTransfers pendingTransfers =
        new ZLinkPendingActorTransfers();
    private final java.util.concurrent.ConcurrentMap<String, CompletableFuture<Void>>
        pendingEntryJoins = new java.util.concurrent.ConcurrentHashMap<>();
    private final java.util.concurrent.ConcurrentMap<String, CompletableFuture<Void>>
        pendingLeaves = new java.util.concurrent.ConcurrentHashMap<>();

    void attach(
        ZLinkActorRuntime actors,
        java.util.function.BooleanSupplier draining) {
        this.actors = actors;
        this.draining = draining == null ? () -> false : draining;
    }

    void traceTransferMarker(String marker, String actorId, long arrivalIndex) {
        requireActors().traceActorTransferMarker(marker, actorId, Long.toString(arrivalIndex));
    }

    CompletionStage<Void> destroyFromEntry(RoutingId nodeRid, ZLinkActor actor) {
        return requireActors().destroyFromEntrySpot(nodeRid, actor);
    }

    CompletionStage<Void> leaveSpot(
        ZLinkInternalSpotNode node,
        ZLinkActor actor,
        RoutingId fallbackSpotRid,
        RoutingId entryNodeRid,
        Duration timeout) {
        ZLinkActorRuntime runtime = requireActors();
        RoutingId currentSpotRid = runtime.spotRid(actor).orElse(fallbackSpotRid);
        ZLinkBackendActorRef actorRef = runtime.actorRef(actor);
        CompletableFuture<Void> entryJoined = entryNodeRid == null
            ? null
            : new CompletableFuture<>();
        CompletableFuture<Void> actorLeft = new CompletableFuture<>();
        if (entryJoined != null) {
            CompletableFuture<Void> previous = pendingEntryJoins.putIfAbsent(
                actor.actorId(), entryJoined);
            if (previous != null) {
                return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                    "actor Entry Spot join is already pending: " + actor.actorId()));
            }
        }
        CompletableFuture<Void> previousLeave = pendingLeaves.putIfAbsent(
            actor.actorId(), actorLeft);
        if (previousLeave != null) {
            if (entryJoined != null) {
                pendingEntryJoins.remove(actor.actorId(), entryJoined);
            }
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "actor leave is already pending: " + actor.actorId()));
        }
        CompletionStage<Void> leaving = node.leaveActor(actorRef, currentSpotRid, timeout)
            .whenComplete((replyParts, error) -> {
                if (replyParts != null) {
                    replyParts.forEach(Message::close);
                }
                if (error != null && entryJoined != null
                    && pendingEntryJoins.remove(actor.actorId(), entryJoined)) {
                    entryJoined.completeExceptionally(error);
                }
                if (error != null && pendingLeaves.remove(actor.actorId(), actorLeft)) {
                    actorLeft.completeExceptionally(error);
                }
            })
            .thenCompose(ignored -> actorLeft)
            .thenCompose(ignored -> entryNodeRid == null
                ? CompletableFuture.completedFuture(null)
                : runtime.joinEntrySpot(actor, entryNodeRid, timeout)
                    .thenCompose(joined -> entryJoined));
        return systems.zlink.framework.execution.ZLinkAsyncSerialQueue.manageCurrent(leaving);
    }

    CompletionStage<Void> markLeft(ZLinkActor actor) {
        return requireActors().markLeft(actor);
    }

    CompletionStage<Void> leaveRoutedActorToLocalEntry(
        ZLinkActor actor,
        RoutingId entryNodeRid,
        Function<String, CompletionStage<ZLinkSpotActorJoinResponse>> admissionCallback,
        Function<ZLinkActor, CompletionStage<Void>> joinedCallback) {
        ZLinkActorRuntime runtime = requireActors();
        return invokeAdmissionCallback(admissionCallback, actor.actorId())
            .thenCompose(response -> effectiveResponse(response).accepted()
                ? runtime.leaveSourceForLocalMove(actor)
                : CompletableFuture.failedFuture(new ZLinkConfigurationException(
                    "actor Entry Spot join was rejected: " + actor.actorId())))
            .thenCompose(ignored -> runtime.commitEntryLocation(actor, entryNodeRid))
            .thenRun(() -> runtime.completeRemoteMove(actor))
            .thenCompose(ignored -> joinedCallback.apply(actor));
    }

    CompletionStage<Void> markJoined(
        ZLinkActor actor,
        ZLinkBackendActorRef actorRef,
        RoutingId spotRid,
        ZLinkSpot<?> spot) {
        return requireActors().markJoined(actor, actorRef, spotRid, spot);
    }

    CompletionStage<ZLinkSpotActorJoinResponse> admitEntryActor(
        ZLinkBackendActorJoinRequest request,
        RoutingId spotRid,
        Function<String, CompletionStage<ZLinkSpotActorJoinResponse>> callback) {
        if (draining.getAsBoolean()) {
            return CompletableFuture.completedFuture(ZLinkSpotActorJoinResponse.reject());
        }
        String actorId = request.targetActor().actorId();
        return invokeAdmissionCallback(callback, actorId)
            .thenApply(ZLinkActorSpotAdmission::effectiveResponse)
            .whenComplete((response, error) -> {
                if (error != null || response == null || !response.accepted()) {
                    completeEntryJoin(actorId, error == null
                        ? new ZLinkConfigurationException(
                            "actor Entry Spot join was rejected: " + actorId)
                        : error);
                }
            });
    }

    CompletionStage<Void> completeEntryActorJoin(
        ZLinkBackendActorJoinRequest request,
        RoutingId spotRid,
        Function<ZLinkActor, CompletionStage<Void>> joinedCallback) {
        String actorId = request.targetActor().actorId();
        ZLinkActorRuntime runtime = requireActors();
        return runtime.getOrCreateLocalActor(actorId, ZLinkActor.class)
            .thenCompose(actor -> actor
                .map(value -> runtime.markJoined(
                        value, request.targetActor(), spotRid, null)
                    .thenCompose(ignored -> joinedCallback.apply(value)))
                .orElseGet(() -> CompletableFuture.failedFuture(
                    new ZLinkConfigurationException(
                        "Entry Spot actor is not available: " + actorId))))
            .whenComplete((ignored, error) -> completeEntryJoin(actorId, error));
    }

    void completeEntryJoin(String actorId, Throwable error) {
        CompletableFuture<Void> pending = pendingEntryJoins.remove(actorId);
        if (pending == null) {
            return;
        }
        if (error == null) {
            pending.complete(null);
        } else {
            pending.completeExceptionally(error);
        }
    }

    boolean isEntryJoinPending(String actorId) {
        return pendingEntryJoins.containsKey(actorId);
    }

    void completeLeave(String actorId, Throwable error) {
        CompletableFuture<Void> pending = pendingLeaves.remove(actorId);
        if (pending == null) {
            return;
        }
        if (error == null) {
            pending.complete(null);
        } else {
            pending.completeExceptionally(error);
        }
    }

    boolean isLeavePending(String actorId) {
        return pendingLeaves.containsKey(actorId);
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
        if (draining.getAsBoolean()) {
            return CompletableFuture.completedFuture(ZLinkSpotActorJoinResponse.reject());
        }
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
        if (draining.getAsBoolean()) {
            return CompletableFuture.completedFuture(ZLinkSpotActorJoinResponse.reject());
        }
        requireActors().traceActorTransferMarker(
            "target_admission_received", request.actorId(), request.transferId());
        return invokeAdmissionCallback(callback, request.actorId())
            .thenApply(ZLinkActorSpotAdmission::effectiveResponse)
            .thenApply(response -> {
                if (!response.accepted()) {
                    return response;
                }
                pendingTransfers.add(request, routeChannelName, sourcePeerRid);
                requireActors().traceActorTransferMarker(
                    "target_admission_accepted", request.actorId(), request.transferId());
                return response;
            });
    }

    CompletionStage<RoutedJoin> commitRoutedActor(
        ZLinkActorSpotRoutePackets.TransferRequest request,
        ZLinkMessage transferState,
        ZLinkInternalSpotNode primaryNode,
        RoutingId spotRid,
        Object spotSurface,
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
        runtime.traceActorTransferMarker(
            "target_commit_received", request.actorId(), request.transferId());
        return runtime.materializeTransferredActor(
                request.actorId(),
                request.actorType(),
                request.adapterKey(),
                transferState)
            .thenCompose(actor -> {
                runtime.setEntrySpotNodeRid(actor, request.sourceEntrySpotNodeRid());
                runtime.setEntrySpotRid(actor, request.sourceEntrySpotRid());
                runtime.setEntryRouterChannelId(actor, request.sourceEntryRouterChannelId());
                runtime.traceActorTransferMarker(
                    "target_materialized", actor.actorId(), request.transferId());
                ZLinkBackendActorRef actorRef = runtime.actorRef(actor);
                long bindingToken = bindRoutedTransfer(
                    runtime,
                    actor,
                    actorRef,
                    pending,
                    primaryNode);
                runtime.traceActorTransferMarker(
                    "target_session_bound", actor.actorId(), request.transferId());
                boolean entryTarget = spotSurface instanceof systems.zlink.framework.spots.ZLinkEntrySpot<?>;
                if (!entryTarget) {
                    runtime.markJoined(
                        actor,
                        actorRef,
                        spotRid,
                        (ZLinkSpot<?>) spotSurface);
                }
                return joinedCallback.apply(actor)
                    .thenRun(() -> runtime.traceActorTransferMarker(
                        "target_joined_callback", actor.actorId(), request.transferId()))
                    .thenCompose(ignored -> backlogReplay.apply(actorRef))
                    .thenApply(replies -> {
                        runtime.traceActorTransferMarker(
                            "target_backlog_replayed", actor.actorId(), request.transferId());
                        return replies;
                    })
                    .thenCompose(replies -> (entryTarget
                        ? runtime.commitEntryLocation(actor, primaryNode.routingId())
                        : runtime.commitJoinedLocation(actor, spotRid))
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
        ZLinkInternalSpotNode primaryNode) {
        ZLinkActorSpotRoutePackets.TransferRequest request = pending.request();
        if (request.hasSourceSessionRoute()) {
            return runtime.bindNativeSession(
                actor,
                primaryNode,
                actorRef,
                request.sourceNodeRid(),
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

    ZLinkActorRuntime runtime() {
        return requireActors();
    }
}
