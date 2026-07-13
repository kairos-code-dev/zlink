package systems.zlink.e2e.spotservice.shared;

import java.time.Duration;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.spots.SpotHandleResolver;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse;
import systems.zlink.framework.spots.ZLinkSpotCreateResponse;

public final class MultiNodeSpot implements ZLinkSpot<ScenarioActor> {
    private final ZLinkSpotContext context;
    private final ScenarioState evidence;
    private final SpotHandleResolver spotHandles;
    private int value;

    public MultiNodeSpot(
        ZLinkSpotContext context,
        ScenarioState evidence,
        SpotHandleResolver spotHandles) {
        this.context = context;
        this.evidence = evidence;
        this.spotHandles = spotHandles;
    }

    @Override
    public ZLinkSpotContext context() {
        return context;
    }

    @Override
    public void configure() {
        context.handlers().addHandler(MultiNodeStateReqHandler.class);
        context.handlers().addHandler(MultiNodeStateMsgHandler.class);
    }

    @Override
    public java.util.concurrent.CompletionStage<ZLinkSpotCreateResponse> onCreate(ZLinkMessage request) {
        evidence.record("MultiNodeSpotCreated", context.spotRid().toString(), request.isEmpty() ? "" : "request");
        if (!request.isEmpty()) {
            Contracts.SpotOnlyMeshReq command = request.decode(Contracts.SpotOnlyMeshReq.class);
            var targetRid = systems.zlink.contracts.core.RoutingId.from(command.targetSpotRid());
            return spotHandles.resolveSpotHandle(targetRid)
                .thenApply(found -> found.orElseThrow(() ->
                    new IllegalStateException("target spot has no live address: " + command.targetSpotRid())))
                .thenCompose(target -> context.outbound()
                    .requestToSpot(target, new Contracts.MultiNodeStateReq(7))
                    .timeout(Duration.ofSeconds(5))
                    .submit(Contracts.MultiNodeStateRes.class)
                    .thenApply(reply -> {
                        context.outbound().sendToSpot(target,
                            new Contracts.MultiNodeStateMsg("sm-f6-send-" + command.marker())).submit();
                        evidence.record("SpotOnlyRequest", context.spotRid().toString(),
                            command.targetSpotRid() + "/" + reply.value() + "/" + command.marker());
                        return ZLinkSpotCreateResponse.accept();
                    }));
        }
        return java.util.concurrent.CompletableFuture.completedFuture(ZLinkSpotCreateResponse.accept());
    }

    @Override
    public java.util.concurrent.CompletionStage<Void> onInitialize() {
        evidence.record("MultiNodeSpotInitialized", context.spotRid().toString(), "");
        return java.util.concurrent.CompletableFuture.completedFuture(null);
    }

    @Override
    public java.util.concurrent.CompletionStage<ZLinkSpotActorJoinResponse> onActorJoin(
        String actorId,
        ZLinkMessage request) {
        return java.util.concurrent.CompletableFuture.completedFuture(ZLinkSpotActorJoinResponse.accept());
    }

    @Override
    public java.util.concurrent.CompletionStage<Void> onJoinedActor(ScenarioActor actor) {
        evidence.record("SpotOnlyActorJoined", context.spotRid().toString(), actor.actorId());
        return java.util.concurrent.CompletableFuture.completedFuture(null);
    }

    @Override
    public java.util.concurrent.CompletionStage<Void> onLeaveActor(ScenarioActor actor) {
        return java.util.concurrent.CompletableFuture.completedFuture(null);
    }

    public int add(int delta) {
        value += delta;
        evidence.record("MultiNodeStateReq", context.spotRid().toString(), Integer.toString(value));
        return value;
    }

    public void command(String marker) {
        evidence.record("MultiNodeStateMsg", context.spotRid().toString(), marker);
    }
}
