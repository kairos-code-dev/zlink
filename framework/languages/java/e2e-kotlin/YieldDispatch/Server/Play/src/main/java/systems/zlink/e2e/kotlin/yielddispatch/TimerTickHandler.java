package systems.zlink.e2e.kotlin.yielddispatch;

import java.time.Duration;
import systems.zlink.framework.spots.ZLinkSpotTimerHandler;
import systems.zlink.framework.spots.ZLinkTimerTick;

public final class TimerTickHandler implements ZLinkSpotTimerHandler<ProbeSpot> {
    private final PlayEvidenceStore evidence;

    public TimerTickHandler(PlayEvidenceStore evidence) {
        this.evidence = evidence;
    }

    @Override
    public void handle(
        ProbeSpot spot,
        ZLinkTimerTick tick) {
        ProbeSpot.TimerScenario scenario = spot.timerScenario(tick.name());
        if (scenario == null) {
            return;
        }
        String value = "spot=" + spot.context().spotRid()
            + ";timer=" + tick.name()
            + ";mailbox=timer:" + tick.name()
            + ";tick=" + tick.deliveryIndex();
        if (tick.deliveryIndex() == 1
            && ("yield-on-first".equals(scenario.mode()) || "yield-then-next".equals(scenario.mode()))) {
            evidence.record(scenario.requestId(), "timer-yield-started", value);
            evidence.record(scenario.requestId(), "timer-yield-released", value);
            spot.context().outbound()
                .requestToChannel(
                    Contracts.DELAY_CHANNEL,
                    new Contracts.DelayReq(scenario.requestId(), scenario.delayMillis()))
                .timeout(Duration.ofSeconds(5))
                .yield(Contracts.DelayRes.class);
            evidence.record(scenario.requestId(), "timer-yield-resumed", value);
            evidence.record(scenario.requestId(), "timer-yield-completed", value);
            if ("yield-on-first".equals(scenario.mode())) {
                spot.closeTimer(tick.name());
            }
            return;
        }
        if (tick.deliveryIndex() == 2 && "yield-then-next".equals(scenario.mode())) {
            evidence.record(scenario.requestId(), "timer-next-started", value);
            evidence.record(scenario.requestId(), "timer-next-completed", value);
            spot.closeTimer(tick.name());
            return;
        }
        if ("fast".equals(scenario.mode())) {
            evidence.record(scenario.requestId(), "timer-fast-started", value);
            evidence.record(scenario.requestId(), "timer-fast-completed", value);
            spot.closeTimer(tick.name());
        }
    }
}
