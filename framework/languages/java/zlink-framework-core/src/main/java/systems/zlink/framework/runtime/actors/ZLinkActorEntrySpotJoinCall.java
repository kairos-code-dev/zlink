package systems.zlink.framework.runtime.actors;

import java.time.Duration;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorJoinCall;
import systems.zlink.framework.actors.ZLinkActorJoinResult;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.runtime.backend.ZLinkBackendRequestResult;
import systems.zlink.framework.runtime.internal.backend.ZLinkInternalSpotNode;

final class ZLinkActorEntrySpotJoinCall implements ZLinkActorJoinCall {
    private final ZLinkActorRuntime.DefaultActorContext context;
    private final RoutingId spotNodeRid;
    private final Message request;
    private final Duration timeout;
    private final Services services;

    ZLinkActorEntrySpotJoinCall(
        ZLinkActorRuntime.DefaultActorContext context,
        RoutingId spotNodeRid,
        Message request,
        Duration timeout,
        Services services) {
        this.context = context;
        this.spotNodeRid = spotNodeRid;
        this.request = request;
        this.timeout = timeout;
        this.services = services;
    }

    @Override
    public ZLinkActorJoinCall timeout(Duration timeout) {
        if (timeout == null || timeout.isNegative() || timeout.isZero()) {
            throw new ZLinkConfigurationException("timeout must be positive");
        }
        return new ZLinkActorEntrySpotJoinCall(
            context,
            spotNodeRid,
            request,
            timeout,
            services);
    }

    @Override
    public CompletionStage<ZLinkActorJoinResult<Void>> submit() {
        Message requestPart = Message.from(request);
        try {
            return manage(services.spotNode().joinActorEntrySpot(
                    context.actorRef(),
                    spotNodeRid,
                    requestPart,
                    timeout)
                .thenCompose(result -> {
                    if (result.result() != ZLinkBackendRequestResult.OK) {
                        throw new ZLinkConfigurationException(
                            "actor entry spot join failed: " + result.result());
                    }
                    if (result.joinResultCode() == 0) {
                        context.markMovedToEntrySpot(result.actor(), result.targetNodeRid());
                    }
                    ZLinkActorJoinResult<Void> decoded = ZLinkActorJoinResults.withoutReply(
                        result.joinResultCode(),
                        result.actor(),
                        result.replyParts());
                    return result.joinResultCode() == 0
                        ? services.locationRenewal().renew(context.actor(), result.targetNodeRid())
                            .thenApply(ignored -> decoded)
                        : CompletableFuture.completedFuture(decoded);
                }));
        } finally {
            requestPart.close();
        }
    }

    @Override
    public <TReply> CompletionStage<ZLinkActorJoinResult<TReply>> submit(Class<TReply> replyType) {
        if (replyType == null) {
            throw new ZLinkConfigurationException("replyType is required");
        }
        Message requestPart = Message.from(request);
        try {
            return manage(services.spotNode().joinActorEntrySpot(
                    context.actorRef(),
                    spotNodeRid,
                    requestPart,
                    timeout)
                .thenCompose(result -> {
                    if (result.result() != ZLinkBackendRequestResult.OK) {
                        throw new ZLinkConfigurationException(
                            "actor entry spot join failed: " + result.result());
                    }
                    if (result.joinResultCode() == 0) {
                        context.markMovedToEntrySpot(result.actor(), result.targetNodeRid());
                    }
                    ZLinkActorJoinResult<TReply> decoded = ZLinkActorJoinResults.withReply(
                        services.serializer(),
                        result.joinResultCode(),
                        result.actor(),
                        result.replyParts(),
                        replyType);
                    return result.joinResultCode() == 0
                        ? services.locationRenewal().renew(context.actor(), result.targetNodeRid())
                            .thenApply(ignored -> decoded)
                        : CompletableFuture.completedFuture(decoded);
                }));
        } finally {
            requestPart.close();
        }
    }

    private static <T> CompletionStage<T> manage(CompletionStage<T> stage) {
        return systems.zlink.framework.execution.ZLinkAsyncSerialQueue.manageCurrent(stage);
    }

    @FunctionalInterface
    interface ActorMovedToEntrySpotLocationRenewal {
        CompletionStage<Void> renew(ZLinkActor actor, RoutingId nodeRid);
    }

    record Services(
        ZLinkInternalSpotNode spotNode,
        ZLinkMessageSerializer serializer,
        ActorMovedToEntrySpotLocationRenewal locationRenewal) {
    }
}
