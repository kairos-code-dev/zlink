package systems.zlink.e2e.kotlin.yielddispatch;

import java.time.Duration;
import systems.zlink.framework.spots.ZLinkSpotPacketHandler;

public final class YieldTimeoutMsgHandler
    implements ZLinkSpotPacketHandler<ProbeSpot, Contracts.YieldTimeoutMsg> {
    private final PlayEvidenceStore evidence;

    public YieldTimeoutMsgHandler(PlayEvidenceStore evidence) {
        this.evidence = evidence;
    }

    @Override
    public void handle(
        ProbeSpot spot,
        Contracts.YieldTimeoutMsg command) {
        String value = "spot=" + spot.context().spotRid() + ";node=" + spot.context().nodeRid();
        evidence.record(command.requestId(), "timeout-yield-started", value);
        try {
            evidence.record(command.requestId(), "timeout-yield-released", value);
            spot.context().outbound()
                .requestToChannel(
                    Contracts.DELAY_CHANNEL,
                    new Contracts.DelayReq(command.requestId(), command.delayMillis()))
                .timeout(Duration.ofMillis(command.timeoutMillis()))
                .yield(Contracts.DelayRes.class);
            evidence.record(command.requestId(), "timeout-yield-unexpected-resumed", value);
        } catch (RuntimeException error) {
            evidence.record(command.requestId(), "timeout-yield-completed", value + ";error=" + error.getClass().getSimpleName());
        }
    }
}
