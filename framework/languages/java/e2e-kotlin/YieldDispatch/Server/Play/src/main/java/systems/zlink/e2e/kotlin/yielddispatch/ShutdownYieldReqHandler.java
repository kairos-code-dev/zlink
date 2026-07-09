package systems.zlink.e2e.kotlin.yielddispatch;

import java.time.Duration;
import systems.zlink.framework.handlers.ZLinkSpotRequest;

public final class ShutdownYieldReqHandler {
    private final PlayEvidenceStore evidence;

    public ShutdownYieldReqHandler(PlayEvidenceStore evidence) {
        this.evidence = evidence;
    }

    @ZLinkSpotRequest
    public Contracts.ScenarioRes handle(
        ProbeSpot spot,
        Contracts.ShutdownYieldReq request) {
        String value = "spot=" + spot.context().spotRid() + ";scenario=YD-E3";
        evidence.record(request.requestId(), "shutdown-yield-started", value);
        evidence.record(request.requestId(), "shutdown-yield-released", value);
        spot.context().outbound()
            .requestToChannel(
                Contracts.DELAY_CHANNEL,
                new Contracts.DelayReq(request.requestId(), request.delayMillis()))
            .timeout(Duration.ofSeconds(10))
            .yield(Contracts.DelayRes.class);
        evidence.record(request.requestId(), "shutdown-yield-resumed", value);
        evidence.record(request.requestId(), "shutdown-yield-completed", value);
        return new Contracts.ScenarioRes(
            "YD-E3",
            request.requestId(),
            spot.context().nodeRid().toString());
    }
}
