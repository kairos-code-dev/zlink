package systems.zlink.e2e.kotlin.yielddispatch;

import java.time.Duration;
import systems.zlink.framework.spots.ZLinkSpotPacketHandler;

public final class YieldCommandHandler
    implements ZLinkSpotPacketHandler<ProbeSpot, Contracts.YieldCommand> {
    private final PlayEvidenceStore evidence;

    public YieldCommandHandler(PlayEvidenceStore evidence) {
        this.evidence = evidence;
    }

    @Override
    public void handle(
        ProbeSpot spot,
        Contracts.YieldCommand command) {
        String value = "spot=" + spot.context().spotRid()
            + ";correlation=" + command.correlationId()
            + ";handler=spot";
        evidence.record(command.requestId(), "yield-started", value);
        evidence.record(command.requestId(), "yield-released", value);
        spot.context().outbound()
            .requestToChannel(
                Contracts.DELAY_CHANNEL,
                new Contracts.DelayRequest(command.requestId(), command.delayMillis()))
            .timeout(Duration.ofSeconds(5))
            .yield(Contracts.DelayReply.class);
        evidence.record(command.requestId(), "yield-resumed", value);
        evidence.record(command.requestId(), "yield-completed", value);
    }
}
