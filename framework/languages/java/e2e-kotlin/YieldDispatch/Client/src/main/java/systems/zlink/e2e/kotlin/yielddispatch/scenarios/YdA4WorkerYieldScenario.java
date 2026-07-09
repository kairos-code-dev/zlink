package systems.zlink.e2e.kotlin.yielddispatch.scenarios;

import java.util.List;
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
            connector.send(new Contracts.WorkerYieldMsg(requestId, 4000))
                .metadata(Contracts.TARGET_NODE_RID_METADATA, "play-a")
                .metadata(Contracts.SPOT_RID_METADATA, "room-a"));
        ClientStreamSupport.waitForEvidence(connector, requestId, "room-a", "worker-yield-released");
        ClientStreamSupport.send(
            connector.send(new Contracts.ProbeMsg(requestId, "worker-probe"))
                .metadata(Contracts.TARGET_NODE_RID_METADATA, "play-a")
                .metadata(Contracts.SPOT_RID_METADATA, "room-a"));
        List<String> markers = waitForMarkers(
            connector,
            requestId,
            "room-a",
            List.of(
                "worker-yield-started",
                "worker-yield-released",
                "probe-started",
                "probe-completed",
                "worker-yield-resumed",
                "worker-yield-completed"));
        ScenarioAssert.containsMarkersInOrder(
            markers,
            "worker-yield-started",
            "worker-yield-released",
            "probe-started",
            "probe-completed",
            "worker-yield-resumed",
            "worker-yield-completed");
        System.out.println("scenario YD-A4 passed");
    }

    private static List<String> waitForMarkers(
        ZLinkStreamConnector connector,
        String requestId,
        String spotRid,
        List<String> expected) {
        List<String> latest = List.of();
        for (int attempt = 0; attempt < 80; attempt++) {
            Contracts.EvidenceRes evidence = ClientStreamSupport.evidence(connector, requestId, spotRid);
            latest = evidence.markers();
            if (startsWithMarkers(latest, expected)) {
                return latest;
            }
            ClientStreamSupport.sleep(100);
        }
        throw new IllegalStateException(
            "YD-A4 markers not observed in order: expected=" + expected + " actual=" + latest);
    }

    private static boolean startsWithMarkers(
        List<String> actual,
        List<String> expected) {
        if (actual.size() < expected.size()) {
            return false;
        }
        for (int i = 0; i < expected.size(); i++) {
            if (!actual.get(i).startsWith(expected.get(i) + "|")) {
                return false;
            }
        }
        return true;
    }
}
