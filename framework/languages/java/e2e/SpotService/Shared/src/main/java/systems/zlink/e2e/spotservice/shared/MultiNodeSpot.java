package systems.zlink.e2e.spotservice.shared;

import java.time.Duration;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.locations.SpotRef;
import systems.zlink.framework.locations.SpotRefResolver;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse;
import systems.zlink.framework.spots.ZLinkSpotCreateResponse;
import systems.zlink.framework.CancellationToken;

public final class MultiNodeSpot implements ZLinkSpot<ScenarioActor> {
    private final ZLinkSpotContext context;
    private final ScenarioState evidence;
    private final SpotRefResolver spotRefs;
    private int value;

    public MultiNodeSpot(
        ZLinkSpotContext context,
        ScenarioState evidence,
        SpotRefResolver spotRefs) {
        this.context = context;
        this.evidence = evidence;
        this.spotRefs = spotRefs;
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
    public ZLinkSpotCreateResponse onCreate(ZLinkMessage request) {
        evidence.record("MultiNodeSpotCreated", context.spotRid().toString(), request.isEmpty() ? "" : "request");
        if (!request.isEmpty()) {
            Contracts.SpotOnlyMeshReq command = request.decode(Contracts.SpotOnlyMeshReq.class);
            SpotRef target = spotRefs.resolveSpotRefAsync(
                    Contracts.SPOT_MESH,
                    systems.zlink.contracts.core.RoutingId.from(command.targetSpotRid()))
                .toCompletableFuture()
                .join();
            if (target == null) {
                throw new IllegalStateException("target spot has no live address: " + command.targetSpotRid());
            }
            Contracts.MultiNodeStateRes reply = context.outbound()
                .requestToSpot(target, new Contracts.MultiNodeStateReq(7))
                .packetName("MultiNodeStateReq")
                .timeout(Duration.ofSeconds(5))
                .await(Contracts.MultiNodeStateRes.class);
            context.outbound()
                .sendToSpot(target, new Contracts.MultiNodeStateMsg("sm-f6-send-" + command.marker()))
                .packetName("MultiNodeStateMsg")
                .await();
            evidence.record(
                "SpotOnlyRequest",
                context.spotRid().toString(),
                command.targetSpotRid() + "/" + reply.value() + "/" + command.marker());
        }
        return ZLinkSpotCreateResponse.accept();
    }

    @Override
    public void onInitialize() {
        evidence.record("MultiNodeSpotInitialized", context.spotRid().toString(), "");
    }

    @Override
    public ZLinkSpotActorJoinResponse onActorJoin(
        ScenarioActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken) {
        evidence.record("SpotOnlyActorJoined", context.spotRid().toString(), actor.actorId());
        return ZLinkSpotActorJoinResponse.accept();
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
