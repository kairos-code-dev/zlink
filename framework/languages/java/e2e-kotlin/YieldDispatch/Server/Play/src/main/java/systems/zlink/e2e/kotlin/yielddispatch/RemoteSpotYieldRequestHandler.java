package systems.zlink.e2e.kotlin.yielddispatch;

import java.time.Duration;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.handlers.ZLinkSpotRequest;

public final class RemoteSpotYieldRequestHandler {
    private final PlayEvidenceStore evidence;

    public RemoteSpotYieldRequestHandler(PlayEvidenceStore evidence) {
        this.evidence = evidence;
    }

    @ZLinkSpotRequest
    public Contracts.ScenarioReply handle(
        ProbeSpot spot,
        Contracts.RemoteSpotYieldRequest request) {
        String value = "spot=" + spot.context().spotRid() + ";target=" + request.targetSpotRid();
        evidence.record(request.requestId(), "remote-yield-started", value);
        evidence.record(request.requestId(), "remote-yield-released", value);
        Contracts.ScenarioReply targetReply = spot.context().outbound()
            .requestToSpot(
                RoutingId.from(request.targetSpotRid()),
                new Contracts.YieldRequest("YD-D2", request.requestId(), "remote-spot"))
            .timeout(Duration.ofSeconds(5))
            .yield(Contracts.ScenarioReply.class);
        String resumed = value + ";targetNode=" + targetReply.result();
        evidence.record(request.requestId(), "remote-yield-resumed", resumed);
        evidence.record(request.requestId(), "remote-yield-completed", resumed);
        return new Contracts.ScenarioReply("YD-D2", request.requestId(), spot.context().nodeRid().toString());
    }
}
