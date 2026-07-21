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
import systems.zlink.contracts.service.spot.ActorRef;
import systems.zlink.contracts.service.spot.ActorTransferId;
import systems.zlink.contracts.service.spot.ActorTransferPrepare;
import systems.zlink.contracts.service.spot.ActorTransferRole;
import systems.zlink.contracts.service.spot.PrepareActorTransferResult;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorJoinCall;
import systems.zlink.framework.actors.ZLinkActorJoinResult;
import systems.zlink.framework.configuration.ZLinkDispatchErrorSurface;
import systems.zlink.framework.configuration.ZLinkDispatchMessageKind;
import systems.zlink.framework.configuration.ZLinkMessageFlowEvent;
import systems.zlink.framework.configuration.ZLinkMessageFlowOutcome;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.errors.ZLinkFrameworkErrorKind;
import systems.zlink.framework.errors.ZLinkFrameworkException;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorJoinResult;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorRef;
import systems.zlink.framework.runtime.backend.ZLinkBackendRequestResult;
import systems.zlink.framework.runtime.internal.backend.ZLinkInternalSpotNode;
import systems.zlink.framework.runtime.channels.ZLinkChannelRuntime;
import systems.zlink.framework.runtime.diagnostics.ZLinkMessageFlowTracer;
import systems.zlink.framework.runtime.messaging.ZLinkFrameworkErrorReply;
import systems.zlink.framework.runtime.internal.spots.SpotTransportAddressResolver;
import systems.zlink.framework.runtime.internal.spots.SpotTransportAddress;
import systems.zlink.framework.spots.SpotHandle;
import systems.zlink.framework.spots.ZLinkSpot;

final class ZLinkActorSpotJoinCall implements ZLinkActorJoinCall {
    private final ZLinkActorRuntime.DefaultActorContext context;
    private final RoutingId spotRid;
    private final Message request;
    private final Duration timeout;
    private final Services services;
    private final String internalRouteChannel;
    private final RoutingId internalTargetNode;
    private final RoutingId explicitTargetNode;
    private final String explicitRouterChannelId;
    private final boolean entryTarget;

    ZLinkActorSpotJoinCall(
        ZLinkActorRuntime.DefaultActorContext context,
        RoutingId spotRid,
        Message request,
        Duration timeout,
        Services services) {
        this.context = context;
        this.spotRid = spotRid;
        this.request = request;
        this.timeout = timeout;
        this.services = services;
        this.internalRouteChannel = null;
        this.internalTargetNode = null;
        this.explicitTargetNode = null;
        this.explicitRouterChannelId = null;
        this.entryTarget = false;
    }

    ZLinkActorSpotJoinCall(
        ZLinkActorRuntime.DefaultActorContext context,
        RoutingId spotRid,
        Message request,
        Duration timeout,
        Services services,
        boolean entryTarget) {
        this.context = context;
        this.spotRid = spotRid;
        this.request = request;
        this.timeout = timeout;
        this.services = services;
        this.internalRouteChannel = null;
        this.internalTargetNode = null;
        this.explicitTargetNode = null;
        this.explicitRouterChannelId = null;
        this.entryTarget = entryTarget;
    }

    ZLinkActorSpotJoinCall(
        ZLinkActorRuntime.DefaultActorContext context,
        RoutingId spotRid,
        RoutingId targetNode,
        String routerChannelId,
        Message request,
        Duration timeout,
        Services services,
        boolean entryTarget) {
        this.context = context;
        this.spotRid = spotRid;
        this.request = request;
        this.timeout = timeout;
        this.services = services;
        this.internalRouteChannel = null;
        this.internalTargetNode = null;
        this.explicitTargetNode = targetNode;
        this.explicitRouterChannelId = routerChannelId;
        this.entryTarget = entryTarget;
    }

    ZLinkActorSpotJoinCall(
        ZLinkActorRuntime.DefaultActorContext context,
        String routeChannel,
        RoutingId targetNode,
        Message request,
        Duration timeout,
        Services services) {
        this.context = context;
        this.spotRid = targetNode;
        this.request = request;
        this.timeout = timeout;
        this.services = services;
        this.internalRouteChannel = routeChannel;
        this.internalTargetNode = targetNode;
        this.explicitTargetNode = targetNode;
        this.explicitRouterChannelId = routeChannel;
        this.entryTarget = true;
    }

    @Override
    public ZLinkActorJoinCall timeout(Duration timeout) {
        if (timeout == null || timeout.isNegative() || timeout.isZero()) {
            throw new ZLinkConfigurationException("timeout must be positive");
        }
        return internalRouteChannel == null
            ? explicitTargetNode == null
                ? new ZLinkActorSpotJoinCall(
                    context, spotRid, request, timeout, services, entryTarget)
                : new ZLinkActorSpotJoinCall(
                    context, spotRid, explicitTargetNode, explicitRouterChannelId,
                    request, timeout, services, entryTarget)
            : new ZLinkActorSpotJoinCall(
                context, internalRouteChannel, internalTargetNode, request, timeout, services);
    }

    @Override
    public CompletionStage<ZLinkActorJoinResult<Void>> submit() {
        traceJoinSent();
        Message requestPart = Message.from(request);
        ZLinkSpot<?> localSpot = services.spotResolver().apply(spotRid);
        if (localSpot == null && services.routedTransport() != null
            && (internalRouteChannel != null || services.remoteAddressResolver() != null)) {
            return manage(joinRemoteRoutedSpot(requestPart)
                .whenComplete((ignored, error) -> requestPart.close())
                .thenCompose(result -> applyCoreRemoteActorMigration(result)
                    .thenCompose(ignored -> decodeJoinResultAsync(result)))
                .whenComplete((r, e) -> traceJoinReplyReceived(e)));
        }
        CompletionStage<SpotTransportAddress> target =
            localSpot != null
                ? CompletableFuture.completedFuture(localAddress())
                : resolveRemoteAddress(spotRid);
        return manage(target.handle((address, error) -> {
                if (error != null) {
                    requestPart.close();
                    throw new CompletionException(error);
                }
                try {
                    return services.spotNode().joinActor(
                        context.actorRef(),
                        address.targetNodeRid(),
                        spotRid,
                        address.spotGeneration(),
                        List.of(requestPart),
                        timeout);
                } finally {
                    requestPart.close();
                }
            })
            .thenCompose(stage -> stage)
            .whenComplete((result, error) -> {
                if (localSpot != null && error != null) {
                    services.actors().cancelLocalJoin(context.actor());
                }
            })
            .thenCompose(result -> completeLocalJoin(localSpot, result)
                .thenCompose(ignored -> applyRemoteActorMigration(result))
                .thenCompose(ignored -> decodeJoinResultAsync(result)))
            .whenComplete((r, e) -> traceJoinReplyReceived(e)));
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
        if (localSpot == null && services.routedTransport() != null
            && (internalRouteChannel != null || services.remoteAddressResolver() != null)) {
            return manage(joinRemoteRoutedSpot(requestPart)
                .whenComplete((ignored, error) -> requestPart.close())
                .thenCompose(result -> applyCoreRemoteActorMigration(result)
                    .thenCompose(ignored -> decodeJoinResultAsync(result, replyType)))
                .whenComplete((r, e) -> traceJoinReplyReceived(e)));
        }
        CompletionStage<SpotTransportAddress> target =
            localSpot != null
                ? CompletableFuture.completedFuture(localAddress())
                : resolveRemoteAddress(spotRid);
        return manage(target.handle((address, error) -> {
                if (error != null) {
                    requestPart.close();
                    throw new CompletionException(error);
                }
                try {
                    return services.spotNode().joinActor(
                        context.actorRef(),
                        address.targetNodeRid(),
                        spotRid,
                        address.spotGeneration(),
                        List.of(requestPart),
                        timeout);
                } finally {
                    requestPart.close();
                }
            })
            .thenCompose(stage -> stage)
            .whenComplete((result, error) -> {
                if (localSpot != null && error != null) {
                    services.actors().cancelLocalJoin(context.actor());
                }
            })
            .thenCompose(result -> completeLocalJoin(localSpot, result)
                .thenCompose(ignored -> applyRemoteActorMigration(result))
                .thenCompose(ignored -> decodeJoinResultAsync(result, replyType)))
            .whenComplete((r, e) -> traceJoinReplyReceived(e)));
    }

    private static <T> CompletionStage<T> manage(CompletionStage<T> stage) {
        return systems.zlink.framework.execution.ZLinkAsyncSerialQueue.manageCurrent(stage);
    }

    private CompletionStage<Void> completeLocalJoin(
        ZLinkSpot<?> localSpot,
        ZLinkBackendActorJoinResult result) {
        if (localSpot == null || result.result() != ZLinkBackendRequestResult.OK
            || result.joinResultCode() != 0) {
            return CompletableFuture.completedFuture(null);
        }
        return services.actors().completeLocalJoinFromCaller(context.actor());
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
        requireJoinCompleted(result);
        ZLinkActorJoinResult<TReply> decoded = ZLinkActorJoinResults.withReply(
            services.serializer(),
            result.joinResultCode(),
            result.actor(),
            result.replyParts(),
            replyType);
        if (!entryTarget && decoded instanceof ZLinkActorJoinResult.Accepted<?>) {
            RoutingId joinedSpotRid = effectiveJoinedSpotRid(result);
            context.markJoined(result.actor(), joinedSpotRid, services.spotResolver().apply(joinedSpotRid));
        }
        return decoded;
    }

    private <TReply> CompletionStage<ZLinkActorJoinResult<TReply>> decodeJoinResultAsync(
        ZLinkBackendActorJoinResult result,
        Class<TReply> replyType) {
        ZLinkActorJoinResult<TReply> decoded = decodeJoinResult(result, replyType);
        if (decoded instanceof ZLinkActorJoinResult.Rejected<?>) {
            return CompletableFuture.completedFuture(decoded);
        }
        return entryTarget
            ? CompletableFuture.completedFuture(decoded)
            : services.locationRenewal().renew(context.actor(), context.joinedSpotRid())
                .thenApply(ignored -> decoded);
    }

    private ZLinkActorJoinResult<Void> decodeJoinResult(ZLinkBackendActorJoinResult result) {
        requireJoinCompleted(result);
        ZLinkActorJoinResult<Void> decoded = ZLinkActorJoinResults.withoutReply(
            result.joinResultCode(),
            result.actor(),
            result.replyParts());
        if (!entryTarget && decoded instanceof ZLinkActorJoinResult.Accepted<?>) {
            RoutingId joinedSpotRid = effectiveJoinedSpotRid(result);
            context.markJoined(result.actor(), joinedSpotRid, services.spotResolver().apply(joinedSpotRid));
        }
        return decoded;
    }

    private CompletionStage<ZLinkActorJoinResult<Void>> decodeJoinResultAsync(
        ZLinkBackendActorJoinResult result) {
        ZLinkActorJoinResult<Void> decoded = decodeJoinResult(result);
        if (decoded instanceof ZLinkActorJoinResult.Rejected<?>) {
            return CompletableFuture.completedFuture(decoded);
        }
        return entryTarget
            ? CompletableFuture.completedFuture(decoded)
            : services.locationRenewal().renew(context.actor(), context.joinedSpotRid())
                .thenApply(ignored -> decoded);
    }

    private void requireJoinCompleted(ZLinkBackendActorJoinResult result) {
        if (result.result() != ZLinkBackendRequestResult.OK) {
            throw new ZLinkConfigurationException(
                "actor spot join failed: " + result.result());
        }
    }

    private CompletionStage<Void> applyRemoteActorMigration(ZLinkBackendActorJoinResult result) {
        return applyRemoteActorMigration(result, false, false);
    }

    private CompletionStage<Void> applyCoreRemoteActorMigration(
        ZLinkBackendActorJoinResult result) {
        // Core source commit already retargets the STREAM binding. Rebinding
        // here would first unbind and destroy Core's transferred route.
        return applyRemoteActorMigration(result, true, true);
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
                if (entryTarget) {
                    context.markMovedToEntrySpot(result.actor(), joinedSpotRid);
                } else {
                    context.markJoined(
                        result.actor(), joinedSpotRid, services.spotResolver().apply(joinedSpotRid));
                }
                services.actors().abandonSourceLocationOwnership(context.actor());
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
        CompletionStage<SpotTransportAddress> resolved = internalRouteChannel == null
            ? explicitRouterChannelId != null && !explicitRouterChannelId.isBlank()
                ? CompletableFuture.completedFuture(new SpotTransportAddress(
                    explicitRouterChannelId,
                    explicitTargetNode,
                    spotRid,
                    0L,
                    systems.zlink.framework.spots.ZLinkSpotKind.ENTRY))
                : resolveHandle(services.remoteAddressResolver(), spotRid)
            .thenCompose(services.remoteAddressResolver()::resolve)
            .thenApply(address -> address.map(value -> explicitTargetNode == null
                ? value
                : new SpotTransportAddress(
                    value.routerChannelId(),
                    explicitTargetNode,
                    spotRid,
                    value.spotGeneration(),
                    value.spotKind())))
            .thenApply(address -> address.orElseThrow(() ->
                new ZLinkConfigurationException("SPOT transport address was not found: " + spotRid)))
            : CompletableFuture.completedFuture(new SpotTransportAddress(
                internalRouteChannel,
                internalTargetNode,
                internalTargetNode,
                0L,
                systems.zlink.framework.spots.ZLinkSpotKind.ENTRY));
        return resolved.thenCompose(target -> {
                ZLinkBackendActorRef currentActorRef = context.actorRef();
                String actorType = actorTypeOrEmpty(currentActorRef.actorId());
                String transferId = UUID.randomUUID().toString();
                List<Message> admissionParts = ZLinkActorSpotRoutePackets.createAdmissionRequestParts(
                    transferId,
                    timeout,
                    currentActorRef.actorId(),
                    actorType,
                    currentActorRef,
                    context.entrySpotNodeRid(),
                    context.entrySpotRid(),
                    entryRouterChannelId(target),
                    context.boundSessionSourceNodeRid(),
                    context.boundSessionSourceSessionRid(),
                    requestPart);
                try {
                    return requestTransfer(target, admissionParts)
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
                                    target,
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
        SpotTransportAddress address,
        String transferId,
        String actorType,
        ZLinkBackendActorRef currentActorRef,
        Message admissionReply) {
        ZLinkActor actor = context.actor();
        AtomicBoolean sourceLeft = new AtomicBoolean();
        AtomicReference<List<ZLinkActorHandoffPacket>> committedBacklog =
            new AtomicReference<>(List.of());
        UUID coreId = UUID.fromString(transferId);
        long membershipEpoch =
            services.spotNode().actorMembershipEpoch(currentActorRef.actorId());
        PrepareActorTransferResult corePrepared =
            services.spotNode().prepareActorTransfer(
                new ActorTransferPrepare(
                    ActorTransferRole.SOURCE,
                    new ActorTransferId(
                        coreId.getMostSignificantBits(),
                        coreId.getLeastSignificantBits()),
                    new ActorRef(
                        currentActorRef.nodeRid(),
                        currentActorRef.actorId(),
                        currentActorRef.generation()),
                    membershipEpoch,
                    address.targetNodeRid(),
                    0L,
                    0L,
                    0L),
                timeout);
        services.actors().traceActorTransferMarker(
            "core_source_prepared",
            currentActorRef.actorId(),
            Long.toUnsignedString(corePrepared.result().transferId().high())
                + ":" + Long.toUnsignedString(corePrepared.result().transferId().low())
                + ":" + Long.toUnsignedString(corePrepared.result().finalSequence()));
        return services.actors().beginRemoteMove(actor)
            .thenCompose(ignored -> services.actors().transferOut(actor))
            .thenCompose(transfer -> services.actors().leaveSourceForCoreRemoteMove(actor)
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
                    context.entrySpotNodeRid(),
                    context.entrySpotRid(),
                    entryRouterChannelId(address),
                    context.boundSessionSourceNodeRid(),
                    context.boundSessionSourceSessionRid(),
                    commit.transfer().adapterKey(),
                    transferState,
                    commit.backlog(),
                    new ZLinkActorSpotRoutePackets.CoreTransfer(
                        true,
                        corePrepared.result().transferId().high(),
                        corePrepared.result().transferId().low(),
                        membershipEpoch,
                        corePrepared.result().finalSequence(),
                        corePrepared.result().reserveMessageCount(),
                        corePrepared.result().reserveByteCount()));
                transferState.close();
                try {
                    return requestTransfer(address, commitParts).thenCompose(reply -> forwardLateBacklog(
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
            .thenApply(result -> {
                services.spotNode().commitActorTransfer(
                    corePrepared.token(), membershipEpoch + 1);
                return result;
            })
            .whenComplete((ignored, error) -> {
                if (error != null && !sourceLeft.get()) {
                    abortCoreTransfer(corePrepared);
                    services.actors().cancelRemoteMove(actor);
                } else if (error != null) {
                    abortCoreTransfer(corePrepared);
                    failPackets(committedBacklog.get(), error);
                    services.actors().failRemoteMove(actor, error);
                }
            });
    }

    private void abortCoreTransfer(PrepareActorTransferResult prepared) {
        try {
            services.spotNode().abortActorTransfer(prepared.token());
        } catch (RuntimeException ignored) {
            // A target that already activated cannot be rolled back through
            // the source token; authority reconciliation owns that terminal case.
        }
    }

    private CompletionStage<List<Message>> requestTransfer(
        SpotTransportAddress address,
        List<Message> parts) {
        if (internalRouteChannel == null) {
            return services.routedTransport().requestToSpotViaRouterChannel(
                address.routerChannelId(),
                address.targetNodeRid(),
                address.spotRid(),
                address.spotGeneration(),
                parts,
                timeout);
        }
        Message envelope = ZLinkActorEntryTransferEnvelope.encode(parts);
        return services.routedTransport().requestInternalToNode(
                address.routerChannelId(),
                address.targetNodeRid(),
                ZLinkActorEntryTransferEnvelope.PACKET_NAME,
                envelope,
                timeout)
            .thenApply(reply -> {
                try {
                    return ZLinkActorEntryTransferEnvelope.decode(reply);
                } finally {
                    reply.close();
                }
            })
            .whenComplete((ignored, error) -> envelope.close());
    }

    private String entryRouterChannelId(SpotTransportAddress target) {
        String existing = context.entryRouterChannelId();
        return existing == null || existing.isBlank()
            ? target.routerChannelId()
            : existing;
    }

    private CompletionStage<CommitReply> forwardLateBacklog(
        SpotTransportAddress address,
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
        SpotTransportAddress address,
        ZLinkBackendActorRef targetActorRef,
        ZLinkActorHandoffPacket packet) {
        List<Message> parts = ZLinkActorSpotRoutePackets.createActorPacketParts(
            targetActorRef,
            packet.header(),
            packet.payload(),
            null,
            packet.arrivalIndex());
        try {
            if (internalRouteChannel != null) {
                return requestTransfer(address, parts)
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
                            return null;
                        } finally {
                            if (replyParts != null) {
                                replyParts.forEach(Message::close);
                            }
                            if (error == null) {
                                packet.close();
                            }
                        }
                    });
            }
            return services.routedTransport().requestToSpotViaRouterChannel(
                    address.routerChannelId(),
                    address.targetNodeRid(),
                    address.spotRid(),
                    address.spotGeneration(),
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

    private CompletionStage<SpotTransportAddress> resolveRemoteAddress(RoutingId spotRid) {
        if (services.remoteAddressResolver() == null) {
            return CompletableFuture.completedFuture(localAddress());
        }
        return resolveHandle(services.remoteAddressResolver(), spotRid)
            .thenCompose(services.remoteAddressResolver()::resolve)
            .thenApply(address -> {
                if (address.isEmpty()
                    || address.get().targetNodeRid() == null
                    || address.get().spotGeneration() <= 0) {
                    throw new ZLinkConfigurationException(
                        "SPOT remote address resolver returned an incomplete owner snapshot: "
                            + spotRid);
                }
                return address.get();
            });
    }

    private SpotTransportAddress localAddress() {
        return new SpotTransportAddress(
            "",
            context.actorRef().nodeRid(),
            spotRid,
            0L,
            systems.zlink.framework.spots.ZLinkSpotKind.USER);
    }

    private static CompletionStage<SpotHandle> resolveHandle(
        SpotTransportAddressResolver resolver,
        RoutingId spotRid) {
        if (!(resolver instanceof systems.zlink.framework.spots.SpotHandleResolver handles)) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "SPOT transport resolver does not provide opaque handles"));
        }
        return handles.resolveSpotHandle(spotRid).thenApply(handle -> handle.orElseThrow(() ->
            new ZLinkConfigurationException("SPOT handle was not found: " + spotRid)));
    }

    @FunctionalInterface
    interface ActorJoinedLocationRenewal {
        CompletionStage<Void> renew(ZLinkActor actor, RoutingId spotRid);
    }

    record Services(
        ZLinkInternalSpotNode spotNode,
        Function<RoutingId, ZLinkSpot<?>> spotResolver,
        SpotTransportAddressResolver remoteAddressResolver,
        ZLinkChannelRuntime routedTransport,
        Function<String, String> actorTypes,
        ZLinkMessageSerializer serializer,
        ZLinkMessageFlowTracer flow,
        ZLinkActorRuntime actors,
        ActorJoinedLocationRenewal locationRenewal) {
    }
}
