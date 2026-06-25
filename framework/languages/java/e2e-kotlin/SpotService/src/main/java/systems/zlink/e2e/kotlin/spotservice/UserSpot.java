package systems.zlink.e2e.kotlin.spotservice;

import java.time.Duration;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.spots.ZLinkSpotCreateResponse;
import systems.zlink.framework.spots.ZLinkTimerOptions;

public final class UserSpot implements ZLinkSpot<ScenarioActor> {
    private final ZLinkSpotContext context;
    private final ScenarioState evidence;
    private String state = "";
    private boolean workerDone = true;

    public UserSpot(
        ZLinkSpotContext context,
        ScenarioState evidence) {
        this.context = context;
        this.evidence = evidence;
    }

    @Override
    public ZLinkSpotContext context() {
        return context;
    }

    @Override
    public void configure() {
        context.handlers().addPacket(StateRequestHandler.class);
        context.handlers().addPacket(StateCommandHandler.class);
        context.handlers().addPacket(SlowRequestHandler.class);
        context.handlers().addPacket(OutboundRequestHandler.class);
        context.handlers().addPacket(OutboundCommandHandler.class);
        context.handlers().addSubscribe("spot.events", SpotEventHandler.class);
        context.handlers().addActorRequest(UserActorEchoHandler.class);
    }

    @Override
    public ZLinkSpotCreateResponse onCreate(ZLinkMessage request) {
        evidence.record("SpotCreated", context.spotRid().toString(), request.isEmpty() ? "" : "request");
        context.addTimer("state-timer", Duration.ofSeconds(2),
            StateTimerHandler.class, new ZLinkTimerOptions());
        return ZLinkSpotCreateResponse.accept();
    }

    @Override
    public void onInitialize() {
        evidence.record("SpotInitialized", context.spotRid().toString(), "");
    }

    @Override
    public void onClosing() {
        evidence.record("SpotClosing", context.spotRid().toString(), state);
    }

    @Override
    public ZLinkSpotActorJoinResponse onActorJoin(
        ScenarioActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken) {
        Contracts.ActorJoinRequest join = request.decode(Contracts.ActorJoinRequest.class);
        actor.applyProfile(join.profile());
        evidence.record("ActorUserJoinRequested", context.spotRid().toString(),
            actor.actorId() + "/" + join.profile().displayName() + "/" + String.join(",", join.tags()));
        return ZLinkSpotActorJoinResponse.accept(new Contracts.ActorJoinReply(
            actor.actorId(),
            context.spotRid().toString(),
            evidence.nodeRid(),
            join.profile().displayName(),
            join.profile().level(),
            join.tags()));
    }

    @Override
    public void onJoinedActor(
        ScenarioActor actor,
        CancellationToken cancellationToken) {
        evidence.record("ActorUserJoined", context.spotRid().toString(),
            actor.actorId() + "#" + actor.nextSequence());
    }

    @Override
    public void onLeaveActor(
        ScenarioActor actor,
        CancellationToken cancellationToken) {
        evidence.record("ActorUserLeft", context.spotRid().toString(), actor.actorId());
    }

    @Override
    public void onDisconnectActor(
        ScenarioActor actor,
        CancellationToken cancellationToken) {
        evidence.record("ActorUserDisconnected", context.spotRid().toString(), actor.actorId());
    }

    public String apply(String op) {
        state = state.isBlank() ? op : state + "," + op;
        evidence.record("StateRequest", context.spotRid().toString(), state);
        if (op.equals("worker-follow-up") && !workerDone) {
            evidence.record("WorkerFollowUpBeforeComplete", context.spotRid().toString(), state);
        }
        return state;
    }

    public String startWorker(String op) {
        workerDone = false;
        evidence.record("WorkerStarted", context.spotRid().toString(), op);
        context.runWorker(token -> {
            Thread.sleep(1500);
            return op + "-done";
        }).submit(
            (value, token) -> {
                workerDone = true;
                state = state.isBlank() ? value : state + "," + value;
                evidence.record("WorkerCompleted", context.spotRid().toString(), value);
            },
            (error, token) -> evidence.record(
                "WorkerFailed",
                context.spotRid().toString(),
                error.getClass().getSimpleName()));
        return state;
    }

    public void command(String value) {
        evidence.record("StateCommand", context.spotRid().toString(), value);
    }

    public void record(String marker, String value) {
        evidence.record(marker, context.spotRid().toString(), value);
    }

    String spotRid() {
        return context.spotRid().toString();
    }

    String nodeRid() {
        return evidence.nodeRid();
    }

    public void timerTick(long deliveryIndex) {
        evidence.record("SpotTimer", context.spotRid().toString(), Long.toString(deliveryIndex));
    }
}
