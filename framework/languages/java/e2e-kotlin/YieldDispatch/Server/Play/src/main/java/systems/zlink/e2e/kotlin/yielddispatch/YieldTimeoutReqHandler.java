package systems.zlink.e2e.kotlin.yielddispatch;

import java.time.Duration;
import systems.zlink.framework.spots.ZLinkSpotRequestHandler;

public final class YieldTimeoutReqHandler
    implements ZLinkSpotRequestHandler<ProbeSpot, Contracts.YieldTimeoutReq, Contracts.YieldTimeoutRes> {
    private final PlayEvidenceStore evidence;

    public YieldTimeoutReqHandler(PlayEvidenceStore evidence) {
        this.evidence = evidence;
    }

    @Override
    public Contracts.YieldTimeoutRes handle(
        ProbeSpot spot,
        Contracts.YieldTimeoutReq request) {
        String value = "spot=" + spot.context().spotRid() + ";node=" + spot.context().nodeRid();
        evidence.record(request.requestId(), "timeout-yield-started", value);
        try {
            evidence.record(request.requestId(), "timeout-yield-released", value);
            spot.context().outbound()
                .requestToChannel(
                    Contracts.DELAY_CHANNEL,
                    new Contracts.DelayReq(request.requestId(), request.delayMillis()))
                .timeout(Duration.ofMillis(request.timeoutMillis()))
                .yield(Contracts.DelayRes.class);
            evidence.record(request.requestId(), "timeout-yield-unexpected-resumed", value);
            return new Contracts.YieldTimeoutRes(
                "YD-E1",
                request.requestId(),
                spot.context().spotRid().toString(),
                spot.context().nodeRid().toString(),
                false,
                "");
        } catch (RuntimeException error) {
            String errorType = error.getClass().getSimpleName();
            evidence.record(request.requestId(), "timeout-yield-completed", value + ";error=" + errorType);
            return new Contracts.YieldTimeoutRes(
                "YD-E1",
                request.requestId(),
                spot.context().spotRid().toString(),
                spot.context().nodeRid().toString(),
                true,
                errorType);
        }
    }
}
