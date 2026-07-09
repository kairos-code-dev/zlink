package systems.zlink.e2e.kotlin.yielddispatch.scenarios;

import java.util.UUID;
import systems.zlink.e2e.kotlin.yielddispatch.Contracts;
import systems.zlink.e2e.kotlin.yielddispatch.support.ClientStreamSupport;
import systems.zlink.e2e.kotlin.yielddispatch.support.ScenarioAssert;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class YdC1TimerIsolationScenario {
    private YdC1TimerIsolationScenario() {
    }

    public static void run(ZLinkStreamConnector connector) {
        String spotRid = "yield-timer-" + UUID.randomUUID().toString().replace("-", "");
        Contracts.EnsureSpotRes spot = ClientStreamSupport.await(
            connector.request(new Contracts.EnsureSpotReq(spotRid))
                .metadata(Contracts.TARGET_NODE_RID_METADATA, "play-a")
                .timeout(ClientStreamSupport.REQUEST_TIMEOUT),
            Contracts.EnsureSpotRes.class);
        ScenarioAssert.that(spotRid.equals(spot.spotRid()), "YD-C1 timer spot creation mismatch");
        String requestId = "YD-C1-" + UUID.randomUUID().toString().replace("-", "");
        String yieldTimer = requestId + "-yield";
        String fastTimer = requestId + "-fast";
        ClientStreamSupport.send(
            connector.send(new Contracts.TimerStartMsg(
                    requestId,
                    yieldTimer,
                    "yield-on-first",
                    50,
                    350))
                .metadata(Contracts.TARGET_NODE_RID_METADATA, "play-a")
                .metadata(Contracts.SPOT_RID_METADATA, spotRid));
        ClientStreamSupport.waitForEvidence(connector, requestId, spotRid, "timer-yield-released");
        ClientStreamSupport.send(
            connector.send(new Contracts.TimerStartMsg(
                    requestId,
                    fastTimer,
                    "fast",
                    50,
                    0))
                .metadata(Contracts.TARGET_NODE_RID_METADATA, "play-a")
                .metadata(Contracts.SPOT_RID_METADATA, spotRid));
        ClientStreamSupport.waitForEvidence(connector, requestId, spotRid, "timer-fast-completed");
        Contracts.EvidenceRes evidence = ClientStreamSupport.waitForEvidence(
            connector,
            requestId,
            spotRid,
            "timer-yield-completed");
        ScenarioAssert.containsMarkersInOrder(evidence.markers(),
            "timer-yield-started",
            "timer-yield-released",
            "timer-fast-started",
            "timer-fast-completed",
            "timer-yield-resumed",
            "timer-yield-completed");
        ClientStreamSupport.send(
            connector.send(new Contracts.TimerStopMsg(requestId))
                .metadata(Contracts.TARGET_NODE_RID_METADATA, "play-a")
                .metadata(Contracts.SPOT_RID_METADATA, spotRid));
        System.out.println("scenario YD-C1 passed");
    }
}
