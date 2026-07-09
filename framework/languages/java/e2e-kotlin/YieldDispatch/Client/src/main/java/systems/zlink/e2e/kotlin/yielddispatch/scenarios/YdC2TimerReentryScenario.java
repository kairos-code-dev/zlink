package systems.zlink.e2e.kotlin.yielddispatch.scenarios;

import java.util.UUID;
import systems.zlink.e2e.kotlin.yielddispatch.Contracts;
import systems.zlink.e2e.kotlin.yielddispatch.support.ClientStreamSupport;
import systems.zlink.e2e.kotlin.yielddispatch.support.ScenarioAssert;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class YdC2TimerReentryScenario {
    private YdC2TimerReentryScenario() {
    }

    public static void run(ZLinkStreamConnector connector) {
        String spotRid = "yield-timer-" + UUID.randomUUID().toString().replace("-", "");
        Contracts.EnsureSpotRes spot = ClientStreamSupport.await(
            connector.request(new Contracts.EnsureSpotReq(spotRid))
                .metadata(Contracts.TARGET_NODE_RID_METADATA, "play-a")
                .timeout(ClientStreamSupport.REQUEST_TIMEOUT),
            Contracts.EnsureSpotRes.class);
        ScenarioAssert.that(spotRid.equals(spot.spotRid()), "YD-C2 timer spot creation mismatch");
        String requestId = "YD-C2-" + UUID.randomUUID().toString().replace("-", "");
        String timerName = requestId + "-same";
        ClientStreamSupport.send(
            connector.send(new Contracts.TimerStartMsg(
                    requestId,
                    timerName,
                    "yield-then-next",
                    50,
                    350))
                .metadata(Contracts.TARGET_NODE_RID_METADATA, "play-a")
                .metadata(Contracts.SPOT_RID_METADATA, spotRid));
        Contracts.EvidenceRes evidence = ClientStreamSupport.waitForEvidence(
            connector,
            requestId,
            spotRid,
            "timer-next-completed");
        ScenarioAssert.containsMarkersInOrder(evidence.markers(),
            "timer-yield-started",
            "timer-yield-released",
            "timer-yield-resumed",
            "timer-yield-completed",
            "timer-next-started",
            "timer-next-completed");
        ClientStreamSupport.send(
            connector.send(new Contracts.TimerStopMsg(requestId))
                .metadata(Contracts.TARGET_NODE_RID_METADATA, "play-a")
                .metadata(Contracts.SPOT_RID_METADATA, spotRid));
        System.out.println("scenario YD-C2 passed");
    }
}
