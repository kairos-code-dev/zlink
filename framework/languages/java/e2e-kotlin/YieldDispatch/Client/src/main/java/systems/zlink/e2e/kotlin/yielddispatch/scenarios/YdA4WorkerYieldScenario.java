package systems.zlink.e2e.kotlin.yielddispatch.scenarios;

import java.util.UUID;
import systems.zlink.e2e.kotlin.yielddispatch.Contracts;
import systems.zlink.e2e.kotlin.yielddispatch.support.ClientStreamSupport;
import systems.zlink.e2e.kotlin.yielddispatch.support.ScenarioAssert;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class YdA4WorkerYieldScenario {
    private YdA4WorkerYieldScenario() {
    }

    public static void run(ZLinkStreamConnector connector, String actorId) {
        ScenarioAssert.that(actorId != null && !actorId.isBlank(), "YD-A4 actor setup mismatch");
        String requestId = "YD-A4-" + UUID.randomUUID();
        ClientStreamSupport.send(
            connector.send(new Contracts.WorkerYieldMsg(requestId, 1200))
                .metadata(Contracts.TARGET_NODE_RID_METADATA, "play-a")
                .metadata(Contracts.SPOT_RID_METADATA, "room-a"));
        ClientStreamSupport.waitForEvidence(connector, requestId, "room-a", "worker-yield-released");
        ClientStreamSupport.send(
            connector.send(new Contracts.ProbeMsg(requestId, "worker-probe"))
                .metadata(Contracts.TARGET_NODE_RID_METADATA, "play-a")
                .metadata(Contracts.SPOT_RID_METADATA, "room-a"));
        Contracts.EvidenceRes evidence = ClientStreamSupport.waitForEvidence(
            connector,
            requestId,
            "room-a",
            "worker-yield-completed");
        ScenarioAssert.containsMarkersInOrder(
            evidence.markers(),
            "worker-yield-started",
            "worker-yield-released",
            "probe-started",
            "probe-completed",
            "worker-yield-resumed",
            "worker-yield-completed");
        System.out.println("scenario YD-A4 passed");
    }
}
