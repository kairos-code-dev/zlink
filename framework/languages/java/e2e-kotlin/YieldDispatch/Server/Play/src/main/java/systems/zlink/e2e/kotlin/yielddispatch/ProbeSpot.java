package systems.zlink.e2e.kotlin.yielddispatch;

import java.time.Duration;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.spots.ZLinkSpotCreateResponse;
import systems.zlink.framework.spots.ZLinkTimer;
import systems.zlink.framework.spots.ZLinkTimerOptions;
import systems.zlink.framework.spots.ZLinkTimerOverrunPolicy;

public final class ProbeSpot implements ZLinkSpot<ProbeActor> {
    private final ZLinkSpotContext context;
    private final Map<String, TimerScenario> timerScenarios = new HashMap<>();
    private final Map<String, ZLinkTimer> timers = new HashMap<>();
    private int sequence;

    public ProbeSpot(ZLinkSpotContext context) {
        this.context = context;
    }

    @Override
    public ZLinkSpotContext context() {
        return context;
    }

    @Override
    public void configure() {
        context.handlers().addHandler(ProbeReqHandler.class);
        context.handlers().addHandler(HoldMsgHandler.class);
        context.handlers().addHandler(YieldReqHandler.class);
        context.handlers().addHandler(ShutdownYieldReqHandler.class);
        context.handlers().addHandler(YieldMsgHandler.class);
        context.handlers().addHandler(WorkerYieldMsgHandler.class);
        context.handlers().addHandler(ProbeMsgHandler.class);
        context.handlers().addHandler(RemoteSpotYieldReqHandler.class);
        context.handlers().addHandler(TimerStartMsgHandler.class);
        context.handlers().addHandler(TimerStopMsgHandler.class);
        context.handlers().addHandler(YieldTimeoutMsgHandler.class);
        context.handlers().addHandler(YieldTimeoutReqHandler.class);
        context.handlers().addHandler(YieldCancelMsgHandler.class);
        context.handlers().addHandler(SpotProbeMsgHandler.class);
        context.handlers().addHandler(ProbeActorRequestHandler.class);
        context.handlers().addHandler(ProbeActorJoinHandler.class);
        context.handlers().addHandler(ProbeActorYieldHandler.class);
        context.handlers().addHandler(ProbeActorFastHandler.class);
        context.handlers().addHandler(ProbeActorPushYieldHandler.class);
    }

    @Override
    public ZLinkSpotCreateResponse onCreate(ZLinkMessage request) {
        return ZLinkSpotCreateResponse.accept();
    }

    @Override
    public ZLinkSpotActorJoinResponse onActorJoin(
        String actorId,
        ZLinkMessage request,
        CancellationToken cancellationToken) {
        if (actorId.startsWith("YD-B3-")) {
            Contracts.DelayReq delay = request.decode(Contracts.DelayReq.class);
            try {
                Thread.sleep(delay.millis());
            } catch (InterruptedException error) {
                Thread.currentThread().interrupt();
                throw new IllegalStateException("actor join interrupted", error);
            }
            return ZLinkSpotActorJoinResponse.accept();
        }
        Contracts.ActorJoinReq join = request.decode(Contracts.ActorJoinReq.class);
        return ZLinkSpotActorJoinResponse.accept(new Contracts.ActorJoinRes(
            actorId,
            context.spotRid().toString(),
            "joined:" + join.value()));
    }

    @Override
    public void onJoinedActor(ProbeActor actor, CancellationToken cancellationToken) {
    }

    @Override
    public void onLeaveActor(ProbeActor actor, CancellationToken cancellationToken) {
    }

    Contracts.ProbeRes handle(Contracts.ProbeReq request) {
        sequence++;
        if (request.op().equals("worker")) {
            String worker = context.runWorker(token -> {
                    Thread.sleep(request.millis());
                    return "worker:" + request.op();
                })
                .timeout(Duration.ofSeconds(5))
                .yield();
            return reply(request, worker + "#" + sequence);
        }
        if (request.millis() <= 0) {
            return reply(request, "immediate:" + request.op() + "#" + sequence);
        }
        Contracts.DelayRes delayed = context.outbound()
            .requestToChannel(Contracts.DELAY_CHANNEL, new Contracts.DelayReq(request.op(), request.millis()))
            .timeout(Duration.ofSeconds(5))
            .yield(Contracts.DelayRes.class);
        return reply(request, delayed.value() + "#" + sequence);
    }

    private Contracts.ProbeRes reply(Contracts.ProbeReq request, String value) {
        return new Contracts.ProbeRes(
            context.spotRid().toString(),
            context.nodeRid().toString(),
            request.op(),
            value);
    }

    synchronized void startTimer(Contracts.TimerStartMsg command) {
        ZLinkTimer previous = timers.remove(command.timerName());
        if (previous != null) {
            previous.close();
        }
        timerScenarios.put(command.timerName(), new TimerScenario(
            command.requestId(),
            command.mode(),
            command.delayMillis()));
        ZLinkTimerOptions options = new ZLinkTimerOptions();
        options.setOverrunPolicy(ZLinkTimerOverrunPolicy.DELAY_NEXT_TICK);
        ZLinkTimer timer = context.addTimer(
                command.timerName(),
                Duration.ofMillis(command.periodMillis()),
                TimerTickHandler.class,
                options)
            .toCompletableFuture()
            .join();
        timers.put(command.timerName(), timer);
    }

    synchronized void stopTimers(String requestId) {
        List<String> names = timerScenarios.entrySet().stream()
            .filter(entry -> entry.getValue().requestId().equals(requestId))
            .map(Map.Entry::getKey)
            .toList();
        names.forEach(this::closeTimer);
    }

    synchronized TimerScenario timerScenario(String timerName) {
        return timerScenarios.get(timerName);
    }

    synchronized void closeTimer(String timerName) {
        timerScenarios.remove(timerName);
        ZLinkTimer timer = timers.remove(timerName);
        if (timer != null) {
            timer.close();
        }
    }

    record TimerScenario(String requestId, String mode, long delayMillis) {
    }
}
