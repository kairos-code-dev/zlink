package systems.zlink.e2e.yielddispatch.shared;

import java.time.Duration;
import java.util.concurrent.atomic.AtomicBoolean;
import systems.zlink.framework.handlers.ZLinkSpotRequest;
import systems.zlink.framework.actors.ZLinkActorJoinResult;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.spots.ZLinkSpotPacketHandler;
import systems.zlink.framework.spots.ZLinkEntrySpotActorRequestHandler;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.framework.spots.ZLinkSpotActorRequestHandler;
import systems.zlink.framework.spots.ZLinkSpotTimerHandler;
import systems.zlink.framework.spots.ZLinkTimerTick;
import systems.zlink.framework.CancellationToken;

public final class YieldProbeHandlers {
    private YieldProbeHandlers() {
    }

    public static final class HoldHandler {
        private final EvidenceStore evidence;

        public HoldHandler(EvidenceStore evidence) {
            this.evidence = evidence;
        }

        @ZLinkSpotRequest
        public Contracts.ScenarioRes handle(
            YieldProbeSpot spot,
            Contracts.HoldReq request) {
            evidence.record("hold-started", request.requestId(), spot.context().spotRid().toString());
            spot.context().outbound()
                .requestToChannel(
                    Contracts.DELAY_CHANNEL,
                    new Contracts.DelayReq(request.requestId(), 800))
                .timeout(Duration.ofSeconds(5))
                .await(Contracts.DelayRes.class);
            evidence.record("hold-resumed", request.requestId(), spot.context().spotRid().toString());
            evidence.record("hold-completed", request.requestId(), spot.context().spotRid().toString());
            return new Contracts.ScenarioRes("YD-A1", request.requestId(), "ok");
        }
    }

    public static final class YieldHandler {
        private final EvidenceStore evidence;

        public YieldHandler(EvidenceStore evidence) {
            this.evidence = evidence;
        }

        @ZLinkSpotRequest
        public Contracts.ScenarioRes handle(
            YieldProbeSpot spot,
            Contracts.YieldReq request) {
            String context = "spot=" + spot.context().spotRid() + ";correlation=" + request.correlationId();
            evidence.record("yield-started", request.requestId(), context);
            evidence.record("yield-released", request.requestId(), context);
            spot.context().outbound()
                .requestToChannel(
                    Contracts.DELAY_CHANNEL,
                    new Contracts.DelayReq(request.requestId(), 800))
                .timeout(Duration.ofSeconds(5))
                .yield(Contracts.DelayRes.class);
            evidence.record("yield-resumed", request.requestId(), context);
            evidence.record("yield-completed", request.requestId(), context);
            return new Contracts.ScenarioRes(request.scenarioId(), request.requestId(), evidence.nodeRid());
        }
    }

    public static final class WorkerYieldHandler {
        private final EvidenceStore evidence;

        public WorkerYieldHandler(EvidenceStore evidence) {
            this.evidence = evidence;
        }

        @ZLinkSpotRequest
        public Contracts.ScenarioRes handle(
            YieldProbeSpot spot,
            Contracts.WorkerYieldReq request) {
            evidence.record("worker-yield-started", request.requestId(), spot.context().spotRid().toString());
            evidence.record("worker-yield-released", request.requestId(), spot.context().spotRid().toString());
            String result = spot.context().runWorker(token -> {
                    Thread.sleep(2000);
                    return request.requestId();
                })
                .timeout(Duration.ofSeconds(10))
                .yield();
            evidence.record("worker-yield-resumed", result, spot.context().spotRid().toString());
            evidence.record("worker-yield-completed", result, spot.context().spotRid().toString());
            return new Contracts.ScenarioRes("YD-A4", request.requestId(), "ok");
        }
    }

    public static final class ProbeHandler {
        private final EvidenceStore evidence;

        public ProbeHandler(EvidenceStore evidence) {
            this.evidence = evidence;
        }

        @ZLinkSpotRequest
        public Contracts.ProbeRes handle(
            YieldProbeSpot spot,
            Contracts.ProbeReq request) {
            evidence.record("probe-started", request.requestId(), spot.context().spotRid().toString());
            evidence.record("probe-completed", request.requestId(), spot.context().spotRid().toString());
            return new Contracts.ProbeRes(request.requestId());
        }
    }

    public static final class WorkerYieldMsgHandler
        implements ZLinkSpotPacketHandler<YieldProbeSpot, Contracts.WorkerYieldMsg> {
        private final EvidenceStore evidence;

        public WorkerYieldMsgHandler(EvidenceStore evidence) {
            this.evidence = evidence;
        }

        @Override
        public void handle(
            YieldProbeSpot spot,
            Contracts.WorkerYieldMsg request) {
            evidence.record("worker-yield-started", request.requestId(), spot.context().spotRid().toString());
            evidence.record("worker-yield-released", request.requestId(), spot.context().spotRid().toString());
            String result = spot.context().runWorker(token -> {
                    Thread.sleep(request.delayMillis());
                    return request.requestId();
                })
                .timeout(Duration.ofSeconds(10))
                .yield();
            evidence.record("worker-yield-resumed", result, spot.context().spotRid().toString());
            evidence.record("worker-yield-completed", result, spot.context().spotRid().toString());
        }
    }

    public static final class ProbeMsgHandler
        implements ZLinkSpotPacketHandler<YieldProbeSpot, Contracts.ProbeMsg> {
        private final EvidenceStore evidence;

        public ProbeMsgHandler(EvidenceStore evidence) {
            this.evidence = evidence;
        }

        @Override
        public void handle(
            YieldProbeSpot spot,
            Contracts.ProbeMsg request) {
            evidence.record("probe-started", request.requestId(), spot.context().spotRid().toString());
            evidence.record("probe-completed", request.requestId(), spot.context().spotRid().toString());
        }
    }

    public static final class YieldMsgHandler
        implements ZLinkSpotPacketHandler<YieldProbeSpot, Contracts.YieldMsg> {
        private final EvidenceStore evidence;

        public YieldMsgHandler(EvidenceStore evidence) {
            this.evidence = evidence;
        }

        @Override
        public void handle(
            YieldProbeSpot spot,
            Contracts.YieldMsg request) {
            String value = "spot=" + spot.context().spotRid()
                + ";correlation=" + request.correlationId()
                + ";handler=spot";
            evidence.record("yield-started", request.requestId(), value);
            evidence.record("yield-released", request.requestId(), value);
            spot.context().outbound()
                .requestToChannel(
                    Contracts.DELAY_CHANNEL,
                    new Contracts.DelayReq(request.requestId(), request.delayMillis()))
                .timeout(Duration.ofSeconds(5))
                .await(Contracts.DelayRes.class);
            evidence.record("yield-resumed", request.requestId(), value);
            evidence.record("yield-completed", request.requestId(), value);
        }
    }

    public static final class YieldTimeoutMsgHandler
        implements ZLinkSpotPacketHandler<YieldProbeSpot, Contracts.YieldTimeoutMsg> {
        private final EvidenceStore evidence;

        public YieldTimeoutMsgHandler(EvidenceStore evidence) {
            this.evidence = evidence;
        }

        @Override
        public void handle(
            YieldProbeSpot spot,
            Contracts.YieldTimeoutMsg request) {
            String value = "spot=" + spot.context().spotRid() + ";handler=spot";
            evidence.record("timeout-yield-started", request.requestId(), value);
            try {
                evidence.record("timeout-yield-released", request.requestId(), value);
                spot.context().outbound()
                    .requestToChannel(
                        Contracts.DELAY_CHANNEL,
                        new Contracts.DelayReq(request.requestId(), request.delayMillis()))
                    .timeout(Duration.ofMillis(request.timeoutMillis()))
                    .yield(Contracts.DelayRes.class);
                evidence.record("timeout-yield-unexpected-resumed", request.requestId(), value);
            } catch (RuntimeException error) {
                evidence.record(
                    "timeout-yield-completed",
                    request.requestId(),
                    value + ";error=" + error.getClass().getSimpleName());
            }
        }
    }

    public static final class YieldCancelMsgHandler
        implements ZLinkSpotPacketHandler<YieldProbeSpot, Contracts.YieldCancelMsg> {
        private final EvidenceStore evidence;

        public YieldCancelMsgHandler(EvidenceStore evidence) {
            this.evidence = evidence;
        }

        @Override
        public void handle(
            YieldProbeSpot spot,
            Contracts.YieldCancelMsg request) {
            String value = "spot=" + spot.context().spotRid() + ";handler=spot";
            AtomicBoolean canceled = new AtomicBoolean(false);
            evidence.record("cancel-yield-started", request.requestId(), value);
            try {
                evidence.record("cancel-yield-released", request.requestId(), value);
                spot.context().runWorker(token -> {
                        Thread.sleep(request.cancelAfterMillis());
                        canceled.set(true);
                        return request.requestId();
                    })
                    .timeout(Duration.ofSeconds(5))
                    .submit();
                spot.context().outbound()
                    .requestToChannel(
                        Contracts.DELAY_CHANNEL,
                        new Contracts.DelayReq(request.requestId(), request.delayMillis()))
                    .timeout(Duration.ofSeconds(5))
                    .yield(Contracts.DelayRes.class, canceled::get);
                evidence.record("cancel-yield-unexpected-resumed", request.requestId(), value);
            } catch (RuntimeException error) {
                evidence.record(
                    "cancel-yield-completed",
                    request.requestId(),
                    value + ";error=" + error.getClass().getSimpleName());
            }
        }
    }

    public static final class RemoteSpotYieldHandler {
        private final EvidenceStore evidence;

        public RemoteSpotYieldHandler(EvidenceStore evidence) {
            this.evidence = evidence;
        }

        @ZLinkSpotRequest
        public Contracts.ScenarioRes handle(
            YieldProbeSpot spot,
            Contracts.RemoteSpotYieldReq request) {
            String value = "spot=" + spot.context().spotRid() + ";target=" + request.targetSpotRid();
            evidence.record("remote-yield-started", request.requestId(), value);
            evidence.record("remote-yield-released", request.requestId(), value);
            Contracts.ScenarioRes targetReply = spot.context().outbound()
                .requestToSpot(
                    RoutingId.from(request.targetSpotRid()),
                    new Contracts.YieldReq("YD-D2", request.requestId(), "remote-spot"))
                .timeout(Duration.ofSeconds(5))
                .yield(Contracts.ScenarioRes.class);
            String resumed = value + ";targetNode=" + targetReply.result();
            evidence.record("remote-yield-resumed", request.requestId(), resumed);
            evidence.record("remote-yield-completed", request.requestId(), resumed);
            return new Contracts.ScenarioRes("YD-D2", request.requestId(), evidence.nodeRid());
        }
    }

    public static final class TimerStartMsgHandler
        implements ZLinkSpotPacketHandler<YieldProbeSpot, Contracts.TimerStartMsg> {
        @Override
        public void handle(
            YieldProbeSpot spot,
            Contracts.TimerStartMsg request) {
            spot.startTimer(request);
        }
    }

    public static final class TimerStopMsgHandler
        implements ZLinkSpotPacketHandler<YieldProbeSpot, Contracts.TimerStopMsg> {
        @Override
        public void handle(
            YieldProbeSpot spot,
            Contracts.TimerStopMsg request) {
            spot.stopTimers(request.requestId());
        }
    }

    public static final class TimerTickHandler implements ZLinkSpotTimerHandler<YieldProbeSpot> {
        private final EvidenceStore evidence;

        public TimerTickHandler(EvidenceStore evidence) {
            this.evidence = evidence;
        }

        @Override
        public void handle(YieldProbeSpot spot, ZLinkTimerTick tick) {
            YieldProbeSpot.TimerScenario scenario = spot.timerScenario(tick.name());
            if (scenario == null) {
                return;
            }
            String value = "timer=" + tick.name() + ";mailbox=timer:" + tick.name()
                + ";tick=" + tick.deliveryIndex() + ";spot=" + spot.context().spotRid();
            if (tick.deliveryIndex() == 1
                && ("yield-on-first".equals(scenario.mode()) || "yield-then-next".equals(scenario.mode()))) {
                evidence.record("timer-yield-started", scenario.requestId(), value);
                evidence.record("timer-yield-released", scenario.requestId(), value);
                spot.context().outbound()
                    .requestToChannel(
                        Contracts.DELAY_CHANNEL,
                        new Contracts.DelayReq(scenario.requestId(), scenario.delayMillis()))
                    .timeout(Duration.ofSeconds(5))
                    .yield(Contracts.DelayRes.class);
                evidence.record("timer-yield-resumed", scenario.requestId(), value);
                evidence.record("timer-yield-completed", scenario.requestId(), value);
                if ("yield-on-first".equals(scenario.mode())) {
                    spot.closeTimer(tick.name());
                }
                return;
            }
            if (tick.deliveryIndex() == 2 && "yield-then-next".equals(scenario.mode())) {
                evidence.record("timer-next-started", scenario.requestId(), value);
                evidence.record("timer-next-completed", scenario.requestId(), value);
                spot.closeTimer(tick.name());
                return;
            }
            if ("fast".equals(scenario.mode())) {
                evidence.record("timer-fast-started", scenario.requestId(), value);
                evidence.record("timer-fast-completed", scenario.requestId(), value);
                spot.closeTimer(tick.name());
            }
        }
    }

    public static final class ActorYieldHandler
        implements ZLinkEntrySpotActorRequestHandler<
            YieldEntrySpot,
            YieldActor,
            Contracts.ActorYieldReq,
            Contracts.ActorYieldRes> {
        private final EvidenceStore evidence;

        public ActorYieldHandler(EvidenceStore evidence) {
            this.evidence = evidence;
        }

        @Override
        public Contracts.ActorYieldRes handle(
            YieldEntrySpot spot,
            YieldActor actor,
            ZLinkSpotActorRequestContext context,
            Contracts.ActorYieldReq request,
            CancellationToken cancellationToken) {
            String value = "actor=" + actor.actorId() + ";mailbox=actor:" + actor.actorId()
                + ";spot=" + spot.context().spotRid();
            evidence.record("actor-yield-started", request.requestId(), value);
            evidence.record("actor-yield-released", request.requestId(), value);
            spot.context().outbound()
                .requestToChannel(
                    Contracts.DELAY_CHANNEL,
                    new Contracts.DelayReq(request.requestId(), request.delayMillis()))
                .timeout(Duration.ofSeconds(5))
                .await(Contracts.DelayRes.class);
            evidence.record("actor-yield-resumed", request.requestId(), value);
            evidence.record("actor-yield-completed", request.requestId(), value);
            return new Contracts.ActorYieldRes("YD-B", request.requestId(), actor.actorId(), "actor-yield-completed");
        }
    }

    public static final class ActorJoinHandler
        implements ZLinkEntrySpotActorRequestHandler<
            YieldEntrySpot,
            YieldActor,
            Contracts.ActorJoinReq,
            Contracts.ActorJoinRes> {
        private final EvidenceStore evidence;

        public ActorJoinHandler(EvidenceStore evidence) {
            this.evidence = evidence;
        }

        @Override
        public Contracts.ActorJoinRes handle(
            YieldEntrySpot spot,
            YieldActor actor,
            ZLinkSpotActorRequestContext context,
            Contracts.ActorJoinReq request,
            CancellationToken cancellationToken) {
            ZLinkActorJoinResult<Void> joined = actor.context()
                .joinSpot(systems.zlink.contracts.core.RoutingId.from(request.spotRid()))
                .timeout(Duration.ofSeconds(5))
                .await();
            evidence.record("actor-joined", request.requestId(),
                "actor=" + joined.actor().actorId() + ";spot=" + request.spotRid());
            return new Contracts.ActorJoinRes("YD-B-JOIN", request.requestId(), actor.actorId(), "joined");
        }
    }

    public static final class ActorJoinYieldHandler
        implements ZLinkEntrySpotActorRequestHandler<
            YieldEntrySpot,
            YieldActor,
            Contracts.ActorJoinYieldReq,
            Contracts.ActorJoinYieldRes> {
        private final EvidenceStore evidence;

        public ActorJoinYieldHandler(EvidenceStore evidence) {
            this.evidence = evidence;
        }

        @Override
        public Contracts.ActorJoinYieldRes handle(
            YieldEntrySpot spot,
            YieldActor actor,
            ZLinkSpotActorRequestContext context,
            Contracts.ActorJoinYieldReq request,
            CancellationToken cancellationToken) {
            String value = "actor=" + actor.actorId() + ";mailbox=actor:" + actor.actorId()
                + ";spot=" + spot.context().spotRid() + ";target=" + request.targetSpotRid();
            evidence.record("actor-join-yield-started", request.requestId(), value);
            evidence.record("actor-join-yield-released", request.requestId(), value);
            ZLinkActorJoinResult<Void> joined = actor.context()
                .joinSpot(
                    systems.zlink.contracts.core.RoutingId.from(request.targetSpotRid()),
                    new Contracts.DelayReq(request.requestId(), 350))
                .timeout(Duration.ofSeconds(5))
                .await();
            evidence.record("actor-join-yield-resumed", request.requestId(),
                value + ";joined=" + joined.actor().actorId());
            evidence.record("actor-join-yield-completed", request.requestId(),
                value + ";joined=" + joined.actor().actorId());
            return new Contracts.ActorJoinYieldRes(
                "YD-B3",
                request.requestId(),
                actor.actorId(),
                "actor-join-yield-completed");
        }
    }

    public static final class ActorPushNotifyYieldHandler
        implements ZLinkEntrySpotActorRequestHandler<
            YieldEntrySpot,
            YieldActor,
            Contracts.ActorPushYieldReq,
            Contracts.ActorPushYieldRes> {
        private final EvidenceStore evidence;

        public ActorPushNotifyYieldHandler(EvidenceStore evidence) {
            this.evidence = evidence;
        }

        @Override
        public Contracts.ActorPushYieldRes handle(
            YieldEntrySpot spot,
            YieldActor actor,
            ZLinkSpotActorRequestContext context,
            Contracts.ActorPushYieldReq request,
            CancellationToken cancellationToken) {
            String value = "actor=" + actor.actorId() + ";mailbox=actor:" + actor.actorId()
                + ";spot=" + spot.context().spotRid() + ";handler=actor";
            evidence.record("actor-push-yield-started", request.requestId(), value);
            evidence.record("actor-push-yield-released", request.requestId(), value);
            spot.context().outbound()
                .requestToChannel(
                    Contracts.DELAY_CHANNEL,
                    new Contracts.DelayReq(request.requestId(), request.delayMillis()))
                .timeout(Duration.ofSeconds(5))
                .await(Contracts.DelayRes.class);
            evidence.record("actor-push-yield-resumed", request.requestId(), value);
            actor.context()
                .boundSession()
                .send(new Contracts.ActorPushNotify(
                    actor.actorId(),
                    request.requestId(),
                    request.value(),
                    spot.context().nodeRid().toString()))
                .packetName("ActorPushNotify")
                .await();
            evidence.record("actor-push-yield-completed", request.requestId(), value);
            return new Contracts.ActorPushYieldRes(
                "YD-D4",
                request.requestId(),
                actor.actorId(),
                "actor-push-yield-completed");
        }
    }

    public static final class ActorFastHandler
        implements ZLinkEntrySpotActorRequestHandler<
            YieldEntrySpot,
            YieldActor,
            Contracts.ActorFastReq,
            Contracts.ActorFastRes> {
        private final EvidenceStore evidence;

        public ActorFastHandler(EvidenceStore evidence) {
            this.evidence = evidence;
        }

        @Override
        public Contracts.ActorFastRes handle(
            YieldEntrySpot spot,
            YieldActor actor,
            ZLinkSpotActorRequestContext context,
            Contracts.ActorFastReq request,
            CancellationToken cancellationToken) {
            String value = "actor=" + actor.actorId() + ";mailbox=actor:" + actor.actorId()
                + ";marker=" + request.marker() + ";spot=" + spot.context().spotRid();
            evidence.record("actor-fast-started", request.requestId(), value);
            evidence.record("actor-fast-completed", request.requestId(), value);
            return new Contracts.ActorFastRes("YD-B", request.requestId(), actor.actorId(), request.marker());
        }
    }

    public static final class SpotActorYieldHandler
        implements ZLinkSpotActorRequestHandler<
            YieldProbeSpot,
            YieldActor,
            Contracts.ActorYieldReq,
            Contracts.ActorYieldRes> {
        private final EvidenceStore evidence;

        public SpotActorYieldHandler(EvidenceStore evidence) {
            this.evidence = evidence;
        }

        @Override
        public Contracts.ActorYieldRes handle(
            YieldProbeSpot spot,
            YieldActor actor,
            ZLinkSpotActorRequestContext context,
            Contracts.ActorYieldReq request,
            CancellationToken cancellationToken) {
            String value = "actor=" + actor.actorId() + ";mailbox=actor:" + actor.actorId()
                + ";spot=" + spot.context().spotRid();
            evidence.record("actor-yield-started", request.requestId(), value);
            evidence.record("actor-yield-released", request.requestId(), value);
            spot.context().outbound()
                .requestToChannel(
                    Contracts.DELAY_CHANNEL,
                    new Contracts.DelayReq(request.requestId(), request.delayMillis()))
                .timeout(Duration.ofSeconds(5))
                .yield(Contracts.DelayRes.class);
            evidence.record("actor-yield-resumed", request.requestId(), value);
            evidence.record("actor-yield-completed", request.requestId(), value);
            return new Contracts.ActorYieldRes("YD-B", request.requestId(), actor.actorId(), "actor-yield-completed");
        }
    }

    public static final class SpotActorFastHandler
        implements ZLinkSpotActorRequestHandler<
            YieldProbeSpot,
            YieldActor,
            Contracts.ActorFastReq,
            Contracts.ActorFastRes> {
        private final EvidenceStore evidence;

        public SpotActorFastHandler(EvidenceStore evidence) {
            this.evidence = evidence;
        }

        @Override
        public Contracts.ActorFastRes handle(
            YieldProbeSpot spot,
            YieldActor actor,
            ZLinkSpotActorRequestContext context,
            Contracts.ActorFastReq request,
            CancellationToken cancellationToken) {
            String value = "actor=" + actor.actorId() + ";mailbox=actor:" + actor.actorId()
                + ";marker=" + request.marker() + ";spot=" + spot.context().spotRid();
            evidence.record("actor-fast-started", request.requestId(), value);
            evidence.record("actor-fast-completed", request.requestId(), value);
            return new Contracts.ActorFastRes("YD-B", request.requestId(), actor.actorId(), request.marker());
        }
    }
}
