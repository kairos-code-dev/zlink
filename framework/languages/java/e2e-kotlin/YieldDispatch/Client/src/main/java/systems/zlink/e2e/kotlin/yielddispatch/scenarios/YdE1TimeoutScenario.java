package systems.zlink.e2e.kotlin.yielddispatch.scenarios;

import java.util.UUID;
import systems.zlink.e2e.kotlin.yielddispatch.Contracts;
import systems.zlink.e2e.kotlin.yielddispatch.support.ClientStreamSupport;
import systems.zlink.e2e.kotlin.yielddispatch.support.ScenarioAssert;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class YdE1TimeoutScenario {
    private YdE1TimeoutScenario() {
    }

    public static void run(ZLinkStreamConnector connector) {
        String spotRid = "yield-timeout-" + UUID.randomUUID().toString().replace("-", "");
        Contracts.EnsureSpotRes spot = ClientStreamSupport.await(
            connector.request(new Contracts.EnsureSpotReq(spotRid))
                .metadata(Contracts.TARGET_NODE_RID_METADATA, "play-a")
                .timeout(ClientStreamSupport.REQUEST_TIMEOUT),
            Contracts.EnsureSpotRes.class);
        ScenarioAssert.that(spotRid.equals(spot.spotRid()), "YD-E1 timeout spot creation mismatch");
        String requestId = "YD-E1-" + UUID.randomUUID().toString().replace("-", "");
        Contracts.YieldTimeoutRes timeout = ClientStreamSupport.await(
            connector.request(new Contracts.YieldTimeoutReq(requestId, 700, 100))
                .packetName("YieldTimeoutReq")
                .metadata(Contracts.TARGET_NODE_RID_METADATA, "play-a")
                .metadata(Contracts.SPOT_RID_METADATA, spotRid)
                .timeout(ClientStreamSupport.REQUEST_TIMEOUT),
            Contracts.YieldTimeoutRes.class);
        ScenarioAssert.that(timeout.timedOut(), "YD-E1 expected public timeout result");
        ScenarioAssert.that(requestId.equals(timeout.requestId()), "YD-E1 timeout reply request mismatch");
        ScenarioAssert.that(spotRid.equals(timeout.spotRid()), "YD-E1 timeout reply spot mismatch");
        Contracts.EvidenceRes timeoutEvidence = ClientStreamSupport.waitForEvidence(
            connector,
            requestId,
            spotRid,
            "timeout-yield-completed");
        ScenarioAssert.that(timeoutEvidence.markers().stream().anyMatch(entry ->
                entry.startsWith("timeout-yield-completed|") && entry.contains("error=")),
            "YD-E1 timeout marker missing public error evidence");
        ClientStreamSupport.send(
            connector.send(new Contracts.ProbeMsg(requestId, "timeout-probe"))
                .packetName("ProbeMsg")
                .metadata(Contracts.TARGET_NODE_RID_METADATA, "play-a")
                .metadata(Contracts.SPOT_RID_METADATA, spotRid));
        Contracts.EvidenceRes evidence = ClientStreamSupport.waitForEvidence(
            connector,
            requestId,
            spotRid,
            "probe-completed");
        ScenarioAssert.containsMarkersInOrder(evidence.markers(),
            "timeout-yield-started",
            "timeout-yield-released",
            "timeout-yield-completed",
            "probe-started",
            "probe-completed");
        ScenarioAssert.that(evidence.markers().stream().anyMatch(entry ->
                entry.startsWith("probe-completed|") && entry.contains("marker=timeout-probe")),
            "YD-E1 post-timeout probe marker missing");
        System.out.println("scenario YD-E1 passed");
    }
}
