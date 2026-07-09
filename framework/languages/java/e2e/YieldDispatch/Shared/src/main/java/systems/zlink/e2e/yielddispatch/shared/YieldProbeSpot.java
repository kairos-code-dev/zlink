package systems.zlink.e2e.yielddispatch.shared;

import java.time.Duration;
import java.util.HashMap;
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

public final class YieldProbeSpot implements ZLinkSpot<YieldActor> {
    private final ZLinkSpotContext context;
    private final EvidenceStore evidence;
    private final Map<String, TimerScenario> timerScenarios = new HashMap<>();
    private final Map<String, ZLinkTimer> timers = new HashMap<>();

    public YieldProbeSpot(
        ZLinkSpotContext context,
        EvidenceStore evidence) {
        this.context = context;
        this.evidence = evidence;
    }

    @Override
    public ZLinkSpotContext context() {
        return context;
    }

    @Override
    public void configure() {
        context.handlers().addHandler(YieldProbeHandlers.HoldHandler.class);
        context.handlers().addHandler(YieldProbeHandlers.YieldHandler.class);
        context.handlers().addHandler(YieldProbeHandlers.WorkerYieldHandler.class);
        context.handlers().addHandler(YieldProbeHandlers.ProbeHandler.class);
        context.handlers().addHandler(YieldProbeHandlers.WorkerYieldMsgHandler.class);
        context.handlers().addHandler(YieldProbeHandlers.ProbeMsgHandler.class);
        context.handlers().addHandler(YieldProbeHandlers.YieldMsgHandler.class);
        context.handlers().addHandler(YieldProbeHandlers.YieldTimeoutMsgHandler.class);
        context.handlers().addHandler(YieldProbeHandlers.YieldCancelMsgHandler.class);
        context.handlers().addHandler(YieldProbeHandlers.RemoteSpotYieldHandler.class);
        context.handlers().addHandler(YieldProbeHandlers.TimerStartMsgHandler.class);
        context.handlers().addHandler(YieldProbeHandlers.TimerStopMsgHandler.class);
        context.handlers().addHandler(YieldProbeHandlers.SpotActorYieldHandler.class);
        context.handlers().addHandler(YieldProbeHandlers.SpotActorFastHandler.class);
    }

    public synchronized void startTimer(Contracts.TimerStartMsg command) {
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
                YieldProbeHandlers.TimerTickHandler.class,
                options)
            .toCompletableFuture()
            .join();
        timers.put(command.timerName(), timer);
    }

    public synchronized void stopTimers(String requestId) {
        timerScenarios.entrySet().removeIf(entry -> {
            if (!entry.getValue().requestId().equals(requestId)) {
                return false;
            }
            ZLinkTimer timer = timers.remove(entry.getKey());
            if (timer != null) {
                timer.close();
            }
            return true;
        });
    }

    public synchronized TimerScenario timerScenario(String timerName) {
        return timerScenarios.get(timerName);
    }

    public synchronized void closeTimer(String timerName) {
        timerScenarios.remove(timerName);
        ZLinkTimer timer = timers.remove(timerName);
        if (timer != null) {
            timer.close();
        }
    }

    public record TimerScenario(String requestId, String mode, long delayMillis) {
    }

    @Override
    public ZLinkSpotCreateResponse onCreate(ZLinkMessage request) {
        return ZLinkSpotCreateResponse.accept();
    }

    @Override
    public ZLinkSpotActorJoinResponse onActorJoin(
        YieldActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken) {
        if (actor.actorId().startsWith("ydb3-")) {
            Contracts.DelayReq delay = request.decode(Contracts.DelayReq.class);
            try {
                Thread.sleep(delay.delayMillis());
            } catch (InterruptedException error) {
                Thread.currentThread().interrupt();
                throw new IllegalStateException("actor join interrupted", error);
            }
        }
        evidence.record("actor-target-join-requested", actor.actorId(), context.spotRid().toString());
        return ZLinkSpotActorJoinResponse.accept();
    }

    @Override
    public void onJoinedActor(
        YieldActor actor,
        CancellationToken cancellationToken) {
        evidence.record("actor-target-joined", actor.actorId(), context.spotRid().toString());
    }
}
