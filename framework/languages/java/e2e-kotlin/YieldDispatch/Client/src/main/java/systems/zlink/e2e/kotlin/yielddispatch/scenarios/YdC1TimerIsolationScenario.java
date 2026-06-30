package systems.zlink.e2e.kotlin.yielddispatch.scenarios;

import java.util.List;
import java.util.UUID;
import systems.zlink.e2e.kotlin.yielddispatch.Contracts;
import systems.zlink.e2e.kotlin.yielddispatch.support.ClientStreamSupport;
import systems.zlink.e2e.kotlin.yielddispatch.support.ScenarioAssert;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class YdC1TimerIsolationScenario {
    private YdC1TimerIsolationScenario() {
    }

    public static void run(ZLinkStreamConnector connector) {
        String requestId = "YD-C1-" + UUID.randomUUID();
        String yieldTimer = requestId + "-yield";
        String fastTimer = requestId + "-fast";
        ClientStreamSupport.send(
            connector.send(new Contracts.TimerStartMsg(
                    requestId,
                    yieldTimer,
                    "yield-on-first",
                    50,
                    1500))
                .metadata(Contracts.TARGET_NODE_RID_METADATA, "play-a")
                .metadata(Contracts.SPOT_RID_METADATA, "room-a"));
        waitForMarkers(connector, requestId, List.of(
            "timer-yield-started",
            "timer-yield-released"));
        ClientStreamSupport.send(
            connector.send(new Contracts.TimerStartMsg(
                    requestId,
                    fastTimer,
                    "fast",
                    50,
                    0))
                .metadata(Contracts.TARGET_NODE_RID_METADATA, "play-a")
                .metadata(Contracts.SPOT_RID_METADATA, "room-a"));
        waitForMarkers(connector, requestId, List.of(
            "timer-yield-started",
            "timer-yield-released",
            "timer-fast-started",
            "timer-fast-completed",
            "timer-yield-resumed",
            "timer-yield-completed"));
        ClientStreamSupport.send(
            connector.send(new Contracts.TimerStopMsg(requestId))
                .metadata(Contracts.TARGET_NODE_RID_METADATA, "play-a")
                .metadata(Contracts.SPOT_RID_METADATA, "room-a"));
        System.out.println("scenario YD-C1 passed");
    }

    private static void waitForMarkers(
        ZLinkStreamConnector connector,
        String requestId,
        List<String> expected) {
        List<String> latest = List.of();
        for (int attempt = 0; attempt < 80; attempt++) {
            Contracts.EvidenceRes evidence = ClientStreamSupport.evidence(connector, requestId);
            latest = evidence.markers();
            if (startsWithMarkers(latest, expected)) {
                return;
            }
            ClientStreamSupport.sleep(100);
        }
        throw new IllegalStateException(
            "YD-C1 markers not observed in order: expected=" + expected + " actual=" + latest);
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
