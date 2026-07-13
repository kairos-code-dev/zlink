package systems.zlink.e2e.kotlin.automaticturn;

import java.time.Duration;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.spots.ZLinkSpotPacketHandler;

public final class AwaitMsgHandler
    implements ZLinkSpotPacketHandler<ProbeSpot, Contracts.AwaitMsg> {
    private final PlayEvidenceStore evidence;

    public AwaitMsgHandler(PlayEvidenceStore evidence) {
        this.evidence = evidence;
    }

    @Override
    public CompletionStage<Void> handle(
        ProbeSpot spot,
        Contracts.AwaitMsg command) {
        String value = "spot=" + spot.context().spotRid()
            + ";correlation=" + command.correlationId()
            + ";handler=spot";
        evidence.record(command.requestId(), "await-started", value);
        evidence.record(command.requestId(), "await-released", value);
        return spot.context().outbound()
            .requestToChannel(
                Contracts.DELAY_CHANNEL,
                new Contracts.DelayReq(command.requestId(), command.delayMillis()))
            .timeout(Duration.ofSeconds(5))
            .submit(Contracts.DelayRes.class)
            .thenAccept(reply -> {
                evidence.record(command.requestId(), "await-resumed", value);
                evidence.record(command.requestId(), "await-completed", value);
            });
    }
}
