package systems.zlink.e2e.spotservice.shared;

import java.time.Duration;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
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
    private CountDownLatch workerFollowUp;

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
        context.handlers().addHandler(StateReqHandler.class);
        context.handlers().addHandler(StateMsgHandler.class);
        context.handlers().addHandler(StageProbeReqHandler.class);
        context.handlers().addHandler(StageTimerStartReqHandler.class);
        context.handlers().addHandler(SlowReqHandler.class);
        context.handlers().addHandler(OutboundReqHandler.class);
        context.handlers().addHandler(OutboundMsgHandler.class);
        context.handlers().addHandler(SpotEventHandler.class);
        context.handlers().addHandler(UserActorEchoHandler.class);
        context.handlers().addHandler(UserActorLeaveHandler.class);
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
        Contracts.JoinAdmittedUserSpotActorReq admission = decodeAdmission(request);
        if (admission != null) {
            actor.applyProfile(admission.profile());
            if (!admission.admit()) {
                evidence.record("ActorUserJoinRejected", context.spotRid().toString(),
                    actor.actorId() + "/" + admission.reason());
                return ZLinkSpotActorJoinResponse.reject(new Contracts.JoinAdmittedUserSpotActorRes(
                    actor.actorId(),
                    context.spotRid().toString(),
                    evidence.nodeRid(),
                    false,
                    "ActorJoinRejected"));
            }
            evidence.record("ActorUserJoinAdmitted", context.spotRid().toString(),
                actor.actorId() + "/" + admission.reason());
            return ZLinkSpotActorJoinResponse.accept(new Contracts.ActorJoinRes(
                actor.actorId(),
                context.spotRid().toString(),
                evidence.nodeRid(),
                admission.profile().displayName(),
                admission.profile().level(),
                admission.tags()));
        }
        Contracts.ActorJoinReq join = request.decode(Contracts.ActorJoinReq.class);
        actor.applyProfile(join.profile());
        evidence.record("ActorUserJoinRequested", context.spotRid().toString(),
            actor.actorId() + "/" + join.profile().displayName() + "/" + String.join(",", join.tags()));
        return ZLinkSpotActorJoinResponse.accept(new Contracts.ActorJoinRes(
            actor.actorId(),
            context.spotRid().toString(),
            evidence.nodeRid(),
            join.profile().displayName(),
            join.profile().level(),
            join.tags()));
    }

    private static Contracts.JoinAdmittedUserSpotActorReq decodeAdmission(ZLinkMessage request) {
        try {
            Contracts.JoinAdmittedUserSpotActorReq admission =
                request.decode(Contracts.JoinAdmittedUserSpotActorReq.class);
            if (!admission.admit() && admission.reason() == null) {
                return null;
            }
            return admission;
        } catch (RuntimeException ignored) {
            return null;
        }
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
        evidence.record("StateReq", context.spotRid().toString(), state);
        if (op.equals("worker-follow-up") && !workerDone) {
            evidence.record("WorkerFollowUpBeforeComplete", context.spotRid().toString(), state);
            CountDownLatch latch = workerFollowUp;
            if (latch != null) {
                latch.countDown();
            }
        }
        return state;
    }

    public String startWorker(String op) {
        workerDone = false;
        workerFollowUp = new CountDownLatch(1);
        evidence.record("WorkerStarted", context.spotRid().toString(), op);
        CountDownLatch latch = workerFollowUp;
        context.runWorker(token -> {
            latch.await(5, TimeUnit.SECONDS);
            return op + "-done";
        }).submit(
            (value, token) -> {
                workerDone = true;
                workerFollowUp = null;
                state = state.isBlank() ? value : state + "," + value;
                evidence.record("WorkerCompleted", context.spotRid().toString(), value);
            },
            (error, token) -> {
                workerFollowUp = null;
                evidence.record(
                    "WorkerFailed",
                    context.spotRid().toString(),
                    error.getClass().getSimpleName());
            });
        return state;
    }

    public void command(String value) {
        evidence.record("StateMsg", context.spotRid().toString(), value);
    }

    public void record(String marker, String value) {
        evidence.record(marker, context.spotRid().toString(), value);
    }

    public ScenarioStage stage() {
        return new ScenarioStage(this);
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
