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
        context.handlers().addPacket(ProbeReqHandler.class);
        context.handlers().addPacket(HoldMsgHandler.class);
        context.handlers().addPacket(YieldReqHandler.class);
        context.handlers().addPacket(YieldMsgHandler.class);
        context.handlers().addPacket(WorkerYieldMsgHandler.class);
        context.handlers().addPacket(ProbeMsgHandler.class);
        context.handlers().addPacket(RemoteSpotYieldReqHandler.class);
        context.handlers().addPacket(TimerStartMsgHandler.class);
        context.handlers().addPacket(TimerStopMsgHandler.class);
        context.handlers().addPacket(YieldTimeoutMsgHandler.class);
        context.handlers().addPacket(SpotProbeMsgHandler.class);
        context.handlers().addActorRequest(ProbeActorRequestHandler.class);
        context.handlers().addActorRequest(ProbeActorJoinHandler.class);
        context.handlers().addActorRequest(ProbeActorYieldHandler.class);
        context.handlers().addActorRequest(ProbeActorFastHandler.class);
    }

    @Override
    public ZLinkSpotCreateResponse onCreate(ZLinkMessage request) {
        return ZLinkSpotCreateResponse.accept();
    }

    @Override
    public ZLinkSpotActorJoinResponse onActorJoin(
        ProbeActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken) {
        Contracts.ActorJoinReq join = request.decode(Contracts.ActorJoinReq.class);
        return ZLinkSpotActorJoinResponse.accept(new Contracts.ActorJoinRes(
            actor.actorId(),
            context.spotRid().toString(),
            "joined:" + join.value()));
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
