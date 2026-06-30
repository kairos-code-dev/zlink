package systems.zlink.e2e.kotlin.yielddispatch.scenarios;

import systems.zlink.e2e.kotlin.yielddispatch.Contracts;
import systems.zlink.e2e.kotlin.yielddispatch.support.ClientStreamSupport;
import systems.zlink.e2e.kotlin.yielddispatch.support.ScenarioAssert;
import systems.zlink.stream.connector.ZLinkStreamConnector;
import java.util.UUID;

public final class YdA2YieldTerminatorScenario {
    private YdA2YieldTerminatorScenario() {
    }

    public static void run(
        ZLinkStreamConnector connector,
        String actorId,
        YdA1BasicTerminatorScenario.Result previous) {
        ScenarioAssert.that(actorId.equals(previous.actorId()), "YD-A2 actor context mismatch");
        String requestId = "YD-A2-" + UUID.randomUUID();
        ClientStreamSupport.send(
            connector.send(new Contracts.YieldMsg(requestId, 1200, "corr-a2"))
                .metadata(Contracts.SPOT_RID_METADATA, "room-a"));
        ClientStreamSupport.waitForEvidence(connector, requestId, "yield-released");
        ClientStreamSupport.send(
            connector.send(new Contracts.ProbeMsg(requestId, "yield-probe"))
                .metadata(Contracts.SPOT_RID_METADATA, "room-a"));
        Contracts.EvidenceRes evidence = ClientStreamSupport.waitForEvidence(
            connector,
            requestId,
            "yield-completed");
        ScenarioAssert.containsMarkersInOrder(
            evidence.markers(),
            "yield-started",
            "yield-released",
            "probe-started",
            "probe-completed",
            "yield-resumed",
            "yield-completed");
        System.out.println("scenario YD-A2 passed");
    }
}
