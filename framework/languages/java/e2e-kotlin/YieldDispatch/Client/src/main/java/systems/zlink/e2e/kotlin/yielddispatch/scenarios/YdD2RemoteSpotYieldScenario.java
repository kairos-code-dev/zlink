package systems.zlink.e2e.kotlin.yielddispatch.scenarios;

import java.util.List;
import java.util.UUID;
import systems.zlink.e2e.kotlin.yielddispatch.Contracts;
import systems.zlink.e2e.kotlin.yielddispatch.support.ClientStreamSupport;
import systems.zlink.e2e.kotlin.yielddispatch.support.ScenarioAssert;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class YdD2RemoteSpotYieldScenario {
    private YdD2RemoteSpotYieldScenario() {
    }

    public static void run(ZLinkStreamConnector connector) {
        String requestId = "YD-D2-" + UUID.randomUUID();
        String ownerSpot = requestId + "-owner";
        String targetSpot = requestId + "-target";

        Contracts.EnsureSpotReply owner = ClientStreamSupport.await(
            connector.request(new Contracts.EnsureSpotRequest(ownerSpot))
                .metadata(Contracts.TARGET_NODE_RID_METADATA, "play-a")
                .timeout(ClientStreamSupport.REQUEST_TIMEOUT),
            Contracts.EnsureSpotReply.class);
        ScenarioAssert.that(ownerSpot.equals(owner.spotRid()), "YD-D2 owner spot mismatch");
        ScenarioAssert.that("play-a".equals(owner.nodeRid()), "YD-D2 owner node mismatch");

        Contracts.EnsureSpotReply target = ClientStreamSupport.await(
            connector.request(new Contracts.EnsureSpotRequest(targetSpot))
                .metadata(Contracts.TARGET_NODE_RID_METADATA, "play-b")
                .timeout(ClientStreamSupport.REQUEST_TIMEOUT),
            Contracts.EnsureSpotReply.class);
        ScenarioAssert.that(targetSpot.equals(target.spotRid()), "YD-D2 target spot mismatch");
        ScenarioAssert.that("play-b".equals(target.nodeRid()), "YD-D2 target node mismatch");

        Contracts.ScenarioReply reply = ClientStreamSupport.await(
            connector.request(new Contracts.RemoteSpotYieldRequest(requestId, targetSpot, 350))
                .metadata(Contracts.TARGET_NODE_RID_METADATA, "play-a")
                .metadata(Contracts.SPOT_RID_METADATA, ownerSpot)
                .timeout(ClientStreamSupport.REQUEST_TIMEOUT),
            Contracts.ScenarioReply.class);
        ScenarioAssert.that("YD-D2".equals(reply.scenarioId()), "YD-D2 reply scenario mismatch");
        ScenarioAssert.that(requestId.equals(reply.requestId()), "YD-D2 reply request mismatch");
        ScenarioAssert.that("play-a".equals(reply.result()), "YD-D2 continuation node mismatch");

        Contracts.EvidenceReply ownerEvidence = evidence(connector, requestId, "play-a");
        assertMarkers(ownerEvidence.markers(), List.of(
            "remote-yield-started",
            "remote-yield-released",
            "remote-yield-resumed",
            "remote-yield-completed"));
        ScenarioAssert.that(
            ownerEvidence.markers().stream()
                .filter(marker -> marker.startsWith("remote-yield-resumed|"))
                .anyMatch(marker -> marker.contains("targetNode=play-b")),
            "YD-D2 owner continuation did not observe play-b target");

        Contracts.EvidenceReply targetEvidence = evidence(connector, requestId, "play-b");
        assertMarkers(targetEvidence.markers(), List.of(
            "yield-started",
            "yield-released",
            "yield-resumed",
            "yield-completed"));
        ScenarioAssert.that(
            targetEvidence.markers().stream().noneMatch(marker -> marker.startsWith("remote-yield-resumed|")),
            "YD-D2 target node owned caller continuation");
        System.out.println("scenario YD-D2 passed");
    }

    private static Contracts.EvidenceReply evidence(
        ZLinkStreamConnector connector,
        String requestId,
        String targetNode) {
        return ClientStreamSupport.await(
            connector.request(new Contracts.EvidenceRequest(requestId))
                .metadata(Contracts.TARGET_NODE_RID_METADATA, targetNode)
                .metadata(Contracts.SPOT_RID_METADATA, "room-a")
                .timeout(ClientStreamSupport.REQUEST_TIMEOUT),
            Contracts.EvidenceReply.class);
    }

    private static void assertMarkers(
        List<String> actual,
        List<String> expected) {
        ScenarioAssert.that(actual.size() >= expected.size(), "YD-D2 evidence marker count mismatch: " + actual);
        for (int i = 0; i < expected.size(); i++) {
            ScenarioAssert.that(
                actual.get(i).startsWith(expected.get(i) + "|"),
                "YD-D2 evidence order mismatch: expected=" + expected + " actual=" + actual);
        }
    }
}
