package systems.zlink.e2e.kotlin.yielddispatch;

import java.time.Duration;
import systems.zlink.framework.spots.ZLinkSpotPacketHandler;

public final class HoldCommandHandler
    implements ZLinkSpotPacketHandler<ProbeSpot, Contracts.HoldCommand> {
    private final PlayEvidenceStore evidence;

    public HoldCommandHandler(PlayEvidenceStore evidence) {
        this.evidence = evidence;
    }

    @Override
    public void handle(
        ProbeSpot spot,
        Contracts.HoldCommand command) {
        String value = "spot=" + spot.context().spotRid()
            + ";handler=spot";
        evidence.record(command.requestId(), "hold-started", value);
        spot.context().outbound()
            .requestToChannel(
                Contracts.DELAY_CHANNEL,
                new Contracts.DelayRequest(command.requestId(), command.delayMillis()))
            .timeout(Duration.ofSeconds(5))
            .await(Contracts.DelayReply.class);
        evidence.record(command.requestId(), "hold-resumed", value);
        evidence.record(command.requestId(), "hold-completed", value);
    }
}
