package systems.zlink.e2e.yielddispatch.shared;

import java.time.Duration;
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
        public Contracts.ScenarioReply handle(
            YieldProbeSpot spot,
            Contracts.HoldRequest request) {
            evidence.record("hold-started", request.requestId(), spot.context().spotRid().toString());
            spot.context().outbound()
                .requestToChannel(
                    Contracts.DELAY_CHANNEL,
                    new Contracts.DelayRequest(request.requestId(), 800))
                .timeout(Duration.ofSeconds(5))
                .await(Contracts.DelayReply.class);
            evidence.record("hold-resumed", request.requestId(), spot.context().spotRid().toString());
            evidence.record("hold-completed", request.requestId(), spot.context().spotRid().toString());
            return new Contracts.ScenarioReply("YD-A1", request.requestId(), "ok");
        }
    }

    public static final class YieldHandler {
        private final EvidenceStore evidence;

        public YieldHandler(EvidenceStore evidence) {
            this.evidence = evidence;
        }

        @ZLinkSpotRequest
        public Contracts.ScenarioReply handle(
            YieldProbeSpot spot,
            Contracts.YieldRequest request) {
            String context = "spot=" + spot.context().spotRid() + ";correlation=" + request.correlationId();
            evidence.record("yield-started", request.requestId(), context);
            evidence.record("yield-released", request.requestId(), context);
            spot.context().outbound()
                .requestToChannel(
                    Contracts.DELAY_CHANNEL,
                    new Contracts.DelayRequest(request.requestId(), 800))
                .timeout(Duration.ofSeconds(5))
                .yield(Contracts.DelayReply.class);
            evidence.record("yield-resumed", request.requestId(), context);
            evidence.record("yield-completed", request.requestId(), context);
            return new Contracts.ScenarioReply(request.scenarioId(), request.requestId(), evidence.nodeRid());
        }
    }

    public static final class WorkerYieldHandler {
        private final EvidenceStore evidence;

        public WorkerYieldHandler(EvidenceStore evidence) {
            this.evidence = evidence;
        }

        @ZLinkSpotRequest
        public Contracts.ScenarioReply handle(
            YieldProbeSpot spot,
            Contracts.WorkerYieldRequest request) {
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
            return new Contracts.ScenarioReply("YD-A4", request.requestId(), "ok");
        }
    }

    public static final class ProbeHandler {
        private final EvidenceStore evidence;

        public ProbeHandler(EvidenceStore evidence) {
            this.evidence = evidence;
        }

        @ZLinkSpotRequest
        public Contracts.ProbeReply handle(
            YieldProbeSpot spot,
            Contracts.ProbeRequest request) {
            evidence.record("probe-started", request.requestId(), spot.context().spotRid().toString());
            evidence.record("probe-completed", request.requestId(), spot.context().spotRid().toString());
            return new Contracts.ProbeReply(request.requestId());
        }
    }

    public static final class WorkerYieldCommandHandler
        implements ZLinkSpotPacketHandler<YieldProbeSpot, Contracts.WorkerYieldCommand> {
        private final EvidenceStore evidence;

        public WorkerYieldCommandHandler(EvidenceStore evidence) {
            this.evidence = evidence;
        }

        @Override
        public void handle(
            YieldProbeSpot spot,
            Contracts.WorkerYieldCommand request) {
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

    public static final class ProbeCommandHandler
        implements ZLinkSpotPacketHandler<YieldProbeSpot, Contracts.ProbeCommand> {
        private final EvidenceStore evidence;

        public ProbeCommandHandler(EvidenceStore evidence) {
            this.evidence = evidence;
        }

        @Override
        public void handle(
            YieldProbeSpot spot,
            Contracts.ProbeCommand request) {
            evidence.record("probe-started", request.requestId(), spot.context().spotRid().toString());
            evidence.record("probe-completed", request.requestId(), spot.context().spotRid().toString());
        }
    }

    public static final class YieldCommandHandler
        implements ZLinkSpotPacketHandler<YieldProbeSpot, Contracts.YieldCommand> {
        private final EvidenceStore evidence;

        public YieldCommandHandler(EvidenceStore evidence) {
            this.evidence = evidence;
        }

        @Override
        public void handle(
            YieldProbeSpot spot,
            Contracts.YieldCommand request) {
            String value = "spot=" + spot.context().spotRid()
                + ";correlation=" + request.correlationId()
                + ";handler=spot";
            evidence.record("yield-started", request.requestId(), value);
            evidence.record("yield-released", request.requestId(), value);
            spot.context().outbound()
                .requestToChannel(
                    Contracts.DELAY_CHANNEL,
                    new Contracts.DelayRequest(request.requestId(), request.delayMillis()))
                .timeout(Duration.ofSeconds(5))
                .yield(Contracts.DelayReply.class);
            evidence.record("yield-resumed", request.requestId(), value);
            evidence.record("yield-completed", request.requestId(), value);
        }
    }

    public static final class YieldTimeoutCommandHandler
        implements ZLinkSpotPacketHandler<YieldProbeSpot, Contracts.YieldTimeoutCommand> {
        private final EvidenceStore evidence;

        public YieldTimeoutCommandHandler(EvidenceStore evidence) {
            this.evidence = evidence;
        }

        @Override
        public void handle(
            YieldProbeSpot spot,
            Contracts.YieldTimeoutCommand request) {
            String value = "spot=" + spot.context().spotRid() + ";handler=spot";
            evidence.record("timeout-yield-started", request.requestId(), value);
            try {
                evidence.record("timeout-yield-released", request.requestId(), value);
                spot.context().outbound()
                    .requestToChannel(
                        Contracts.DELAY_CHANNEL,
                        new Contracts.DelayRequest(request.requestId(), request.delayMillis()))
                    .timeout(Duration.ofMillis(request.timeoutMillis()))
                    .yield(Contracts.DelayReply.class);
                evidence.record("timeout-yield-unexpected-resumed", request.requestId(), value);
            } catch (RuntimeException error) {
                evidence.record(
                    "timeout-yield-completed",
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
        public Contracts.ScenarioReply handle(
            YieldProbeSpot spot,
            Contracts.RemoteSpotYieldRequest request) {
            String value = "spot=" + spot.context().spotRid() + ";target=" + request.targetSpotRid();
            evidence.record("remote-yield-started", request.requestId(), value);
            evidence.record("remote-yield-released", request.requestId(), value);
            Contracts.ScenarioReply targetReply = spot.context().outbound()
                .requestToSpot(
                    RoutingId.from(request.targetSpotRid()),
                    new Contracts.YieldRequest("YD-D2", request.requestId(), "remote-spot"))
                .timeout(Duration.ofSeconds(5))
                .yield(Contracts.ScenarioReply.class);
            String resumed = value + ";targetNode=" + targetReply.result();
            evidence.record("remote-yield-resumed", request.requestId(), resumed);
            evidence.record("remote-yield-completed", request.requestId(), resumed);
            return new Contracts.ScenarioReply("YD-D2", request.requestId(), evidence.nodeRid());
        }
    }

    public static final class TimerStartCommandHandler
        implements ZLinkSpotPacketHandler<YieldProbeSpot, Contracts.TimerStartCommand> {
        @Override
        public void handle(
            YieldProbeSpot spot,
            Contracts.TimerStartCommand request) {
            spot.startTimer(request);
        }
    }

    public static final class TimerStopCommandHandler
        implements ZLinkSpotPacketHandler<YieldProbeSpot, Contracts.TimerStopCommand> {
        @Override
        public void handle(
            YieldProbeSpot spot,
            Contracts.TimerStopCommand request) {
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
                        new Contracts.DelayRequest(scenario.requestId(), scenario.delayMillis()))
                    .timeout(Duration.ofSeconds(5))
                    .yield(Contracts.DelayReply.class);
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
            Contracts.ActorYieldRequest,
            Contracts.ActorReply> {
        private final EvidenceStore evidence;

        public ActorYieldHandler(EvidenceStore evidence) {
            this.evidence = evidence;
        }

        @Override
        public Contracts.ActorReply handle(
            YieldEntrySpot spot,
            YieldActor actor,
            ZLinkSpotActorRequestContext context,
            Contracts.ActorYieldRequest request,
            CancellationToken cancellationToken) {
            String value = "actor=" + actor.actorId() + ";mailbox=actor:" + actor.actorId()
                + ";spot=" + spot.context().spotRid();
            evidence.record("actor-yield-started", request.requestId(), value);
            evidence.record("actor-yield-released", request.requestId(), value);
            spot.context().outbound()
                .requestToChannel(
                    Contracts.DELAY_CHANNEL,
                    new Contracts.DelayRequest(request.requestId(), request.delayMillis()))
                .timeout(Duration.ofSeconds(5))
                .yield(Contracts.DelayReply.class);
            evidence.record("actor-yield-resumed", request.requestId(), value);
            evidence.record("actor-yield-completed", request.requestId(), value);
            return new Contracts.ActorReply("YD-B", request.requestId(), actor.actorId(), "actor-yield-completed");
        }
    }

    public static final class ActorJoinHandler
        implements ZLinkEntrySpotActorRequestHandler<
            YieldEntrySpot,
            YieldActor,
            Contracts.ActorJoinRequest,
            Contracts.ActorReply> {
        private final EvidenceStore evidence;

        public ActorJoinHandler(EvidenceStore evidence) {
            this.evidence = evidence;
        }

        @Override
        public Contracts.ActorReply handle(
            YieldEntrySpot spot,
            YieldActor actor,
            ZLinkSpotActorRequestContext context,
            Contracts.ActorJoinRequest request,
            CancellationToken cancellationToken) {
            ZLinkActorJoinResult<Void> joined = actor.context()
                .joinSpot(systems.zlink.contracts.core.RoutingId.from(request.spotRid()))
                .timeout(Duration.ofSeconds(5))
                .await();
            evidence.record("actor-joined", request.requestId(),
                "actor=" + joined.actor().actorId() + ";spot=" + request.spotRid());
            return new Contracts.ActorReply("YD-B-JOIN", request.requestId(), actor.actorId(), "joined");
        }
    }

    public static final class ActorJoinYieldHandler
        implements ZLinkEntrySpotActorRequestHandler<
            YieldEntrySpot,
            YieldActor,
            Contracts.ActorJoinYieldRequest,
            Contracts.ActorReply> {
        private final EvidenceStore evidence;

        public ActorJoinYieldHandler(EvidenceStore evidence) {
            this.evidence = evidence;
        }

        @Override
        public Contracts.ActorReply handle(
            YieldEntrySpot spot,
            YieldActor actor,
            ZLinkSpotActorRequestContext context,
            Contracts.ActorJoinYieldRequest request,
            CancellationToken cancellationToken) {
            String value = "actor=" + actor.actorId() + ";mailbox=actor:" + actor.actorId()
                + ";spot=" + spot.context().spotRid() + ";target=" + request.targetSpotRid();
            evidence.record("actor-join-yield-started", request.requestId(), value);
            evidence.record("actor-join-yield-released", request.requestId(), value);
            ZLinkActorJoinResult<Void> joined = actor.context()
                .joinSpot(
                    systems.zlink.contracts.core.RoutingId.from(request.targetSpotRid()),
                    new Contracts.DelayRequest(request.requestId(), 350))
                .timeout(Duration.ofSeconds(5))
                .yield();
            evidence.record("actor-join-yield-resumed", request.requestId(),
                value + ";joined=" + joined.actor().actorId());
            evidence.record("actor-join-yield-completed", request.requestId(),
                value + ";joined=" + joined.actor().actorId());
            return new Contracts.ActorReply(
                "YD-B3",
                request.requestId(),
                actor.actorId(),
                "actor-join-yield-completed");
        }
    }

    public static final class ActorPushYieldHandler
        implements ZLinkEntrySpotActorRequestHandler<
            YieldEntrySpot,
            YieldActor,
            Contracts.ActorPushYieldRequest,
            Contracts.ActorReply> {
        private final EvidenceStore evidence;

        public ActorPushYieldHandler(EvidenceStore evidence) {
            this.evidence = evidence;
        }

        @Override
        public Contracts.ActorReply handle(
            YieldEntrySpot spot,
            YieldActor actor,
            ZLinkSpotActorRequestContext context,
            Contracts.ActorPushYieldRequest request,
            CancellationToken cancellationToken) {
            String value = "actor=" + actor.actorId() + ";mailbox=actor:" + actor.actorId()
                + ";spot=" + spot.context().spotRid() + ";handler=actor";
            evidence.record("actor-push-yield-started", request.requestId(), value);
            evidence.record("actor-push-yield-released", request.requestId(), value);
            spot.context().outbound()
                .requestToChannel(
                    Contracts.DELAY_CHANNEL,
                    new Contracts.DelayRequest(request.requestId(), request.delayMillis()))
                .timeout(Duration.ofSeconds(5))
                .yield(Contracts.DelayReply.class);
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
            return new Contracts.ActorReply(
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
            Contracts.ActorFastRequest,
            Contracts.ActorReply> {
        private final EvidenceStore evidence;

        public ActorFastHandler(EvidenceStore evidence) {
            this.evidence = evidence;
        }

        @Override
        public Contracts.ActorReply handle(
            YieldEntrySpot spot,
            YieldActor actor,
            ZLinkSpotActorRequestContext context,
            Contracts.ActorFastRequest request,
            CancellationToken cancellationToken) {
            String value = "actor=" + actor.actorId() + ";mailbox=actor:" + actor.actorId()
                + ";marker=" + request.marker() + ";spot=" + spot.context().spotRid();
            evidence.record("actor-fast-started", request.requestId(), value);
            evidence.record("actor-fast-completed", request.requestId(), value);
            return new Contracts.ActorReply("YD-B", request.requestId(), actor.actorId(), request.marker());
        }
    }

    public static final class SpotActorYieldHandler
        implements ZLinkSpotActorRequestHandler<
            YieldProbeSpot,
            YieldActor,
            Contracts.ActorYieldRequest,
            Contracts.ActorReply> {
        private final EvidenceStore evidence;

        public SpotActorYieldHandler(EvidenceStore evidence) {
            this.evidence = evidence;
        }

        @Override
        public Contracts.ActorReply handle(
            YieldProbeSpot spot,
            YieldActor actor,
            ZLinkSpotActorRequestContext context,
            Contracts.ActorYieldRequest request,
            CancellationToken cancellationToken) {
            String value = "actor=" + actor.actorId() + ";mailbox=actor:" + actor.actorId()
                + ";spot=" + spot.context().spotRid();
            evidence.record("actor-yield-started", request.requestId(), value);
            evidence.record("actor-yield-released", request.requestId(), value);
            spot.context().outbound()
                .requestToChannel(
                    Contracts.DELAY_CHANNEL,
                    new Contracts.DelayRequest(request.requestId(), request.delayMillis()))
                .timeout(Duration.ofSeconds(5))
                .yield(Contracts.DelayReply.class);
            evidence.record("actor-yield-resumed", request.requestId(), value);
            evidence.record("actor-yield-completed", request.requestId(), value);
            return new Contracts.ActorReply("YD-B", request.requestId(), actor.actorId(), "actor-yield-completed");
        }
    }

    public static final class SpotActorFastHandler
        implements ZLinkSpotActorRequestHandler<
            YieldProbeSpot,
            YieldActor,
            Contracts.ActorFastRequest,
            Contracts.ActorReply> {
        private final EvidenceStore evidence;

        public SpotActorFastHandler(EvidenceStore evidence) {
            this.evidence = evidence;
        }

        @Override
        public Contracts.ActorReply handle(
            YieldProbeSpot spot,
            YieldActor actor,
            ZLinkSpotActorRequestContext context,
            Contracts.ActorFastRequest request,
            CancellationToken cancellationToken) {
            String value = "actor=" + actor.actorId() + ";mailbox=actor:" + actor.actorId()
                + ";marker=" + request.marker() + ";spot=" + spot.context().spotRid();
            evidence.record("actor-fast-started", request.requestId(), value);
            evidence.record("actor-fast-completed", request.requestId(), value);
            return new Contracts.ActorReply("YD-B", request.requestId(), actor.actorId(), request.marker());
        }
    }
}
