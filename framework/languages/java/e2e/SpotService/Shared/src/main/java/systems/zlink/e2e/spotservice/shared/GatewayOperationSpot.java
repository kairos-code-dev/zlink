package systems.zlink.e2e.spotservice.shared;

import java.time.Duration;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.TimeUnit;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorClient;
import systems.zlink.framework.actors.ZLinkActorDirectory;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.spots.SpotHandle;
import systems.zlink.framework.spots.SpotHandleResolver;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;

public final class GatewayOperationSpot implements ZLinkSpot<ZLinkActor> {
    private static final CompletableFuture<GatewayOperationSpot> READY = new CompletableFuture<>();
    private final ZLinkSpotContext context;
    private final ZLinkRouteClient routes;
    private final ZLinkActorClient actors;
    private final ZLinkActorDirectory actorRefs;
    private final SpotHandleResolver spotHandles;

    public GatewayOperationSpot(
        ZLinkSpotContext context,
        ZLinkRouteClient routes,
        ZLinkActorClient actors,
        ZLinkActorDirectory actorRefs,
        SpotHandleResolver spotHandles) {
        this.context = context;
        this.routes = routes;
        this.actors = actors;
        this.actorRefs = actorRefs;
        this.spotHandles = spotHandles;
    }

    @Override
    public ZLinkSpotContext context() {
        return context;
    }

    @Override
    public CompletionStage<Void> onInitialize() {
        READY.complete(this);
        return CompletableFuture.completedFuture(null);
    }

    @Override
    public CompletionStage<Void> onJoinedActor(ZLinkActor actor) {
        return CompletableFuture.completedFuture(null);
    }

    @Override
    public CompletionStage<Void> onLeaveActor(ZLinkActor actor) {
        return CompletableFuture.completedFuture(null);
    }

    public static GatewayOperationSpot awaitReady() {
        try {
            return READY.get(10, TimeUnit.SECONDS);
        } catch (Exception error) {
            throw new IllegalStateException("gateway operation spot did not initialize", error);
        }
    }

    Contracts.StateRes requestState(Contracts.SpotStateOperation operation) {
        return context.outbound().requestToSpot(spot(operation.spotId()),
                new Contracts.StateReq(operation.value()))
            .timeout(Duration.ofMillis(operation.timeoutMilliseconds()))
            .submit(Contracts.StateRes.class).toCompletableFuture().join();
    }

    Contracts.StateRes requestSlow(Contracts.SpotStateOperation operation) {
        return context.outbound().requestToSpot(spot(operation.spotId()),
                new Contracts.SlowReq(operation.value()))
            .timeout(Duration.ofMillis(operation.timeoutMilliseconds()))
            .submit(Contracts.StateRes.class).toCompletableFuture().join();
    }

    Contracts.OutboundRes requestOutbound(Contracts.SpotValueOperation operation) {
        return context.outbound().requestToSpot(spot(operation.spotId()),
                new Contracts.OutboundReq(operation.value()))
            .timeout(Duration.ofSeconds(5))
            .submit(Contracts.OutboundRes.class).toCompletableFuture().join();
    }

    Contracts.OperationAccepted sendState(Contracts.SpotValueOperation operation) {
        context.outbound().sendToSpot(spot(operation.spotId()),
            new Contracts.StateMsg(operation.value())).submit();
        return new Contracts.OperationAccepted(true);
    }

    Contracts.OperationAccepted sendOutbound(Contracts.SpotValueOperation operation) {
        context.outbound().sendToSpot(spot(operation.spotId()),
            new Contracts.OutboundMsg(operation.value())).submit();
        return new Contracts.OperationAccepted(true);
    }

    Contracts.RouteRes requestRoute(Contracts.RouteOperation operation) {
        return routes.requestToNode(Contracts.ROUTE_CHANNEL, RoutingId.from(operation.nodeRid()),
                new Contracts.RouteReq(operation.value()))
            .timeout(Duration.ofMillis(operation.timeoutMilliseconds()))
            .submit(Contracts.RouteRes.class).toCompletableFuture().join();
    }

    Contracts.ActorPingRes requestActorPush(Contracts.ActorOperation operation) {
        var actor = actorRefs.find(operation.actorId()).thenApply(found -> found.orElseThrow(() ->
            new IllegalStateException("actor ref not found: " + operation.actorId())))
            .toCompletableFuture().join();
        return actors.requestToActor(actor, new Contracts.ActorPushReq(operation.value()))
            .timeout(Duration.ofMillis(operation.timeoutMilliseconds()))
            .submit(Contracts.ActorPingRes.class).toCompletableFuture().join();
    }

    private SpotHandle spot(String spotRid) {
        return spotHandles.resolveSpotHandle(RoutingId.from(spotRid)).toCompletableFuture().join()
            .orElseThrow(() -> new IllegalStateException("spot not found: " + spotRid));
    }
}
