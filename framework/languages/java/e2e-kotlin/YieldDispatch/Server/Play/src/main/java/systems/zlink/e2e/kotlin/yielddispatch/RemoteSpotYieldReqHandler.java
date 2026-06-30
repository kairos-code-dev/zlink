package systems.zlink.e2e.kotlin.yielddispatch;

import java.time.Duration;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.handlers.ZLinkSpotRequest;

public final class RemoteSpotYieldReqHandler {
    private final PlayEvidenceStore evidence;

    public RemoteSpotYieldReqHandler(PlayEvidenceStore evidence) {
        this.evidence = evidence;
    }

    @ZLinkSpotRequest
    public Contracts.ScenarioRes handle(
        ProbeSpot spot,
        Contracts.RemoteSpotYieldReq request) {
        String value = "spot=" + spot.context().spotRid() + ";target=" + request.targetSpotRid();
        evidence.record(request.requestId(), "remote-yield-started", value);
        evidence.record(request.requestId(), "remote-yield-released", value);
        Contracts.ScenarioRes targetReply = spot.context().outbound()
            .requestToSpot(
                RoutingId.from(request.targetSpotRid()),
                new Contracts.YieldReq("YD-D2", request.requestId(), "remote-spot"))
            .timeout(Duration.ofSeconds(5))
            .yield(Contracts.ScenarioRes.class);
        String resumed = value + ";targetNode=" + targetReply.result();
        evidence.record(request.requestId(), "remote-yield-resumed", resumed);
        evidence.record(request.requestId(), "remote-yield-completed", resumed);
        return new Contracts.ScenarioRes("YD-D2", request.requestId(), spot.context().nodeRid().toString());
    }
}
