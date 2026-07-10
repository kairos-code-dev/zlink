package systems.zlink.framework.runtime.actors;

import java.time.Duration;
import java.util.List;
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
import systems.zlink.framework.spots.SpotRemoteRefResolver;
import systems.zlink.framework.spots.ZLinkSpot;

final class ZLinkActorSpotJoinCall implements ZLinkActorJoinSpotCall {
    private static final String FRAMEWORK_ERROR_REPLY_MARKER = "ZLinkFrameworkError";

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
                .thenCompose(result -> applyRemoteActorMigration(result)
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
                .thenCompose(result -> applyRemoteActorMigration(result)
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
        if (result.result() != ZLinkBackendRequestResult.OK
            || result.joinResultCode() != 0
            || result.actor() == null
            || context.actorRef() == null
            || result.actor().equals(context.actorRef())) {
            return CompletableFuture.completedFuture(null);
        }
        return context.rebindNativeActor(result.actor(), timeout)
            .thenRun(() -> {
                RoutingId joinedSpotRid = effectiveJoinedSpotRid(result);
                context.markJoined(result.actor(), joinedSpotRid, services.spotResolver().apply(joinedSpotRid));
            });
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
                List<Message> joinParts = ZLinkActorSpotRoutePackets.createJoinRequestParts(
                    currentActorRef.actorId(),
                    actorTypeOrEmpty(currentActorRef.actorId()),
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
                        joinParts,
                        timeout);
                } finally {
                    joinParts.forEach(Message::close);
                }
            })
            .thenApply(replyParts -> {
                try {
                    if (replyParts.isEmpty()) {
                        throw new ZLinkConfigurationException(
                            "remote actor Spot join reply was empty: " + spotRid);
                    }
                    if (isFrameworkErrorReply(replyParts)) {
                        throw new ZLinkFrameworkException(
                            ZLinkFrameworkErrorKind.REQUEST_FAILED,
                            frameworkErrorReplyMessage(replyParts));
                    }
                    ZLinkActorSpotRoutePackets.JoinReply reply =
                        ZLinkActorSpotRoutePackets.decodeJoinReply(replyParts.get(0));
                    return new ZLinkBackendActorJoinResult(
                        ZLinkBackendRequestResult.OK,
                        reply.accepted() ? 0 : 1,
                        reply.actorRef(),
                        spotRid,
                        reply.actorRef().generation(),
                        0,
                        List.of(Message.from(reply.reply())));
                } finally {
                    replyParts.forEach(Message::close);
                }
            });
    }

    private String actorTypeOrEmpty(String actorId) {
        String actorType = services.actorTypes().apply(actorId);
        return actorType == null ? "" : actorType;
    }

    private boolean isFrameworkErrorReply(List<Message> parts) {
        return parts.size() >= 2
            && FRAMEWORK_ERROR_REPLY_MARKER.equals(parts.get(0).toUtf8String());
    }

    private String frameworkErrorReplyMessage(List<Message> parts) {
        return parts.get(1).toUtf8String();
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
        ActorJoinedLocationRenewal locationRenewal) {
    }
}
