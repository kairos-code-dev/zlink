package systems.zlink.e2e.kotlin.yielddispatch;

import java.time.Duration;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.spots.ZLinkSpotPacketHandler;

public final class YieldCancelMsgHandler
    implements ZLinkSpotPacketHandler<ProbeSpot, Contracts.YieldCancelMsg> {
    private static final ScheduledExecutorService CANCELLATION_TIMER =
        Executors.newSingleThreadScheduledExecutor(task -> {
            Thread thread = new Thread(task, "yield-dispatch-cancel-timer");
            thread.setDaemon(true);
            return thread;
        });

    private final PlayEvidenceStore evidence;

    public YieldCancelMsgHandler(PlayEvidenceStore evidence) {
        this.evidence = evidence;
    }

    @Override
    public void handle(
        ProbeSpot spot,
        Contracts.YieldCancelMsg command) {
        String value = "spot=" + spot.context().spotRid() + ";node=" + spot.context().nodeRid();
        AtomicBoolean canceled = new AtomicBoolean(false);
        var cancelTask = CANCELLATION_TIMER.schedule(
            () -> canceled.set(true),
            command.cancelAfterMillis(),
            TimeUnit.MILLISECONDS);
        CancellationToken cancelAfter = canceled::get;

        evidence.record(command.requestId(), "cancel-yield-started", value);
        try {
            evidence.record(command.requestId(), "cancel-yield-released", value);
            spot.context().outbound()
                .requestToChannel(
                    Contracts.DELAY_CHANNEL,
                    new Contracts.DelayReq(command.requestId(), command.delayMillis()))
                .timeout(Duration.ofSeconds(5))
                .yield(Contracts.DelayRes.class, cancelAfter);
            evidence.record(command.requestId(), "cancel-yield-unexpected-resumed", value);
        } catch (RuntimeException error) {
            evidence.record(command.requestId(), "cancel-yield-completed", value + ";error=" + error.getClass().getSimpleName());
        } finally {
            cancelTask.cancel(false);
        }
    }
}
