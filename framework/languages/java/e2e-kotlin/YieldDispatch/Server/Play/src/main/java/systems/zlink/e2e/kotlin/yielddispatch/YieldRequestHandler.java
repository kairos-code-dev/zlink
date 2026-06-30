package systems.zlink.e2e.kotlin.yielddispatch;

import java.time.Duration;
import systems.zlink.framework.handlers.ZLinkSpotRequest;

public final class YieldRequestHandler {
    private final PlayEvidenceStore evidence;

    public YieldRequestHandler(PlayEvidenceStore evidence) {
        this.evidence = evidence;
    }

    @ZLinkSpotRequest
    public Contracts.ScenarioReply handle(
        ProbeSpot spot,
        Contracts.YieldRequest request) {
        String value = "spot=" + spot.context().spotRid() + ";correlation=" + request.correlationId();
        evidence.record(request.requestId(), "yield-started", value);
        evidence.record(request.requestId(), "yield-released", value);
        spot.context().outbound()
            .requestToChannel(
                Contracts.DELAY_CHANNEL,
                new Contracts.DelayRequest(request.requestId(), 350))
            .timeout(Duration.ofSeconds(5))
            .yield(Contracts.DelayReply.class);
        evidence.record(request.requestId(), "yield-resumed", value);
        evidence.record(request.requestId(), "yield-completed", value);
        return new Contracts.ScenarioReply(
            request.scenarioId(),
            request.requestId(),
            spot.context().nodeRid().toString());
    }
}
