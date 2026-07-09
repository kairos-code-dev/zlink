package systems.zlink.e2e.kotlin.yielddispatch.scenarios;

import java.util.UUID;
import systems.zlink.e2e.kotlin.yielddispatch.Contracts;
import systems.zlink.e2e.kotlin.yielddispatch.support.ClientStreamSupport;
import systems.zlink.e2e.kotlin.yielddispatch.support.ScenarioAssert;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class YdA3ContinuationContextScenario {
    private YdA3ContinuationContextScenario() {
    }

    public static void run(
        ZLinkStreamConnector roomA,
        String actorA,
        ZLinkStreamConnector roomB,
        String actorB) {
        String requestId = "YD-A3-" + UUID.randomUUID();
        String spotRid = "room-a";
        String correlationId = "corr-a3";
        ClientStreamSupport.send(
            roomA.send(new Contracts.YieldMsg(requestId, 80, correlationId))
                .metadata(Contracts.SPOT_RID_METADATA, spotRid));

        Contracts.EvidenceRes evidence = ClientStreamSupport.waitForEvidence(
            roomA,
            requestId,
            spotRid,
            "yield-completed");
        ScenarioAssert.containsMarkersInOrder(
            evidence.markers(),
            "yield-started",
            "yield-released",
            "yield-resumed",
            "yield-completed");

        for (String marker : evidence.markers()) {
            if (!marker.startsWith("yield-")) {
                continue;
            }
            ScenarioAssert.that(
                marker.contains("|spot=" + spotRid + ";"),
                "YD-A3 continuation marker lost target spot: " + marker);
            ScenarioAssert.that(
                marker.contains(";correlation=" + correlationId + ";"),
                "YD-A3 continuation marker lost correlation id: " + marker);
        }
        System.out.println("scenario YD-A3 passed");
    }
}
