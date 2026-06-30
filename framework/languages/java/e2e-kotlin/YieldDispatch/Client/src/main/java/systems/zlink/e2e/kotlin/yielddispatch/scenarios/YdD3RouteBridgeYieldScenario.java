package systems.zlink.e2e.kotlin.yielddispatch.scenarios;

import java.util.List;
import java.util.UUID;
import systems.zlink.e2e.kotlin.yielddispatch.Contracts;
import systems.zlink.e2e.kotlin.yielddispatch.support.ClientStreamSupport;
import systems.zlink.e2e.kotlin.yielddispatch.support.ScenarioAssert;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class YdD3RouteBridgeYieldScenario {
    private YdD3RouteBridgeYieldScenario() {
    }

    public static void run(ZLinkStreamConnector connector) {
        String requestId = "YD-D3-" + UUID.randomUUID();
        String spotRid = requestId + "-spot";

        Contracts.EnsureSpotRes target = ClientStreamSupport.await(
            connector.request(new Contracts.EnsureSpotReq(spotRid))
                .metadata(Contracts.TARGET_NODE_RID_METADATA, "play-b")
                .timeout(ClientStreamSupport.REQUEST_TIMEOUT),
            Contracts.EnsureSpotRes.class);
        ScenarioAssert.that(spotRid.equals(target.spotRid()), "YD-D3 target spot mismatch");
        ScenarioAssert.that("play-b".equals(target.nodeRid()), "YD-D3 target node mismatch");

        ClientStreamSupport.send(
            connector.send(new Contracts.YieldMsg(requestId, 1000, "route-bridge"))
                .metadata(Contracts.TARGET_NODE_RID_METADATA, "play-b")
                .metadata(Contracts.SPOT_RID_METADATA, spotRid));
        waitForMarkers(connector, requestId, "play-b", List.of(
            "yield-started",
            "yield-released"));

        ClientStreamSupport.send(
            connector.send(new Contracts.ProbeMsg(requestId, "route-bridge-probe"))
                .metadata(Contracts.TARGET_NODE_RID_METADATA, "play-b")
                .metadata(Contracts.SPOT_RID_METADATA, spotRid));
        List<String> markers = waitForMarkers(connector, requestId, "play-b", List.of(
            "yield-started",
            "yield-released",
            "probe-started",
            "probe-completed",
            "yield-resumed",
            "yield-completed"));
        ScenarioAssert.that(
            markers.stream()
                .filter(marker -> marker.startsWith("yield-started|"))
                .anyMatch(marker -> marker.contains("handler=spot") && marker.contains("spot=" + spotRid)),
            "YD-D3 target Spot handler marker missing");
        System.out.println("scenario YD-D3 passed");
    }

    private static List<String> waitForMarkers(
        ZLinkStreamConnector connector,
        String requestId,
        String targetNode,
        List<String> expected) {
        List<String> latest = List.of();
        for (int attempt = 0; attempt < 80; attempt++) {
            Contracts.EvidenceRes evidence = ClientStreamSupport.await(
                connector.request(new Contracts.EvidenceReq(requestId))
                    .metadata(Contracts.TARGET_NODE_RID_METADATA, targetNode)
                    .metadata(Contracts.SPOT_RID_METADATA, "room-a")
                    .timeout(ClientStreamSupport.REQUEST_TIMEOUT),
                Contracts.EvidenceRes.class);
            latest = evidence.markers();
            if (startsWithMarkers(latest, expected)) {
                return latest;
            }
            ClientStreamSupport.sleep(100);
        }
        throw new IllegalStateException(
            "YD-D3 markers not observed in order: expected=" + expected + " actual=" + latest);
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
