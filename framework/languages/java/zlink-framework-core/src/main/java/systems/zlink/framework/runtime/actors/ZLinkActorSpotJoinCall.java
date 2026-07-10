package systems.zlink.framework.runtime.actors;

import java.time.Duration;
import java.util.List;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CompletionStage;
import java.util.function.Function;
import java.util.function.Supplier;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.ZLinkAwait;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorJoinResult;
import systems.zlink.framework.actors.ZLinkActorJoinSpotCall;
import systems.zlink.framework.configuration.ZLinkDispatchErrorSurface;
import systems.zlink.framework.configuration.ZLinkDispatchMessageKind;
import systems.zlink.framework.configuration.ZLinkMessageFlowEvent;
import systems.zlink.framework.configuration.ZLinkMessageFlowOutcome;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.errors.ZLinkFrameworkErrorKind;
import systems.zlink.framework.errors.ZLinkFrameworkException;
import systems.zlink.framework.execution.ZLinkFrameworkTurns;
import systems.zlink.framework.execution.ZLinkYieldTurn;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorJoinResult;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorRef;
import systems.zlink.framework.runtime.backend.ZLinkBackendRequestResult;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpotNode;
import systems.zlink.framework.runtime.channels.ZLinkChannelRuntime;
import systems.zlink.framework.runtime.diagnostics.ZLinkMessageFlowTracer;
import systems.zlink.framework.runtime.messaging.ZLinkFrameworkErrorReply;
import systems.zlink.framework.spots.SpotRemoteRefResolver;
import systems.zlink.framework.spots.SpotRemoteRef;
import systems.zlink.framework.spots.ZLinkSpot;

final class ZLinkActorSpotJoinCall implements ZLinkActorJoinSpotCall {
    private final ZLinkActorRuntime.DefaultActorContext context;
    private final RoutingId spotRid;
    private final Message request;
    private final Duration timeout;
    private final ZLinkYieldTurn turn;
    private final Services services;

    ZLinkActorSpotJoinCall(
        ZLinkActorRuntime.DefaultActorContext context,
        RoutingId spotRid,
        Message request,
        Duration timeout,
        Services services) {
        this(
            context,
            spotRid,
            request,
            timeout,
            ZLinkFrameworkTurns.captureCurrent(),
            services);
    }

    private ZLinkActorSpotJoinCall(
        ZLinkActorRuntime.DefaultActorContext context,
        RoutingId spotRid,
        Message request,
        Duration timeout,
        ZLinkYieldTurn turn,
        Services services) {
        this.context = context;
        this.spotRid = spotRid;
        this.request = request;
        this.timeout = timeout;
        this.turn = turn;
        this.services = services;
    }

    @Override
    public ZLinkActorJoinSpotCall timeout(Duration timeout) {
        if (timeout == null || timeout.isNegative() || timeout.isZero()) {
            throw new ZLinkConfigurationException("timeout must be positive");
        }
        return new ZLinkActorSpotJoinCall(
            context,
            spotRid,
            request,
            timeout,
            turn,
            services);
    }

    @Override
    public CompletionStage<ZLinkActorJoinResult<Void>> submit() {
        traceJoinSent();
        Message requestPart = Message.from(request);
        ZLinkSpot<?> localSpot = services.spotResolver().apply(spotRid);
        if (localSpot == null && services.remoteAddressResolver() != null && services.routedTransport() != null) {
            return joinRemoteRoutedSpot(requestPart)
                .whenComplete((ignored, error) -> requestPart.close())
                .thenCompose(result -> applyRemoteActorMigration(result, false, true)
                    .thenCompose(ignored -> decodeJoinResultAsync(result)))
                .whenComplete((r, e) -> traceJoinReplyReceived(e));
        }
        CompletionStage<RoutingId> targetNode =
            localSpot != null
                ? CompletableFuture.completedFuture(context.actorRef().nodeRid())
                : resolveRemoteTargetNode(spotRid);
        return targetNode.handle((nodeRid, error) -> {
                if (error != null) {
                    requestPart.close();
                    throw new CompletionException(error);
                }
                try {
                    return services.spotNode().joinActor(
                        context.actorRef(),
                        nodeRid,
                        spotRid,
                        List.of(requestPart),
                        timeout);
                } finally {
                    requestPart.close();
                }
            })
            .thenCompose(stage -> stage)
            .thenCompose(result -> applyRemoteActorMigration(result)
                .thenCompose(ignored -> decodeJoinResultAsync(result)))
            .whenComplete((r, e) -> traceJoinReplyReceived(e));
    }

    @Override
    public <TReply> CompletionStage<ZLinkActorJoinResult<TReply>> submit(
        Class<TReply> replyType) {
        if (replyType == null) {
            throw new ZLinkConfigurationException("replyType is required");
        }
        traceJoinSent();
        Message requestPart = Message.from(request);
        ZLinkSpot<?> localSpot = services.spotResolver().apply(spotRid);
        if (localSpot == null && services.remoteAddressResolver() != null && services.routedTransport() != null) {
            return joinRemoteRoutedSpot(requestPart)
                .whenComplete((ignored, error) -> requestPart.close())
                .thenCompose(result -> applyRemoteActorMigration(result, false, true)
                    .thenCompose(ignored -> decodeJoinResultAsync(result, replyType)))
                .whenComplete((r, e) -> traceJoinReplyReceived(e));
        }
        CompletionStage<RoutingId> targetNode =
            localSpot != null
                ? CompletableFuture.completedFuture(context.actorRef().nodeRid())
                : resolveRemoteTargetNode(spotRid);
        return targetNode.handle((nodeRid, error) -> {
                if (error != null) {
                    requestPart.close();
                    throw new CompletionException(error);
                }
                try {
                    return services.spotNode().joinActor(
                        context.actorRef(),
                        nodeRid,
                        spotRid,
                        List.of(requestPart),
                        timeout);
                } finally {
                    requestPart.close();
                }
            })
            .thenCompose(stage -> stage)
            .thenCompose(result -> applyRemoteActorMigration(result)
                .thenCompose(ignored -> decodeJoinResultAsync(result, replyType)))
            .whenComplete((r, e) -> traceJoinReplyReceived(e));
    }

    private void traceJoinSent() {
        if (services.flow() != null && services.flow().enabled(ZLinkMessageFlowOutcome.SENT)) {
            services.flow().trace(new ZLinkMessageFlowEvent(
                ZLinkMessageFlowOutcome.SENT,
                ZLinkDispatchErrorSurface.SPOT_ACTOR,
                ZLinkDispatchMessageKind.ACTOR_REQUEST,
                "JoinSpot", null, null, null, null,
                spotRid.toString(), context.actorRef().actorId(), null));
        }
    }

    private void traceJoinReplyReceived(Throwable error) {
        if (error == null
            && services.flow() != null
            && services.flow().enabled(ZLinkMessageFlowOutcome.REPLY_RECEIVED)) {
            services.flow().trace(new ZLinkMessageFlowEvent(
                ZLinkMessageFlowOutcome.REPLY_RECEIVED,
                ZLinkDispatchErrorSurface.SPOT_ACTOR,
                ZLinkDispatchMessageKind.RESPONSE,
                "JoinSpot", null, null, null, null,
                spotRid.toString(), context.actorRef().actorId(), null));
        }
    }

    private <TReply> ZLinkActorJoinResult<TReply> decodeJoinResult(
        ZLinkBackendActorJoinResult result,
        Class<TReply> replyType) {
        requireAcceptedJoin(result);
        RoutingId joinedSpotRid = effectiveJoinedSpotRid(result);
        context.markJoined(result.actor(), joinedSpotRid, services.spotResolver().apply(joinedSpotRid));
        return ZLinkActorJoinResults.withReply(
            services.serializer(),
            result.joinResultCode(),
            result.actor(),
            result.replyParts(),
            replyType);
    }

    private <TReply> CompletionStage<ZLinkActorJoinResult<TReply>> decodeJoinResultAsync(
        ZLinkBackendActorJoinResult result,
        Class<TReply> replyType) {
        ZLinkActorJoinResult<TReply> decoded = decodeJoinResult(result, replyType);
        return services.locationRenewal().renew(context.actor(), context.joinedSpotRid())
            .thenApply(ignored -> decoded);
    }

    private ZLinkActorJoinResult<Void> decodeJoinResult(ZLinkBackendActorJoinResult result) {
        requireAcceptedJoin(result);
        RoutingId joinedSpotRid = effectiveJoinedSpotRid(result);
        context.markJoined(result.actor(), joinedSpotRid, services.spotResolver().apply(joinedSpotRid));
        return ZLinkActorJoinResults.withoutReply(
            result.joinResultCode(),
            result.actor(),
            result.replyParts());
    }

    private CompletionStage<ZLinkActorJoinResult<Void>> decodeJoinResultAsync(
        ZLinkBackendActorJoinResult result) {
        ZLinkActorJoinResult<Void> decoded = decodeJoinResult(result);
        return services.locationRenewal().renew(context.actor(), context.joinedSpotRid())
            .thenApply(ignored -> decoded);
    }

    private void requireAcceptedJoin(ZLinkBackendActorJoinResult result) {
        if (result.result() != ZLinkBackendRequestResult.OK) {
            throw new ZLinkConfigurationException(
                "actor spot join failed: " + result.result());
        }
        if (result.joinResultCode() != 0) {
            throw new ZLinkConfigurationException(
                "actor spot join rejected: " + result.joinResultCode());
        }
    }

    private CompletionStage<Void> applyRemoteActorMigration(ZLinkBackendActorJoinResult result) {
        return applyRemoteActorMigration(result, false, false);
    }

    private CompletionStage<Void> applyRemoteActorMigration(
        ZLinkBackendActorJoinResult result,
        boolean sessionAlreadyRebound,
        boolean retainForwardingSource) {
        if (result.result() != ZLinkBackendRequestResult.OK
            || result.joinResultCode() != 0
            || result.actor() == null
            || context.actorRef() == null
            || result.actor().equals(context.actorRef())) {
            return CompletableFuture.completedFuture(null);
        }
        ZLinkBackendActorRef sourceActorRef = context.actorRef();
        CompletionStage<Void> rebound = sessionAlreadyRebound
            ? CompletableFuture.completedFuture(null)
            : context.rebindNativeActor(result.actor(), timeout);
        Supplier<CompletionStage<Void>> cleanup = () -> rebound
            .thenRun(() -> {
                RoutingId joinedSpotRid = effectiveJoinedSpotRid(result);
                context.markJoined(result.actor(), joinedSpotRid, services.spotResolver().apply(joinedSpotRid));
                services.actors().completeRemoteMove(context.actor());
                if (retainForwardingSource) {
                    services.actors().retainForwardingSource(
                        context.actor(), sourceActorRef, result.actor());
                }
            });
        if (retainForwardingSource && services.actors().isActorDispatchActive(context.actor())) {
            services.actors().continueAfterActorDispatch(context.actor(), cleanup);
            return CompletableFuture.completedFuture(null);
        }
        return cleanup.get();
    }

    private RoutingId effectiveJoinedSpotRid(ZLinkBackendActorJoinResult result) {
        RoutingId joinedSpotRid = result.joinedSpotRid();
        return joinedSpotRid == null || joinedSpotRid.toString().isBlank()
            ? spotRid
            : joinedSpotRid;
    }

    private CompletionStage<ZLinkBackendActorJoinResult> joinRemoteRoutedSpot(Message requestPart) {
        return services.remoteAddressResolver().resolveSpotRemoteRefAsync(spotRid)
            .thenCompose(address -> {
                ZLinkBackendActorRef currentActorRef = context.actorRef();
                String actorType = actorTypeOrEmpty(currentActorRef.actorId());
                String transferId = UUID.randomUUID().toString();
                List<Message> admissionParts = ZLinkActorSpotRoutePackets.createAdmissionRequestParts(
                    transferId,
                    timeout,
                    currentActorRef.actorId(),
                    actorType,
                    currentActorRef,
                    services.sourceEntrySpotRid().get(),
                    context.boundSessionSourceNodeRid(),
                    context.boundSessionSourceSessionRid(),
                    requestPart);
                try {
                    return services.routedTransport().requestToSpotViaRouterChannel(
                        address.routerChannelId(),
                        address.targetNodeRid(),
                        address.spotRid(),
                        admissionParts,
                        timeout)
                        .thenCompose(replyParts -> {
                            try {
                                if (replyParts.isEmpty()) {
                                    return CompletableFuture.failedFuture(
                                        new ZLinkConfigurationException(
                                            "remote actor Spot admission reply was empty: " + spotRid));
                                }
                                ZLinkActorSpotRoutePackets.AdmissionReply admission =
                                    ZLinkActorSpotRoutePackets.decodeAdmissionReply(replyParts.get(0));
                                if (!admission.accepted()) {
                                    return CompletableFuture.completedFuture(
                                        rejectedRemoteJoin(currentActorRef, admission.reply()));
                                }
                                return commitRemoteTransfer(
                                    address,
                                    transferId,
                                    actorType,
                                    currentActorRef,
                                    admission.reply());
                            } finally {
                                replyParts.forEach(Message::close);
                            }
                        });
                } finally {
                    admissionParts.forEach(Message::close);
                }
            });
    }

    private CompletionStage<ZLinkBackendActorJoinResult> commitRemoteTransfer(
        SpotRemoteRef address,
        String transferId,
        String actorType,
        ZLinkBackendActorRef currentActorRef,
        Message admissionReply) {
        ZLinkActor actor = context.actor();
        AtomicBoolean sourceLeft = new AtomicBoolean();
        AtomicReference<List<ZLinkActorHandoffPacket>> committedBacklog =
            new AtomicReference<>(List.of());
        return services.actors().beginRemoteMove(actor)
            .thenApply(ignored -> services.actors().transferOut(actor, () -> false))
            .thenCompose(transfer -> services.actors().leaveSourceForRemoteMove(actor)
                .thenApply(ignored -> {
                    sourceLeft.set(true);
                    List<ZLinkActorHandoffPacket> backlog =
                        services.actors().takeRemoteMoveBacklog(actor);
                    committedBacklog.set(backlog);
                    services.actors().traceActorTransferMarker(
                        "commit_request", actor.actorId(), transferId);
                    return new TransferCommit(transfer, backlog);
                }))
            .thenCompose(commit -> {
                Message transferState = Message.from(
                    commit.transfer().state().toEncodedPayload(services.serializer()).bytes());
                List<Message> commitParts = ZLinkActorSpotRoutePackets.createCommitRequestParts(
                    transferId,
                    timeout,
                    currentActorRef.actorId(),
                    actorType,
                    currentActorRef,
                    services.sourceEntrySpotRid().get(),
                    context.boundSessionSourceNodeRid(),
                    context.boundSessionSourceSessionRid(),
                    commit.transfer().adapterKey(),
                    transferState,
                    commit.backlog());
                transferState.close();
                try {
                    return services.routedTransport().requestToSpotViaRouterChannel(
                        address.routerChannelId(),
                        address.targetNodeRid(),
                        address.spotRid(),
                        commitParts,
                        timeout).thenCompose(reply -> forwardLateBacklog(
                            address, reply, commit.backlog()));
                } finally {
                    commitParts.forEach(Message::close);
                }
            })
            .handle((commitReply, error) -> {
                if (error != null) {
                    admissionReply.close();
                    throw new CompletionException(error);
                }
                return decodeCommitReply(
                    commitReply.parts(), admissionReply, commitReply.backlog());
            })
            .whenComplete((ignored, error) -> {
                if (error != null && !sourceLeft.get()) {
                    services.actors().cancelRemoteMove(actor);
                } else if (error != null) {
                    failPackets(committedBacklog.get(), error);
                    services.actors().failRemoteMove(actor, error);
                }
            });
    }

    private CompletionStage<CommitReply> forwardLateBacklog(
        SpotRemoteRef address,
        List<Message> commitReplyParts,
        List<ZLinkActorHandoffPacket> committedBacklog) {
        if (commitReplyParts.isEmpty()) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "remote actor Spot commit reply was empty: " + spotRid));
        }
        ZLinkActorSpotRoutePackets.JoinReply joinReply =
            ZLinkActorSpotRoutePackets.decodeJoinReply(commitReplyParts.get(0));
        List<ZLinkActorHandoffPacket> lateBacklog =
            services.actors().finishRemoteMoveBacklog(context.actor());
        CompletionStage<Void> tail = CompletableFuture.completedFuture(null);
        for (ZLinkActorHandoffPacket packet : lateBacklog) {
            tail = tail.thenCompose(ignored -> forwardHandoffPacket(
                address, joinReply.actorRef(), packet));
        }
        joinReply.reply().close();
        return tail.thenApply(ignored -> new CommitReply(commitReplyParts, committedBacklog));
    }

    private CompletionStage<Void> forwardHandoffPacket(
        SpotRemoteRef address,
        ZLinkBackendActorRef targetActorRef,
        ZLinkActorHandoffPacket packet) {
        List<Message> parts = ZLinkActorSpotRoutePackets.createActorPacketParts(
            targetActorRef, packet.header(), packet.payload(), packet.replyRoute());
        try {
            if (packet.replyRoute() != null) {
                return services.routedTransport().sendToSpotViaRouterChannel(
                        address.routerChannelId(),
                        address.targetNodeRid(),
                        address.spotRid(),
                        parts)
                    .thenRun(() -> {
                        packet.complete(java.util.Optional.empty());
                        packet.close();
                    })
                    .exceptionallyCompose(error -> {
                        if (packet.fail(error)) {
                            packet.close();
                        }
                        return CompletableFuture.failedFuture(error);
                    });
            }
            return services.routedTransport().requestToSpotViaRouterChannel(
                    address.routerChannelId(),
                    address.targetNodeRid(),
                    address.spotRid(),
                    parts,
                    timeout)
                .handle((replyParts, error) -> {
                    try {
                        if (error != null) {
                            if (packet.fail(error)) {
                                packet.close();
                            }
                            throw new CompletionException(error);
                        }
                        packet.complete(replyParts.isEmpty()
                            || ZLinkActorSpotRoutePackets.isHandoffDirectReplyAck(replyParts.get(0))
                            ? java.util.Optional.empty()
                            : java.util.Optional.of(Message.from(replyParts.get(0))));
                    } finally {
                        if (replyParts != null) {
                            replyParts.forEach(Message::close);
                        }
                        if (error == null) {
                            packet.close();
                        }
                    }
                    return null;
                });
        } finally {
            parts.forEach(Message::close);
        }
    }

    private ZLinkBackendActorJoinResult decodeCommitReply(
        List<Message> replyParts,
        Message admissionReply,
        List<ZLinkActorHandoffPacket> backlog) {
        try {
            if (replyParts.isEmpty()) {
                throw new ZLinkConfigurationException(
                    "remote actor Spot commit reply was empty: " + spotRid);
            }
            if (isFrameworkErrorReply(replyParts)) {
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.REQUEST_FAILED,
                    frameworkErrorReplyMessage(replyParts));
            }
            ZLinkActorSpotRoutePackets.JoinReply reply =
                ZLinkActorSpotRoutePackets.decodeJoinReply(replyParts.get(0));
            if (replyParts.size() != backlog.size() + 1) {
                throw new ZLinkConfigurationException(
                    "remote actor Spot handoff reply count did not match backlog");
            }
            for (int index = 0; index < backlog.size(); index++) {
                ZLinkActorHandoffPacket packet = backlog.get(index);
                Message response = replyParts.get(index + 1);
                packet.complete(response.toByteArray().length == 0
                    ? java.util.Optional.empty()
                    : java.util.Optional.of(Message.from(response)));
                packet.close();
            }
            Message commitReply = reply.reply();
            try (commitReply) {
                return new ZLinkBackendActorJoinResult(
                    ZLinkBackendRequestResult.OK,
                    reply.accepted() ? 0 : 1,
                    reply.actorRef(),
                    spotRid,
                    reply.actorRef().generation(),
                    0,
                    List.of(Message.from(admissionReply)));
            }
        } finally {
            admissionReply.close();
            replyParts.forEach(Message::close);
        }
    }

    private record TransferCommit(
        ZLinkActorTransferRegistry.TransferState transfer,
        List<ZLinkActorHandoffPacket> backlog) {
    }

    private static void failPackets(
        List<ZLinkActorHandoffPacket> packets,
        Throwable error) {
        packets.forEach(packet -> {
            if (packet.fail(error)) {
                packet.close();
            }
        });
    }

    private record CommitReply(
        List<Message> parts,
        List<ZLinkActorHandoffPacket> backlog) {
    }

    private ZLinkBackendActorJoinResult rejectedRemoteJoin(
        ZLinkBackendActorRef currentActorRef,
        Message reply) {
        try (reply) {
            return new ZLinkBackendActorJoinResult(
                ZLinkBackendRequestResult.OK,
                1,
                currentActorRef,
                spotRid,
                currentActorRef.generation(),
                0,
                List.of(Message.from(reply)));
        }
    }

    private String actorTypeOrEmpty(String actorId) {
        String actorType = services.actorTypes().apply(actorId);
        return actorType == null ? "" : actorType;
    }

    private boolean isFrameworkErrorReply(List<Message> parts) {
        return ZLinkFrameworkErrorReply.isReply(parts);
    }

    private String frameworkErrorReplyMessage(List<Message> parts) {
        return ZLinkFrameworkErrorReply.message(parts);
    }

    private CompletionStage<RoutingId> resolveRemoteTargetNode(RoutingId spotRid) {
        if (services.remoteAddressResolver() == null) {
            return CompletableFuture.completedFuture(context.actorRef().nodeRid());
        }
        return services.remoteAddressResolver().resolveSpotRemoteRefAsync(spotRid)
            .thenApply(address -> {
                if (address == null || address.targetNodeRid() == null) {
                    throw new ZLinkConfigurationException(
                        "SPOT remote address resolver returned no target node: " + spotRid);
                }
                return address.targetNodeRid();
            });
    }

    @Override
    public ZLinkActorJoinResult<Void> yield() {
        return ZLinkAwait.await(
            ZLinkFrameworkTurns.awaitManagedCompletion(requireTurn(), submit()));
    }

    @Override
    public ZLinkActorJoinResult<Void> yield(CancellationToken cancellationToken) {
        ZLinkFrameworkTurns.throwIfCancellationRequested(cancellationToken);
        return ZLinkAwait.await(
            ZLinkFrameworkTurns.awaitManagedCompletion(requireTurn(), submit(), cancellationToken));
    }

    @Override
    public <TReply> ZLinkActorJoinResult<TReply> yield(Class<TReply> replyType) {
        return ZLinkAwait.await(
            ZLinkFrameworkTurns.awaitManagedCompletion(requireTurn(), submit(replyType)));
    }

    @Override
    public <TReply> ZLinkActorJoinResult<TReply> yield(
        Class<TReply> replyType,
        CancellationToken cancellationToken) {
        ZLinkFrameworkTurns.throwIfCancellationRequested(cancellationToken);
        return ZLinkAwait.await(
            ZLinkFrameworkTurns.awaitManagedCompletion(requireTurn(), submit(replyType), cancellationToken));
    }

    private ZLinkYieldTurn requireTurn() {
        if (turn == null) {
            ZLinkYieldTurn current = ZLinkFrameworkTurns.captureCurrent();
            if (current != null) {
                return current;
            }
            throw new IllegalStateException(
                "yield requires a framework Spot handler turn captured when the call object was created");
        }
        return turn;
    }

    @FunctionalInterface
    interface ActorJoinedLocationRenewal {
        CompletionStage<Void> renew(ZLinkActor actor, RoutingId spotRid);
    }

    record Services(
        ZLinkBackendSpotNode spotNode,
        Function<RoutingId, ZLinkSpot<?>> spotResolver,
        SpotRemoteRefResolver remoteAddressResolver,
        ZLinkChannelRuntime routedTransport,
        Supplier<RoutingId> sourceEntrySpotRid,
        Function<String, String> actorTypes,
        ZLinkMessageSerializer serializer,
        ZLinkMessageFlowTracer flow,
        ZLinkActorRuntime actors,
        ActorJoinedLocationRenewal locationRenewal) {
    }
}
