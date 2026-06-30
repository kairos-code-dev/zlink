package systems.zlink.e2e.kotlin.yielddispatch;

import java.time.Duration;
import systems.zlink.framework.handlers.ZLinkSpotRequest;

public final class YieldReqHandler {
    private final PlayEvidenceStore evidence;

    public YieldReqHandler(PlayEvidenceStore evidence) {
        this.evidence = evidence;
    }

    @ZLinkSpotRequest
    public Contracts.ScenarioRes handle(
        ProbeSpot spot,
        Contracts.YieldReq request) {
        String value = "spot=" + spot.context().spotRid() + ";correlation=" + request.correlationId();
        evidence.record(request.requestId(), "yield-started", value);
        evidence.record(request.requestId(), "yield-released", value);
        spot.context().outbound()
            .requestToChannel(
                Contracts.DELAY_CHANNEL,
                new Contracts.DelayReq(request.requestId(), 350))
            .timeout(Duration.ofSeconds(5))
            .yield(Contracts.DelayRes.class);
        evidence.record(request.requestId(), "yield-resumed", value);
        evidence.record(request.requestId(), "yield-completed", value);
        return new Contracts.ScenarioRes(
            request.scenarioId(),
            request.requestId(),
            spot.context().nodeRid().toString());
    }
}
