package systems.zlink.e2e.kotlin.yielddispatch.scenarios;

import java.util.UUID;
import systems.zlink.e2e.kotlin.yielddispatch.Contracts;
import systems.zlink.e2e.kotlin.yielddispatch.support.ClientStreamSupport;
import systems.zlink.e2e.kotlin.yielddispatch.support.ScenarioAssert;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class YdA1BasicTerminatorScenario {
    private YdA1BasicTerminatorScenario() {
    }

    public static Result run(ZLinkStreamConnector connector, String actorId) {
        String requestId = "YD-A1-" + UUID.randomUUID();
        ClientStreamSupport.send(
            connector.send(new Contracts.HoldMsg(requestId, 350))
                .metadata(Contracts.SPOT_RID_METADATA, "room-a"));
        ClientStreamSupport.sleep(120);
        ClientStreamSupport.send(
            connector.send(new Contracts.ProbeMsg(requestId, "hold-probe"))
                .metadata(Contracts.SPOT_RID_METADATA, "room-a"));
        Contracts.EvidenceRes evidence = ClientStreamSupport.waitForEvidence(
            connector,
            requestId,
            "probe-completed");
        ScenarioAssert.containsMarkersInOrder(
            evidence.markers(),
            "hold-started",
            "hold-resumed",
            "hold-completed",
            "probe-started");
        System.out.println("scenario YD-A1 passed");
        return new Result(actorId);
    }

    public record Result(String actorId) {
    }
}
